#include "updater.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

class UpdaterTests : public QObject {
    Q_OBJECT
private slots:
    void releaseValidation() {
        const QString name = "ve-windows-x64.exe";
        const QString url = "https://github.com/indy256/video-editor/releases/download/v1.2.0/" + name;
        const QJsonObject valid{{"name", name.toUpper()}, {"size", 123},
            {"digest", "sha256:" + QString(64, 'a')}, {"browser_download_url", url}};
        const auto release = [](const QJsonObject &asset) {
            return QJsonObject{{"tag_name", "v1.2.0"}, {"assets", QJsonArray{asset}}};
        };
        AppUpdate::Asset asset;
        QVERIFY(AppUpdate::releaseAsset(release(valid), name, asset));
        QCOMPARE(asset.size, 123);
        QCOMPARE(asset.sha256, QByteArray(64, 'a'));
        QVERIFY(!AppUpdate::releaseAsset(release(valid), "ve-windows-arm64.exe", asset));
        for (const QString &badUrl : {
                 "http://github.com/indy256/video-editor/releases/download/v1.2.0/" + name,
                 "https://example.com/indy256/video-editor/releases/download/v1.2.0/" + name,
                 "https://github.com/other/repo/releases/download/v1.2.0/" + name,
                 "https://user@github.com/indy256/video-editor/releases/download/v1.2.0/" + name,
                 "https://github.com:444/indy256/video-editor/releases/download/v1.2.0/" + name,
                 "https://github.com/indy256/video-editor/releases/download/v1.1.0/" + name,
             }) {
            auto bad = valid;
            bad["browser_download_url"] = badUrl;
            QVERIFY(!AppUpdate::releaseAsset(release(bad), name, asset));
        }
        for (const QString &badHash : {QString(), QString("sha256:abcd"), "sha512:" + QString(64, 'a')}) {
            auto bad = valid;
            bad["digest"] = badHash;
            QVERIFY(!AppUpdate::releaseAsset(release(bad), name, asset));
        }
        for (qint64 size : {qint64(0), qint64(-1), 1024LL * 1024 * 1024 + 1}) {
            auto bad = valid;
            bad["size"] = size;
            QVERIFY(!AppUpdate::releaseAsset(release(bad), name, asset));
        }
        for (const auto *key : {"draft", "prerelease"}) {
            auto bad = release(valid);
            bad[key] = true;
            QVERIFY(!AppUpdate::releaseAsset(bad, name, asset));
        }
        QVERIFY(!AppUpdate::releaseAsset({}, name, asset));
    }
    void windowsInstall_data() {
        QTest::addColumn<bool>("invalidProcess");
        QTest::newRow("success") << false;
        QTest::newRow("preparation-failure") << true;
    }
    void windowsInstall() {
#ifndef Q_OS_WIN
        QSKIP("Windows helper integration test");
#else
        Q_INIT_RESOURCE(updater);
        QFETCH(bool, invalidProcess);
        QTemporaryDir root(QDir::tempPath() + "/editor update ' & [test]-XXXXXX");
        QVERIFY(root.isValid());
        const QString stage = root.filePath(".video-editor-update-test");
        QVERIFY(QDir().mkdir(stage));
        const QString target = root.filePath("Video Editor.exe");
        const QString source = stage + "/download";
        const QString marker = root.filePath("video ' & [1].restarted");
        QFile old(target);
        QVERIFY(old.open(QIODevice::WriteOnly));
        old.write("old application");
        old.close();
        QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(), source));
        QVERIFY(QFile::copy(":/updates/update-windows.ps1", stage + "/install.ps1"));
        QProcess sleeper;
        sleeper.start(QCoreApplication::applicationFilePath(), {"--wait-for-update"});
        QVERIFY(sleeper.waitForStarted());
        const QJsonArray arguments{marker, "--resume-after-update", "1250"};
        const QJsonObject plan{{"target", target}, {"source", source}, {"arguments", arguments},
                              {"pid", invalidProcess ? qint64(-1) : sleeper.processId()}};
        QFile planFile(stage + "/plan.json");
        QVERIFY(planFile.open(QIODevice::WriteOnly));
        planFile.write(QJsonDocument(plan).toJson());
        planFile.close();
        QProcess installer;
        installer.setWorkingDirectory(root.path());
        installer.setStandardOutputFile(QProcess::nullDevice());
        installer.setProcessChannelMode(QProcess::MergedChannels);
        installer.start(qEnvironmentVariable("SystemRoot") + "/System32/WindowsPowerShell/v1.0/powershell.exe",
            {"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", stage + "/install.ps1",
             "-PlanPath", stage + "/plan.json"});
        QVERIFY(installer.waitForStarted());
        if (invalidProcess) {
            QVERIFY(installer.waitForFinished(30000));
            QCOMPARE(installer.exitCode(), 1);
            QVERIFY(!QDir(stage).exists());
            QVERIFY(old.open(QIODevice::ReadOnly));
            QCOMPARE(old.readAll(), QByteArray("old application"));
            sleeper.kill();
            QVERIFY(sleeper.waitForFinished());
            return;
        }
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(stage + "/ready"), 15000);
        QVERIFY(old.open(QIODevice::ReadOnly));
        QCOMPARE(old.readAll(), QByteArray("old application"));
        old.close();
        QFile commit(stage + "/commit");
        QVERIFY(commit.open(QIODevice::WriteOnly));
        commit.close();
        QTest::qWait(200);
        QVERIFY(!QFile::exists(marker)); // Must wait for the old editor to exit.
        sleeper.kill();
        QVERIFY(sleeper.waitForFinished());
        QVERIFY(installer.waitForFinished(30000));
        QCOMPARE(installer.exitCode(), 0);
        QVERIFY(!QDir(stage).exists());
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(marker), 10000);
        QFile recorded(marker);
        QVERIFY(recorded.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(recorded.readAll()), QString("1250\n") + marker);
        QTest::qWait(200); // Let the restarted test executable release its file handle.
#endif
    }
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().value(1) == "--wait-for-update") {
        QTimer::singleShot(60000, &app, &QCoreApplication::quit);
        return app.exec();
    }
    if (app.arguments().value(2) == "--resume-after-update" && app.arguments().size() == 4) {
        const QString path = app.arguments().at(1);
        QFile marker(path);
        if (!marker.open(QIODevice::WriteOnly)) return 1;
        marker.write((app.arguments().at(3) + '\n' + path).toUtf8());
        return 0;
    }
    UpdaterTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "updater_tests.moc"
