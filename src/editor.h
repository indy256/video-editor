#pragma once
#include <QMainWindow>
#include "jobs.h"
class QMediaPlayer;
class QAudioOutput;
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
    void openFile(const QString &path, const QString &title = {});
    void openYouTubeUrl(const QString &url);
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    void openYouTube();
    void chooseFile();
    void saveSelection();
    void refreshSelection();
    void setBusy(bool busy);
    void togglePlayback();
    MediaJobs jobs_;
    QMediaPlayer *player_;
    QAudioOutput *audio_;
    Timeline *timeline_;
    QLineEdit *url_;
    QComboBox *loadMode_, *exportMode_;
    QLabel *title_, *clock_, *selection_, *status_, *empty_;
    QPushButton *open_, *local_, *save_, *play_, *cancel_, *preview_;
    QProgressBar *progress_;
    QSlider *seek_;
    QTimeEdit *in_, *out_;
    QString source_;
    QString youtubePage_;
    bool busy_ = false, previewing_ = false;
};
