#include "editor.h"
#include <QApplication>
#include <QFileInfo>
#include <QIcon>
#include <QMediaPlayer>
#include <QTemporaryDir>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>

// Exercise the installed encoder and Qt decoder without opening the editor.
static int checkRuntime(QApplication &app, const QString &source) {
    QTemporaryDir directory;
    if (!directory.isValid() || !QFileInfo(source).isFile()) return 1;
    for (const auto &tool : {"ffmpeg", "yt-dlp", "deno"})
        if (findTool(tool).isEmpty()) return 1;
    MediaJobs jobs;
    QMediaPlayer player;
    QVideoSink video;
    player.setVideoSink(&video);
    QObject::connect(&jobs, &MediaJobs::exported, &player, [&](const QString &path) {
        player.setSource(QUrl::fromLocalFile(path)); player.play();
    });
    QObject::connect(&jobs, &MediaJobs::failed, &app, [&](const QString &message) {
        qWarning().noquote() << message; app.exit(1);
    });
    QObject::connect(&player, &QMediaPlayer::errorOccurred, &app, [&] { app.exit(1); });
    QObject::connect(&video, &QVideoSink::videoFrameChanged, &app, [&](const QVideoFrame &frame) {
        if (frame.isValid() && !frame.toImage().isNull()) app.exit(0);
    });
    QTimer::singleShot(30000, &app, [&] { app.exit(1); });
    QTimer::singleShot(0, &jobs, [&] { jobs.exportClip(source, 0, 500, directory.filePath("clip.mp4")); });
    return app.exec();
}

int main(int argc, char **argv) {
    qputenv("QT_MEDIA_BACKEND", "ffmpeg");
    QApplication app(argc, argv);
    app.setApplicationName("VideoEditor"); app.setOrganizationName("VideoEditor");
    app.setApplicationVersion(QStringLiteral(VIDEO_EDITOR_VERSION));
    const auto arguments = app.arguments();
    if (arguments.value(1) == "--runtime-check") return checkRuntime(app, arguments.value(2));
    app.setWindowIcon(QIcon(":/video-editor.png"));
    EditorWindow window; window.show();
    const QString source = arguments.value(1);
    const bool resuming = arguments.value(2) == "--resume-after-update";
    bool positionValid = false;
    const auto position = arguments.value(3).toLongLong(&positionValid);
    if (!source.isEmpty() && (!resuming || positionValid))
        window.openSource(source, resuming ? std::optional{position} : std::nullopt);
    return app.exec();
}
