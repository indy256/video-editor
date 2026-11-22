#include "editor.h"
#include "timeline.h"
#include <QAudioOutput>
#include <QCloseEvent>
#include <QCryptographicHash>
#include <QDir>
#include <QSignalBlocker>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QMessageBox>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QShortcut>
#include <QSettings>
#include <QSlider>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleOptionSpinBox>
#include <QTimeEdit>
#include <QVBoxLayout>
#include <QVideoFrame>
#include <QVideoSink>
#include <QVideoWidget>
#include <QStackedLayout>
#include <algorithm>
#include <cmath>

namespace {
constexpr int seekSliderMaximum = 1'000'000;
constexpr qint64 millisecondsPerDay = 86'400'000;

QString selectionSettingsKey(const QString &kind, const QString &identity) {
    const auto hash = QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex();
    return "selections/" + kind + '/' + QString::fromLatin1(hash);
}

QString youtubeSelectionKey(const QString &videoId, const QString &page) {
    return selectionSettingsKey("youtube", videoId.isEmpty() ? page.trimmed() : videoId);
}

class FrameTimeEdit final : public QTimeEdit {
public:
    explicit FrameTimeEdit(QMediaPlayer &player) : QTimeEdit(QTime(0, 0)), player_(player) {
        setToolTip("Up and Down move the marker by one frame");
        connect(&player_, &QMediaPlayer::sourceChanged, this, [this] { preciseTime_ = -1; });
    }

