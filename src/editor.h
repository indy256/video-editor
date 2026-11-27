#pragma once
#include <QMainWindow>
#include <optional>
#include "jobs.h"
class QMediaPlayer;
class QLineEdit;
class QLabel;
class QPushButton;
class QSlider;
class QProgressBar;
class QTimeEdit;
class Timeline;
class QComboBox;

class EditorWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit EditorWindow(QWidget *parent = nullptr);
    void openFile(const QString &path);
    void openSource(const QString &source, std::optional<qint64> position = std::nullopt);
protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    bool isWindowBackground(const QPoint &point) const;
    void openYouTube();
    void openMedia(const QUrl &source, const QString &selectionKey, const QString &reopenSource, qint64 duration = 0);
    void updateToLatest();
    void updateMediaDuration(qint64 duration);
    void chooseFile();
    void saveSelection();
    void refreshSelection();
    void updateSelectionActions();
    void setBusy(bool busy);
    void togglePlayback();
    void seekTo(qint64 position);
    void applyPendingSeek();
    void seekAndPlay(qint64 position);
    bool playbackReady() const;
    MediaJobs jobs_;
    QMediaPlayer *player_;
    QWidget *previewArea_;
    Timeline *timeline_;
    QLineEdit *url_;
    QComboBox *loadMode_, *exportMode_;
    QLabel *clock_, *selection_, *status_;
    QPushButton *open_, *local_, *save_, *play_, *cancel_, *playSelection_;
    QProgressBar *progress_;
    QSlider *seek_, *volume_;
    QTimeEdit *in_, *out_;
    QString youtubePage_, selectionKey_, reopenSource_;
    struct WindowDrag {
        QObject *surface = nullptr;
        QPoint pressPosition;
        QPoint windowPosition;
        bool moving = false;
    };
    WindowDrag windowDrag_;
    std::optional<qint64> pendingSeek_;
    std::optional<qint64> reopenPosition_;
    bool seeking_ = false;
    bool busy_ = false;
    bool updating_ = false;
};
