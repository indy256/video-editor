#include "updater.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTimer>
#include <QVersionNumber>

static void initializeUpdateResources() { Q_INIT_RESOURCE(updater); }

namespace {
constexpr auto title = "Update to latest version";
bool writeFile(const QString &path, const QByteArray &data) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.flush();
}
bool matchesAsset(const QString &path, const AppUpdate::Asset &asset) {
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return file.open(QIODevice::ReadOnly) && file.size() == asset.size
        && hash.addData(&file) && hash.result().toHex() == asset.sha256;
}
QProcessEnvironment cleanEnvironment() {
    auto environment = QProcessEnvironment::systemEnvironment();
    // Detached helpers and the new AppImage must not inherit the old runtime.
    for (const char *key : {"APPIMAGE", "APPDIR", "OWD", "LD_LIBRARY_PATH",
                            "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH",
                            "VIDEO_EDITOR_LAUNCHER"})
        environment.remove(QString::fromLatin1(key));
    return environment;
}
bool download(QNetworkAccessManager &network, QProgressDialog &progress,
              const QUrl &url, QIODevice &output, qint64 limit, QString &error) {
    if (!output.open(QIODevice::WriteOnly)) { error = output.errorString(); return false; }
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "VideoEditor-Updater");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(30000);
    auto *reply = network.get(request);
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    qint64 total = 0;
    const auto consume = [&] {
        const QByteArray bytes = reply->readAll();
        total += bytes.size();
        if (total > limit) error = "The download exceeds the expected size.";
        else if (output.write(bytes) != bytes.size()) error = output.errorString();
        if (!error.isEmpty()) reply->abort();
    };
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, consume);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&progress, &QProgressDialog::canceled, &loop, [&] { reply->abort(); });
    QObject::connect(&deadline, &QTimer::timeout, &loop, [&] {
        error = "The download timed out.";
        reply->abort();
    });
    deadline.start(300000);
    if (!reply->isFinished()) loop.exec();
    if (error.isEmpty()) consume();
    if (error.isEmpty() && reply->error() != QNetworkReply::NoError) error = reply->errorString();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (error.isEmpty() && status != 200) error = QString("The server returned HTTP %1.").arg(status);
    reply->deleteLater();
    return error.isEmpty() && !progress.wasCanceled();
}
#ifdef Q_OS_MACOS
bool runTool(const QString &program, const QStringList &args, QProgressDialog &progress, QString &error) {
    QProcess process;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&process, &QProcess::finished, &loop, &QEventLoop::quit);
    QObject::connect(&process, &QProcess::errorOccurred, &loop, &QEventLoop::quit);
    QObject::connect(&progress, &QProgressDialog::canceled, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    process.start(program, args);
    timer.start(60000);
    loop.exec();
    if (process.state() != QProcess::NotRunning) { process.kill(); process.waitForFinished(1000); }
    if (!timer.isActive() || process.error() == QProcess::FailedToStart
        || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        error = "Could not prepare the application bundle: " + process.errorString()
            + "\n" + QString::fromUtf8(process.readAllStandardError()).left(2000);
        return false;
    }
    return !progress.wasCanceled();
}
#endif
}

bool AppUpdate::releaseAsset(const QJsonObject &release, const QString &name, Asset &asset) {
    asset = {};
    const QString tag = release.value("tag_name").toString();
    if (release.value("draft").toBool() || release.value("prerelease").toBool()
        || !QRegularExpression("^v[0-9]+[.][0-9]+[.][0-9]+([-+][0-9A-Za-z.+-]+)?$").match(tag).hasMatch()) return false;
    for (const auto &entry : release.value("assets").toArray()) {
        const auto object = entry.toObject();
        if (object.value("name").toString().compare(name, Qt::CaseInsensitive)) continue;
        const QString digest = object.value("digest").toString();
        const QUrl url(object.value("browser_download_url").toString());
        const qint64 size = object.value("size").toInteger();
        if (!QRegularExpression("^sha256:[0-9a-f]{64}$").match(digest).hasMatch()
            || size <= 0 || size > 1024LL * 1024 * 1024 || !url.isValid()
            || url.scheme() != "https" || url.host() != "github.com" || !url.userInfo().isEmpty()
            || (url.port() != -1 && url.port() != 443)
            || url.path() != "/indy256/video-editor/releases/download/" + tag + "/" + name
            || url.hasQuery() || url.hasFragment()) return false;
        asset = {url, digest.mid(7).toLatin1(), size};
        return true;
    }
    return false;
}

