#include "editor.h"
#include <QApplication>
#include <QDir>
int main(int argc, char **argv) {
    qputenv("QT_MEDIA_BACKEND", "ffmpeg");
    QApplication app(argc, argv);
    app.setApplicationName("VideoEditor"); app.setOrganizationName("VideoEditor");
    EditorWindow window; window.show();
    if (argc > 1) {
        const auto input = QString::fromLocal8Bit(argv[1]);
        if (isYouTubeUrl(input)) window.openYouTubeUrl(input); else window.openFile(QDir::cleanPath(input));
    }
    return app.exec();
}
