#pragma once
#include <QMainWindow>
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
    void openYouTubeUrl(const QString &url);
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    void openYouTube();
    void openMedia(const QUrl &source, const QString &title, const QString &selectionKey, qint64 duration = 0);
    void updateMediaDuration(qint64 duration);
    void chooseFile();
    void saveSelection();
    void refreshSelection();
    void updateSelectionActions();
    void setBusy(bool busy);
    void togglePlayback();
    void seekAndPlay(qint64 position);
    bool textFieldHasFocus() const;
    MediaJobs jobs_;
    QMediaPlayer *player_;
    Timeline *timeline_;
    QLineEdit *url_;
    QComboBox *loadMode_, *exportMode_;
    QLabel *title_, *clock_, *selection_, *status_;
    QPushButton *open_, *local_, *save_, *play_, *cancel_, *preview_;
    QProgressBar *progress_;
    QSlider *seek_;
    QTimeEdit *in_, *out_;
    QString youtubePage_, selectionKey_;
    bool busy_ = false;
};