bool AppUpdate::installLatest(QWidget *parent, const QStringList &restartArguments) {
    // Explicitly reference the resource initializer because editor_core is static.
    initializeUpdateResources();
    const auto fail = [parent](const QString &message) {
        QMessageBox::warning(parent, title, message);
        return false;
    };
    QString target = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
    QString name;
#if defined(Q_OS_WIN)
#if defined(Q_PROCESSOR_X86_64)
    name = "ve-windows-x64.exe";
#elif defined(Q_PROCESSOR_ARM_64)
    name = "ve-windows-arm64.exe";
#endif
    const QString launcher = qEnvironmentVariable("VIDEO_EDITOR_LAUNCHER");
    if (!launcher.isEmpty()) target = QFileInfo(launcher).canonicalFilePath();
#elif defined(Q_OS_LINUX) && defined(Q_PROCESSOR_X86_64)
    name = "ve-linux-x64.AppImage";
    target = QFileInfo(qEnvironmentVariable("APPIMAGE")).canonicalFilePath();
    if (qEnvironmentVariableIsEmpty("APPIMAGE"))
        return fail("Run the AppImage release to update automatically.");
#elif defined(Q_OS_MACOS) && defined(Q_PROCESSOR_ARM_64)
    name = "ve-macos-arm64.dmg";
    QDir bundle(QCoreApplication::applicationDirPath());
    if (!bundle.cdUp() || !bundle.cdUp() || !bundle.dirName().endsWith(".app"))
        return fail("Run the installed VideoEditor.app to update automatically.");
    target = QFileInfo(bundle.absolutePath()).canonicalFilePath();
#endif
    if (name.isEmpty()) return fail("No release is available for this operating system and architecture.");
    if (target.isEmpty() || !QFileInfo::exists(target)) return fail("Cannot locate the installed application.");
    QProgressDialog progress("Checking the latest release...", "Cancel", 0, 0, parent);
    progress.setWindowTitle(title);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.show();
    QNetworkAccessManager network;
    QString error;
    const auto failed = [&] { progress.hide(); return progress.wasCanceled() ? false : fail(error); };
    QBuffer metadata;
    if (!download(network, progress, QUrl("https://api.github.com/repos/indy256/video-editor/releases/latest"),
                  metadata, 2 * 1024 * 1024, error)) return failed();
    const auto release = QJsonDocument::fromJson(metadata.data()).object();
    Asset asset;
    if (!releaseAsset(release, name, asset))
        return fail("The latest release has no verified download for " + name + ". Try again after its builds finish.");
    const QString latestVersion = release.value("tag_name").toString().mid(1);
    const QString currentVersion = QCoreApplication::applicationVersion();
    const auto latest = QVersionNumber::fromString(latestVersion);
    // Local developer builds can have an older default version than the published packages.
    // Releases through 1.0.2 predate this updater and its restart arguments.
    const bool predatesUpdater = latest <= QVersionNumber(1, 0, 2);
    const bool newerThanRelease = QVersionNumber::fromString(currentVersion) > latest;
#ifdef Q_OS_MACOS
    const bool matchesRelease = currentVersion == latestVersion;
#else
    const bool matchesRelease = matchesAsset(target, asset);
#endif
    if (predatesUpdater || newerThanRelease || matchesRelease) {
        progress.hide();
        QMessageBox::information(parent, title, predatesUpdater
            ? "The latest published release predates this build. Check again after the next release."
            : "You already have the latest version.");
        return false;
    }
    const QString installDirectory = QFileInfo(target).absolutePath();
    QTemporaryDir stage(installDirectory + "/.video-editor-update-XXXXXX");
    if (!stage.isValid()) return fail("The application folder is not writable. Move the app to a writable folder and try again.");
    progress.setLabelText("Downloading " + release.value("tag_name").toString() + "...");
    QString source = stage.filePath("download");
    QFile package(source);
    if (!download(network, progress, asset.url, package, asset.size, error)) return failed();
    if (!package.flush()) return fail(package.errorString());
    package.close();
    progress.setLabelText("Verifying the download...");
    if (!matchesAsset(source, asset))
        return fail("The downloaded package failed verification.");
#ifdef Q_OS_MACOS
    const QString mount = stage.filePath("mount");
    QDir().mkpath(mount);
    const bool mounted = runTool("/usr/bin/hdiutil", {"attach", "-readonly", "-nobrowse", "-mountpoint", mount, source}, progress, error);
    source = stage.filePath("new.app");
    const bool copied = mounted && runTool("/usr/bin/ditto", {mount + "/VideoEditor.app", source}, progress, error);
    QProcess detach;
    detach.start("/usr/bin/hdiutil", {"detach", mount});
    if (!detach.waitForFinished(15000)) { detach.kill(); detach.waitForFinished(1000); }
    if (detach.exitStatus() != QProcess::NormalExit || detach.exitCode() != 0) {
        stage.setAutoRemove(false); // Never recursively delete a mounted image.
        return fail("Could not detach the disk image. Update files remain in " + stage.path());
    }
    if (!copied) return failed();
    if (!QFileInfo(source + "/Contents/MacOS/VideoEditor").isExecutable())
        return fail("The release does not contain a valid VideoEditor.app.");
#else
    if (!QFile::setPermissions(source, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
            | QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther))
        return fail("Cannot prepare the downloaded application.");
#endif
    if (progress.wasCanceled()) return false;
    const QString ready = stage.filePath("ready");
    const QString commit = stage.filePath("commit");
    QProcess installer;
#ifdef Q_OS_WIN
    const QString script = stage.filePath("install.ps1");
    const QString plan = stage.filePath("plan.json");
    const QJsonObject data{{"target", target}, {"source", source}, {"arguments", QJsonArray::fromStringList(restartArguments)},
                           {"pid", QCoreApplication::applicationPid()}};
    if (!QFile::copy(":/updates/update-windows.ps1", script) || !writeFile(plan, QJsonDocument(data).toJson()))
        return fail("Cannot prepare the update helper.");
    installer.setProgram(qEnvironmentVariable("SystemRoot") + "/System32/WindowsPowerShell/v1.0/powershell.exe");
    installer.setArguments({"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
                           "-WindowStyle", "Hidden", "-File", script, "-PlanPath", plan});
    installer.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= 0x08000000; // CREATE_NO_WINDOW
    });
#else
    const QString script = stage.filePath("install.sh");
    if (!QFile::copy(":/updates/update-unix.sh", script)) return fail("Cannot prepare the update helper.");
    installer.setProgram("/bin/sh");
    installer.setArguments(QStringList{script, target, source, QString::number(QCoreApplication::applicationPid())}
                           + restartArguments);
#endif
    installer.setWorkingDirectory(installDirectory);
    installer.setProcessEnvironment(cleanEnvironment());
    installer.setProcessChannelMode(QProcess::MergedChannels);
    installer.setStandardOutputFile(QProcess::nullDevice());
    if (!installer.startDetached()) return fail("Cannot start the update helper.");
    stage.setAutoRemove(false);
    progress.setCancelButton(nullptr);
    progress.setLabelText("Preparing to restart...");
    QEventLoop loop;
    QTimer poll, deadline;
    poll.setInterval(50);
    deadline.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (QFileInfo::exists(ready)) loop.quit(); });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start();
    deadline.start(15000);
    loop.exec();
    progress.hide();
    if (!QFileInfo::exists(ready) || !writeFile(commit, "install"))
        return fail("The update helper could not prepare the installation.");
    return true;
}
