#include "editor.h"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
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
    const auto arguments = app.arguments();
    if (arguments.value(1) == "--runtime-check") return checkRuntime(app, arguments.value(2));
    EditorWindow window; window.show();
    if (argc > 1) {
        const auto input = QString::fromLocal8Bit(argv[1]);
        if (isYouTubeUrl(input)) window.openYouTubeUrl(input); else window.openFile(QDir::cleanPath(input));
    }
    return app.exec();
}