    void stepBy(int steps) override {
        const qreal interval = frameInterval();
        if (!(interval > 0) || !std::isfinite(interval)) return;

        const int displayedTime = time().msecsSinceStartOfDay();
        if (qRound(preciseTime_) != displayedTime) preciseTime_ = displayedTime;
        preciseTime_ = std::clamp(preciseTime_ + steps * interval, qreal(0), qreal(lastMillisecond()));
        setTime(QTime::fromMSecsSinceStartOfDay(qRound(preciseTime_)));
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        QTimeEdit::paintEvent(event);

        QStyleOptionSpinBox option;
        initStyleOption(&option);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        for (auto control : {QStyle::SC_SpinBoxUp, QStyle::SC_SpinBoxDown}) {
            const bool up = control == QStyle::SC_SpinBoxUp;
            const auto step = up ? StepUpEnabled : StepDownEnabled;
            const bool enabled = isEnabled() && option.stepEnabled.testFlag(step);
            const QPointF center = style()->subControlRect(QStyle::CC_SpinBox, &option, control, this).center();
            const qreal rise = up ? 2 : -2;
            const QPointF arrow[] = {center + QPointF(-4, rise), center - QPointF(0, rise), center + QPointF(4, rise)};
            painter.setPen(QPen(QColor(enabled ? "#e7ebf3" : "#697386"), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPolyline(arrow, 3);
        }
    }

    StepEnabled stepEnabled() const override {
        if (isReadOnly() || !player_.hasVideo()) return StepNone;
        const int current = time().msecsSinceStartOfDay();
        StepEnabled enabled = StepNone;
        if (current < lastMillisecond()) enabled |= StepUpEnabled;
        if (current > 0) enabled |= StepDownEnabled;
        return enabled;
    }

private:
    int lastMillisecond() const {
        return int(qMin(player_.duration(), qint64(maximumTime().msecsSinceStartOfDay())));
    }

    qreal frameInterval() const {
        const qreal fps = player_.metaData().value(QMediaMetaData::VideoFrameRate).toReal();
        if (std::isfinite(fps) && fps > 0) return 1000.0 / fps;

        if (const auto *sink = player_.videoSink()) {
            const QVideoFrame frame = sink->videoFrame();
            if (frame.isValid() && frame.startTime() >= 0 && frame.endTime() > frame.startTime())
                return (frame.endTime() - frame.startTime()) / 1000.0;
        }
        return 0;
    }

    QMediaPlayer &player_;
    qreal preciseTime_ = -1;
};
}

EditorWindow::EditorWindow(QWidget *parent) : QMainWindow(parent), jobs_(this) {
    QSettings settings;
    setWindowTitle("Video Editor"); resize(1120, 820); setMinimumSize(820, 680);
    auto *root = new QWidget; setCentralWidget(root);
    auto *layout = new QVBoxLayout(root); layout->setContentsMargins(24, 20, 24, 20); layout->setSpacing(14);
    auto *heading = new QHBoxLayout;
    auto *brand = new QLabel("VIDEO EDITOR"); brand->setObjectName("brand"); heading->addWidget(brand);
    heading->addStretch(); auto *tag = new QLabel("OPEN  /  SELECT  /  SAVE"); tag->setObjectName("muted"); heading->addWidget(tag); layout->addLayout(heading);
    auto *sourceRow = new QHBoxLayout;
    url_ = new QLineEdit; url_->setPlaceholderText("Paste a YouTube video URL..."); url_->setClearButtonEnabled(true); url_->setAccessibleName("YouTube URL");
    url_->setText(settings.value("youtube/url").toString());
    open_ = new QPushButton("Open YouTube"); local_ = new QPushButton("Open file...");
    loadMode_ = new QComboBox; loadMode_->addItems({"Stream on demand", "Download first"});
    loadMode_->setCurrentIndex(settings.value("youtube/downloadFirst", false).toBool() ? 1 : 0);
    loadMode_->setAccessibleName("YouTube loading mode");
    loadMode_->setToolTip("Streaming starts without a full download. Preview resolution depends on combined streams; saved clips use up to 1080p. Download first caches the full video for offline seeking.");
    sourceRow->addWidget(url_, 1); sourceRow->addWidget(loadMode_); sourceRow->addWidget(open_); sourceRow->addWidget(local_); layout->addLayout(sourceRow);
    title_ = new QLabel("Your next clip starts here"); title_->setObjectName("title"); title_->setTextFormat(Qt::PlainText); layout->addWidget(title_);
    auto *stage = new QWidget; stage->setObjectName("stage"); stage->setMinimumHeight(220);
    auto *stack = new QStackedLayout(stage);
    auto *empty = new QLabel("Open a video to begin\n\nYouTube links and local video files"); empty->setAlignment(Qt::AlignCenter); empty->setObjectName("empty");
    auto *video = new QVideoWidget; video->setAspectRatioMode(Qt::KeepAspectRatio); stack->addWidget(empty); stack->addWidget(video); layout->addWidget(stage, 1);
    player_ = new QMediaPlayer(this);
    auto *audio = new QAudioOutput(this);
    player_->setAudioOutput(audio); player_->setVideoOutput(video);
    auto *transport = new QHBoxLayout;
    play_ = new QPushButton("Play"); play_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay)); play_->setEnabled(false);
    clock_ = new QLabel("00:00:00.000 / 00:00:00.000"); clock_->setMinimumWidth(245);
    auto *volume = new QSlider(Qt::Horizontal); volume->setRange(0, 100); volume->setMaximumWidth(100); volume->setAccessibleName("Volume");
    bool volumeValid = false;
    const int savedVolume = settings.value("playback/volume").toInt(&volumeValid);
    volume->setValue(volumeValid ? savedVolume : 70);
    audio->setVolume(volume->value() / 100.f);
    transport->addWidget(play_); transport->addWidget(clock_); transport->addStretch(); transport->addWidget(new QLabel("Volume")); transport->addWidget(volume); layout->addLayout(transport);
    seek_ = new QSlider(Qt::Horizontal); seek_->setRange(0, 0); seek_->setAccessibleName("Playback position"); layout->addWidget(seek_);
    auto *selectionHeader = new QHBoxLayout;
    auto *label = new QLabel("SELECT YOUR CLIP"); label->setObjectName("brand"); selectionHeader->addWidget(label); selectionHeader->addStretch();
    auto *hint = new QLabel("Click: move closest marker    /    Drag handles    /    Shift-click: seek"); hint->setObjectName("muted"); selectionHeader->addWidget(hint); layout->addLayout(selectionHeader);
    timeline_ = new Timeline; timeline_->setFocusPolicy(Qt::ClickFocus); layout->addWidget(timeline_);
    auto *clipRow = new QHBoxLayout;
    in_ = new FrameTimeEdit(*player_); out_ = new FrameTimeEdit(*player_);
    for (auto *edit : {in_, out_}) { edit->setDisplayFormat("HH:mm:ss.zzz"); edit->setEnabled(false); }
    in_->setAccessibleName("Selection start"); out_->setAccessibleName("Selection end");
    clipRow->addWidget(new QLabel("IN")); clipRow->addWidget(in_); clipRow->addSpacing(12); clipRow->addWidget(new QLabel("OUT")); clipRow->addWidget(out_);
    selection_ = new QLabel; clipRow->addWidget(selection_); clipRow->addStretch();
    preview_ = new QPushButton("Preview selection"); save_ = new QPushButton("Save selection"); save_->setObjectName("primary"); save_->setEnabled(false); preview_->setEnabled(false);
    clipRow->addWidget(preview_); clipRow->addWidget(save_); layout->addLayout(clipRow);
    auto *exportRow = new QHBoxLayout;
    exportMode_ = new QComboBox; exportMode_->setAccessibleName("Export mode");
    exportMode_->addItem("H.264 (MP4)", int(ExportMode::H264Mp4));
    exportMode_->addItem("H.265 (MP4, slow)", int(ExportMode::H265Mp4));
    exportMode_->addItem("Original streams (no re-encoding)", int(ExportMode::OriginalStreams));
    exportRow->addWidget(new QLabel("Save as")); exportRow->addWidget(exportMode_);
    auto *exportHint = new QLabel("MP4 with accurate selection boundaries"); exportHint->setObjectName("muted");
    exportRow->addWidget(exportHint); exportRow->addStretch(); layout->addLayout(exportRow);
    connect(exportMode_, &QComboBox::currentIndexChanged, this, [this, exportHint] {
        const bool copy = exportMode_->currentData().toInt() == int(ExportMode::OriginalStreams);
        exportHint->setText(copy ? "MKV; copies video and audio. Starts at the preceding keyframe." : "MP4 with accurate selection boundaries");
    });
    const auto previousMode = settings.value("export/originalStreams", false).toBool() ? ExportMode::OriginalStreams : ExportMode::H264Mp4;
    constexpr int retiredH265VerySlowMode = 3;
    const int savedMode = settings.value("export/mode", int(previousMode)).toInt();
    const int savedIndex = exportMode_->findData(savedMode == retiredH265VerySlowMode ? int(ExportMode::H265Mp4) : savedMode);
    exportMode_->setCurrentIndex(qMax(0, savedIndex));
    connect(exportMode_, &QComboBox::currentIndexChanged, this, [this] {
        QSettings().setValue("export/mode", exportMode_->currentData());
    });
    auto *statusRow = new QHBoxLayout;
    status_ = new QLabel("Ready. Open a YouTube link or a local video."); status_->setTextFormat(Qt::PlainText); status_->setWordWrap(true);
    progress_ = new QProgressBar; progress_->setFixedWidth(160); progress_->hide(); cancel_ = new QPushButton("Cancel"); cancel_->hide();
    statusRow->addWidget(status_, 1); statusRow->addWidget(progress_); statusRow->addWidget(cancel_); layout->addLayout(statusRow);
    setStyleSheet(R"(
        QMainWindow, QWidget { background: #141820; color: #e7ebf3; font-family: 'Segoe UI'; font-size: 13px; }
        QLabel#brand { color: #63e6c6; font-size: 12px; font-weight: 700; letter-spacing: 2px; }
        QLabel#title { font-size: 19px; font-weight: 600; }
        QLabel#muted { color: #929daf; font-size: 12px; }
        QWidget#stage, QLabel#empty { background: #090c11; border-radius: 8px; color: #8290a5; }
        QLineEdit, QTimeEdit, QComboBox { background: #1d2330; border: 1px solid #353e50; border-radius: 5px; padding: 9px; selection-background-color: #245b57; }
        QTimeEdit::up-button, QTimeEdit::down-button { subcontrol-origin: padding; width: 24px; background: #262e3e; border-left: 1px solid #3a455a; }
        QTimeEdit::up-button { subcontrol-position: top right; border-top-right-radius: 4px; }
        QTimeEdit::down-button { subcontrol-position: bottom right; border-top: 1px solid #3a455a; border-bottom-right-radius: 4px; }
        QTimeEdit::up-button:hover, QTimeEdit::down-button:hover { background: #354157; }
        QPushButton { background: #262e3e; border: 1px solid #3a455a; border-radius: 5px; padding: 9px 15px; font-weight: 600; }
        QPushButton:hover { background: #354157; } QPushButton:disabled { color: #697386; border-color: #2b3241; }
        QPushButton#primary { background: #63e6c6; color: #102a26; border: 0; } QPushButton#primary:hover { background: #93f4dc; }
        QPushButton#primary:disabled { background: #28463f; color: #81948e; }
        QSlider::groove:horizontal { background: #30394a; height: 5px; border-radius: 2px; }
        QSlider::handle:horizontal { background: #63e6c6; width: 12px; margin: -5px 0; border-radius: 6px; }
        QProgressBar { border: 1px solid #354157; border-radius: 4px; text-align: center; } QProgressBar::chunk { background: #245b57; }
    )");
    connect(open_, &QPushButton::clicked, this, &EditorWindow::openYouTube);
    connect(url_, &QLineEdit::returnPressed, this, &EditorWindow::openYouTube);
    connect(url_, &QLineEdit::textChanged, this, [](const QString &url) {
        QSettings().setValue("youtube/url", url);
    });
    connect(loadMode_, &QComboBox::currentIndexChanged, this, [](int index) {
        QSettings().setValue("youtube/downloadFirst", index == 1);
    });
    connect(local_, &QPushButton::clicked, this, &EditorWindow::chooseFile);
    connect(play_, &QPushButton::clicked, this, &EditorWindow::togglePlayback);
    connect(save_, &QPushButton::clicked, this, &EditorWindow::saveSelection);
    connect(cancel_, &QPushButton::clicked, &jobs_, &MediaJobs::cancel);
    connect(volume, &QSlider::valueChanged, audio, [audio](int value) {
        audio->setVolume(value / 100.f);
        QSettings().setValue("playback/volume", value);
    });
    connect(seek_, &QSlider::sliderMoved, this, [this](int pos) {
        player_->setPosition(qint64(double(pos) / seekSliderMaximum * player_->duration()));
    });
    connect(timeline_, &Timeline::seekRequested, player_, &QMediaPlayer::setPosition);
    connect(timeline_, &Timeline::selectionChanged, this, [this](qint64 start, qint64 end) {
        if (!selectionKey_.isEmpty() && timeline_->duration() > 0)
            QSettings().setValue(selectionKey_, QVariantList{start, end});
        refreshSelection();
    });
    connect(in_, &QTimeEdit::timeChanged, this, [this](QTime time) {
        timeline_->setSelection(time.msecsSinceStartOfDay(), timeline_->end());
        player_->setPosition(timeline_->start());
    });
    connect(out_, &QTimeEdit::timeChanged, this, [this](QTime time) {
        timeline_->setSelection(timeline_->start(), time.msecsSinceStartOfDay());
        player_->setPosition(timeline_->end());
    });
    connect(preview_, &QPushButton::clicked, this, [this] { player_->setPosition(timeline_->start()); player_->play(); });
    connect(player_, &QMediaPlayer::durationChanged, this, &EditorWindow::updateMediaDuration);
    connect(player_, &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
        const qint64 out = timeline_->end();
        const bool reachedOut = player_->isPlaying() && timeline_->position() < out && pos >= out;
        const qint64 duration = player_->duration();
        timeline_->setPosition(pos);
        if (!seek_->isSliderDown()) seek_->setValue(duration ? int(double(pos) / duration * seekSliderMaximum) : 0);
        clock_->setText(timeText(pos) + " / " + timeText(duration));
        if (reachedOut) {
            player_->pause();
            if (player_->position() != out) player_->setPosition(out);
        }
    });
    connect(player_, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        bool playing = state == QMediaPlayer::PlayingState; play_->setText(playing ? "Pause" : "Play");
        play_->setIcon(style()->standardIcon(playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    });
    connect(player_, &QMediaPlayer::mediaStatusChanged, this, [this, stack, video](QMediaPlayer::MediaStatus state) {
        if (state == QMediaPlayer::LoadedMedia || state == QMediaPlayer::BufferedMedia) {
            stack->setCurrentWidget(video);
            play_->setEnabled(true);
            updateSelectionActions();
        }
    });
    connect(player_, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &message) {
        play_->setEnabled(false); updateMediaDuration(0); status_->setText("Playback failed: " + message + (youtubePage_.isEmpty() ? "" : " Reopen the YouTube link to refresh the stream, or choose Download first."));
    });
    connect(&jobs_, &MediaJobs::progress, this, [this](int percent, const QString &message) {
        setBusy(true); status_->setText(message); progress_->setRange(0, percent < 0 ? 0 : 100); if (percent >= 0) progress_->setValue(percent);
    });
    connect(&jobs_, &MediaJobs::downloaded, this, [this](const QString &path, const QString &title, const QString &videoId) {
        setBusy(false);
        youtubePage_.clear();
        openMedia(QUrl::fromLocalFile(path), title, youtubeSelectionKey(videoId, url_->text()));
    });
    connect(&jobs_, &MediaJobs::streamReady, this, [this](const QUrl &url, const QString &title, qint64 duration, int height, const QString &videoId) {
        setBusy(false); youtubePage_ = url_->text().trimmed();
        openMedia(url, title, youtubeSelectionKey(videoId, youtubePage_), duration);
        status_->setText(QString("Streaming preview: %1p. Seek anywhere; Save selection fetches the interval at up to 1080p.").arg(height));
    });
    connect(&jobs_, &MediaJobs::exported, this, [this](const QString &path) {
        setBusy(false);
        status_->setText("Saved: " + QDir::toNativeSeparators(path) + (jobs_.copiesOriginalStreams() ? " | Original streams; starts at " + timeText(jobs_.actualExportStart()) : ""));
    });
    connect(&jobs_, &MediaJobs::failed, this, [this](const QString &message) { setBusy(false); status_->setText("Operation failed. See details."); QMessageBox::warning(this, "Video Editor", message); });
    connect(&jobs_, &MediaJobs::canceled, this, [this] { setBusy(false); status_->setText("Canceled."); });
    auto addPlaybackShortcut = [this](Qt::Key key, auto action) {
        auto *shortcut = new QShortcut(QKeySequence(key), this, this, action);
        shortcut->setAutoRepeat(false);
    };
    addPlaybackShortcut(Qt::Key_Space, [this] {
        if (!textFieldHasFocus()) togglePlayback();
    });
    addPlaybackShortcut(Qt::Key_Left, [this] { seekAndPlay(timeline_->start()); });
    addPlaybackShortcut(Qt::Key_Right, [this] {
        seekAndPlay(qMax(qint64(0), timeline_->end() - 1000));
    });
    addPlaybackShortcut(Qt::Key_Pause, &EditorWindow::togglePlayback);
    new QShortcut(QKeySequence::Open, this, this, &EditorWindow::chooseFile);
    new QShortcut(QKeySequence::Save, this, this, &EditorWindow::saveSelection);
    new QShortcut(QKeySequence(Qt::Key_Escape), this, this, &QWidget::close);
    refreshSelection();
    restoreGeometry(settings.value("window/geometry").toByteArray());
}
void EditorWindow::togglePlayback() {
    if (player_->source().isEmpty() || !play_->isEnabled()) return;
    if (player_->isPlaying()) player_->pause(); else player_->play();
}
void EditorWindow::seekAndPlay(qint64 position) {
    if (textFieldHasFocus()) return;
    if (player_->source().isEmpty() || !play_->isEnabled()) return;
    player_->setPosition(position);
    player_->play();
}
bool EditorWindow::textFieldHasFocus() const {
    return url_->hasFocus() || in_->hasFocus() || out_->hasFocus();
}
void EditorWindow::chooseFile() {
    if (busy_) return;
    QString path = QFileDialog::getOpenFileName(this, "Open video", {}, "Videos (*.mp4 *.mkv *.webm *.mov *.avi *.m4v *.ts);;All files (*)");
    if (!path.isEmpty()) openFile(path);
}
void EditorWindow::openFile(const QString &path) {
    if (busy_) return;
    const QFileInfo file(path);
    if (!file.isFile()) { status_->setText("File does not exist: " + path); return; }
    youtubePage_.clear();
    const QString canonicalPath = file.canonicalFilePath();
    const auto key = selectionSettingsKey("file", canonicalPath.isEmpty() ? file.absoluteFilePath() : canonicalPath);
    openMedia(QUrl::fromLocalFile(file.absoluteFilePath()), file.fileName(), key);
}
void EditorWindow::openMedia(const QUrl &source, const QString &title, const QString &selectionKey, qint64 duration) {
    if (source == player_->source() && player_->duration() > 0) duration = player_->duration();
    player_->stop();
    selectionKey_ = selectionKey;
    updateMediaDuration(duration);
    title_->setText(title); title_->setToolTip(title);
    status_->setText("Click the track or drag a handle to set IN and OUT. Choose an export mode and Save selection.");
    player_->setSource(source); player_->pause();
    updateSelectionActions();
}
void EditorWindow::updateMediaDuration(qint64 duration) {
    const QSignalBlocker blocker(timeline_);
    timeline_->setDuration(duration);
    if (duration > 0 && !selectionKey_.isEmpty()) {
        const auto saved = QSettings().value(selectionKey_).toList();
        if (saved.size() == 2) {
            bool startOk = false, endOk = false;
            const auto start = saved[0].toLongLong(&startOk);
            const auto end = saved[1].toLongLong(&endOk);
            if (startOk && endOk) timeline_->setSelection(start, end);
        }
    }
    timeline_->setPosition(player_->position());
    seek_->setRange(0, duration > 0 ? seekSliderMaximum : 0);
    // QTime editors cover up to 24h; the timeline itself supports longer media.
    const bool timeFieldsEnabled = duration > 0 && duration < millisecondsPerDay;
    in_->setEnabled(timeFieldsEnabled);
    out_->setEnabled(timeFieldsEnabled);
    refreshSelection();
}
void EditorWindow::openYouTube() {
    if (busy_) return;
    player_->pause();
    if (loadMode_->currentIndex() == 0) jobs_.resolveStream(url_->text()); else jobs_.download(url_->text());
}
void EditorWindow::openYouTubeUrl(const QString &url) { if (!busy_) { url_->setText(url); openYouTube(); } }
void EditorWindow::refreshSelection() {
    const qint64 start = timeline_->start(), end = timeline_->end();
    const QSignalBlocker blockIn(in_), blockOut(out_);
    in_->setTime(QTime::fromMSecsSinceStartOfDay(int(start % millisecondsPerDay)));
    out_->setTime(QTime::fromMSecsSinceStartOfDay(int(end % millisecondsPerDay)));
    selection_->setText(end > start ? timeText(end - start) + " selected" : "OUT must be after IN");
    updateSelectionActions();
}
void EditorWindow::updateSelectionActions() {
    const bool canPreview = !busy_ && timeline_->end() > timeline_->start();
    preview_->setEnabled(canPreview);
    save_->setEnabled(canPreview && !player_->source().isEmpty());
}
void EditorWindow::setBusy(bool busy) {
    if (busy_ == busy) return;
    busy_ = busy;
    QWidget *controls[] = {open_, local_, url_, loadMode_, exportMode_};
    for (auto *control : controls) control->setEnabled(!busy);
    progress_->setVisible(busy);
    cancel_->setVisible(busy);
    updateSelectionActions();
}
void EditorWindow::saveSelection() {
    if (!save_->isEnabled()) return;
    const auto mode = static_cast<ExportMode>(exportMode_->currentData().toInt());
    const bool copy = mode == ExportMode::OriginalStreams;
    const QString extension = copy ? ".mkv" : ".mp4";
    QSettings settings;
    const QString movies = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    QString directory = settings.value("export/directory", movies).toString();
    if (!QDir(directory).exists()) directory = movies;
    QString path = QFileDialog::getSaveFileName(this, "Save selection", QDir(directory).filePath("v" + extension),
        copy ? "Matroska video (*.mkv)" : "MP4 video (*.mp4)");
    if (path.isEmpty()) return;
    if (!path.endsWith(extension, Qt::CaseInsensitive)) {
        path += extension;
        if (QFileInfo::exists(path) && QMessageBox::question(this, "Replace file?", "Replace " + path + "?") != QMessageBox::Yes) return;
    }
    settings.setValue("export/directory", QFileInfo(path).absolutePath());
    const qint64 start = timeline_->start(), end = timeline_->end();
    if (youtubePage_.isEmpty()) jobs_.exportClip(player_->source().toLocalFile(), start, end, path, mode);
    else jobs_.exportYouTube(youtubePage_, start, end, path, mode);
}
void EditorWindow::closeEvent(QCloseEvent *event) {
    if (jobs_.busy()) {
        if (QMessageBox::question(this, "Operation in progress", "Cancel the operation and close?") != QMessageBox::Yes) { event->ignore(); return; }
        jobs_.cancel();
    }
    QSettings().setValue("window/geometry", saveGeometry());
    player_->stop(); player_->setSource(QUrl()); event->accept();
}
