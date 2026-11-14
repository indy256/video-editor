#include "editor.h"
#include "timeline.h"
#include <QAudioOutput>
#include <QCloseEvent>
#include <QDir>
#include <QSignalBlocker>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMediaPlayer>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QShortcut>
#include <QSlider>
#include <QStandardPaths>
#include <QStyle>
#include <QTimeEdit>
#include <QVBoxLayout>
#include <QVideoWidget>
#include <QStackedLayout>
#include <climits>

EditorWindow::EditorWindow(QWidget *parent) : QMainWindow(parent), jobs_(this) {
    setWindowTitle("Video Editor"); resize(1120, 820); setMinimumSize(820, 680);
    auto *root = new QWidget; setCentralWidget(root);
    auto *layout = new QVBoxLayout(root); layout->setContentsMargins(24, 20, 24, 20); layout->setSpacing(14);
    auto *heading = new QHBoxLayout;
    auto *brand = new QLabel("VIDEO EDITOR"); brand->setObjectName("brand"); heading->addWidget(brand);
    heading->addStretch(); auto *tag = new QLabel("OPEN  /  SELECT  /  SAVE"); tag->setObjectName("muted"); heading->addWidget(tag); layout->addLayout(heading);
    auto *sourceRow = new QHBoxLayout;
    url_ = new QLineEdit; url_->setPlaceholderText("Paste a YouTube video URL..."); url_->setClearButtonEnabled(true); url_->setAccessibleName("YouTube URL");
    open_ = new QPushButton("Open YouTube"); local_ = new QPushButton("Open file...");
    loadMode_ = new QComboBox; loadMode_->addItems({"Stream on demand", "Download first"});
    loadMode_->setAccessibleName("YouTube loading mode");
    loadMode_->setToolTip("Streaming starts without a full download. Preview resolution depends on combined streams; saved clips use up to 1080p. Download first caches the full video for offline seeking.");
    sourceRow->addWidget(url_, 1); sourceRow->addWidget(loadMode_); sourceRow->addWidget(open_); sourceRow->addWidget(local_); layout->addLayout(sourceRow);
    title_ = new QLabel("Your next clip starts here"); title_->setObjectName("title"); title_->setTextFormat(Qt::PlainText); layout->addWidget(title_);
    auto *stage = new QWidget; stage->setObjectName("stage"); stage->setMinimumHeight(220);
    auto *stack = new QStackedLayout(stage);
    empty_ = new QLabel("Open a video to begin\n\nYouTube links and local video files"); empty_->setAlignment(Qt::AlignCenter); empty_->setObjectName("empty");
    auto *video = new QVideoWidget; video->setAspectRatioMode(Qt::KeepAspectRatio); stack->addWidget(empty_); stack->addWidget(video); layout->addWidget(stage, 1);
    player_ = new QMediaPlayer(this); audio_ = new QAudioOutput(this); audio_->setVolume(0.7f);
    player_->setAudioOutput(audio_); player_->setVideoOutput(video);
    auto *transport = new QHBoxLayout;
    play_ = new QPushButton("Play"); play_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay)); play_->setEnabled(false);
    auto *back = new QPushButton("-5s"), *forward = new QPushButton("+5s");
    clock_ = new QLabel("00:00:00.000 / 00:00:00.000"); clock_->setMinimumWidth(245);
    auto *volume = new QSlider(Qt::Horizontal); volume->setRange(0, 100); volume->setValue(70); volume->setMaximumWidth(100); volume->setAccessibleName("Volume");
    transport->addWidget(play_); transport->addWidget(back); transport->addWidget(forward); transport->addWidget(clock_); transport->addStretch(); transport->addWidget(new QLabel("Volume")); transport->addWidget(volume); layout->addLayout(transport);
    seek_ = new QSlider(Qt::Horizontal); seek_->setRange(0, 0); seek_->setAccessibleName("Playback position"); layout->addWidget(seek_);
    auto *selectionHeader = new QHBoxLayout;
    auto *label = new QLabel("SELECT YOUR CLIP"); label->setObjectName("brand"); selectionHeader->addWidget(label); selectionHeader->addStretch();
    auto *hint = new QLabel("Left-click: IN    /    Right-click: OUT    /    Shift-click: seek"); hint->setObjectName("muted"); selectionHeader->addWidget(hint); layout->addLayout(selectionHeader);
    timeline_ = new Timeline; layout->addWidget(timeline_);
    auto *clipRow = new QHBoxLayout;
    in_ = new QTimeEdit(QTime(0, 0)); out_ = new QTimeEdit(QTime(0, 0));
    for (auto *edit : {in_, out_}) { edit->setDisplayFormat("HH:mm:ss.zzz"); edit->setEnabled(false); }
    in_->setAccessibleName("Selection start"); out_->setAccessibleName("Selection end");
    clipRow->addWidget(new QLabel("IN")); clipRow->addWidget(in_); clipRow->addSpacing(12); clipRow->addWidget(new QLabel("OUT")); clipRow->addWidget(out_);
    selection_ = new QLabel; clipRow->addWidget(selection_); clipRow->addStretch();
    preview_ = new QPushButton("Preview selection"); save_ = new QPushButton("Save selection"); save_->setObjectName("primary"); save_->setEnabled(false); preview_->setEnabled(false);
    clipRow->addWidget(preview_); clipRow->addWidget(save_); layout->addLayout(clipRow);
    auto *exportRow = new QHBoxLayout;
    exportMode_ = new QComboBox; exportMode_->setAccessibleName("Export mode");
    exportMode_->addItems({"Precise MP4 (re-encode)", "Original streams (no re-encoding)"});
    exportRow->addWidget(new QLabel("Save as")); exportRow->addWidget(exportMode_);
    auto *exportHint = new QLabel("MP4 with accurate selection boundaries"); exportHint->setObjectName("muted");
    exportRow->addWidget(exportHint); exportRow->addStretch(); layout->addLayout(exportRow);
    connect(exportMode_, &QComboBox::currentIndexChanged, this, [exportHint](int index) {
        exportHint->setText(index == 0 ? "MP4 with accurate selection boundaries" : "MKV; copies video and audio. Starts at the preceding keyframe.");
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
    connect(local_, &QPushButton::clicked, this, &EditorWindow::chooseFile);
    connect(play_, &QPushButton::clicked, this, &EditorWindow::togglePlayback);
    connect(save_, &QPushButton::clicked, this, &EditorWindow::saveSelection);
    connect(cancel_, &QPushButton::clicked, &jobs_, &MediaJobs::cancel);
    connect(volume, &QSlider::valueChanged, this, [this](int v) { audio_->setVolume(v / 100.f); });
    connect(back, &QPushButton::clicked, this, [this] { player_->setPosition(qMax(qint64(0), player_->position() - 5000)); });
    connect(forward, &QPushButton::clicked, this, [this] { player_->setPosition(qMin(player_->duration(), player_->position() + 5000)); });
    connect(seek_, &QSlider::sliderMoved, this, [this](int pos) { previewing_ = false; player_->setPosition(qint64(double(pos) / 1000000 * player_->duration())); });
    connect(timeline_, &Timeline::seekRequested, player_, &QMediaPlayer::setPosition);
    connect(timeline_, &Timeline::selectionChanged, this, &EditorWindow::refreshSelection);
    connect(in_, &QTimeEdit::timeChanged, this, [this](QTime t) { timeline_->setSelection(QTime(0, 0).msecsTo(t), timeline_->end()); });
    connect(out_, &QTimeEdit::timeChanged, this, [this](QTime t) { timeline_->setSelection(timeline_->start(), QTime(0, 0).msecsTo(t)); });
    connect(preview_, &QPushButton::clicked, this, [this] { previewing_ = true; player_->setPosition(timeline_->start()); player_->play(); });
    connect(player_, &QMediaPlayer::durationChanged, this, [this](qint64 duration) {
        timeline_->setDuration(duration); seek_->setRange(0, duration ? 1000000 : 0);
        // QTime editors cover up to 24h; the timeline itself supports longer media.
        in_->setEnabled(duration > 0 && duration < 86400000); out_->setEnabled(in_->isEnabled());
    });
    connect(player_, &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
        timeline_->setPosition(pos);
        if (!seek_->isSliderDown()) seek_->setValue(player_->duration() ? int(double(pos) / player_->duration() * 1000000) : 0);
        clock_->setText(timeText(pos) + " / " + timeText(player_->duration()));
        if (previewing_ && pos >= timeline_->end()) { previewing_ = false; player_->pause(); player_->setPosition(timeline_->end()); }
    });
    connect(player_, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        bool playing = state == QMediaPlayer::PlayingState; play_->setText(playing ? "Pause" : "Play");
        play_->setIcon(style()->standardIcon(playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    });
    connect(player_, &QMediaPlayer::mediaStatusChanged, this, [this, stack, video](QMediaPlayer::MediaStatus state) {
        if (state == QMediaPlayer::LoadedMedia || state == QMediaPlayer::BufferedMedia) { stack->setCurrentWidget(video); play_->setEnabled(true); refreshSelection(); }
    });
    connect(player_, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &message) {
        play_->setEnabled(false); timeline_->setDuration(0); status_->setText("Playback failed: " + message + (youtubePage_.isEmpty() ? "" : " Reopen the YouTube link to refresh the stream, or choose Download first."));
    });
    connect(&jobs_, &MediaJobs::progress, this, [this](int percent, const QString &message) {
        setBusy(true); status_->setText(message); progress_->setRange(0, percent < 0 ? 0 : 100); if (percent >= 0) progress_->setValue(percent);
    });
    connect(&jobs_, &MediaJobs::downloaded, this, [this](const QString &path, const QString &title) { setBusy(false); openFile(path, title); });
    connect(&jobs_, &MediaJobs::streamReady, this, [this](const QUrl &url, const QString &title, qint64 duration, int height) {
        setBusy(false); previewing_ = false; player_->stop();
        source_ = url.toString(QUrl::FullyEncoded); youtubePage_ = url_->text().trimmed();
        title_->setText(title); title_->setToolTip(title); timeline_->setDuration(duration);
        player_->setSource(url); player_->play(); refreshSelection();
        status_->setText(QString("Streaming preview: %1p. Seek anywhere; Save selection fetches the interval at up to 1080p.").arg(height));
    });
    connect(&jobs_, &MediaJobs::exported, this, [this](const QString &path) {
        setBusy(false);
        status_->setText("Saved: " + QDir::toNativeSeparators(path) + (jobs_.copiesOriginalStreams() ? " | Original streams; starts at " + timeText(jobs_.actualExportStart()) : ""));
    });
    connect(&jobs_, &MediaJobs::failed, this, [this](const QString &message) { setBusy(false); status_->setText("Operation failed. See details."); QMessageBox::warning(this, "Video Editor", message); });
    connect(&jobs_, &MediaJobs::canceled, this, [this] { setBusy(false); status_->setText("Canceled."); });
    auto *space = new QShortcut(QKeySequence(Qt::Key_Space), this); connect(space, &QShortcut::activated, this, [this] { if (!url_->hasFocus() && !in_->hasFocus() && !out_->hasFocus()) togglePlayback(); });
    auto *openShortcut = new QShortcut(QKeySequence::Open, this); connect(openShortcut, &QShortcut::activated, this, &EditorWindow::chooseFile);
    auto *saveShortcut = new QShortcut(QKeySequence::Save, this); connect(saveShortcut, &QShortcut::activated, this, &EditorWindow::saveSelection);
    refreshSelection();
}
void EditorWindow::togglePlayback() {
    if (source_.isEmpty() || !play_->isEnabled()) return;
    previewing_ = false;
    if (player_->isPlaying()) player_->pause(); else player_->play();
}
void EditorWindow::chooseFile() {
    if (busy_) return;
    QString path = QFileDialog::getOpenFileName(this, "Open video", {}, "Videos (*.mp4 *.mkv *.webm *.mov *.avi *.m4v *.ts);;All files (*)");
    if (!path.isEmpty()) openFile(path);
}
void EditorWindow::openFile(const QString &path, const QString &title) {
    if (busy_) return;
    if (!QFileInfo(path).isFile()) { status_->setText("File does not exist: " + path); return; }
    previewing_ = false; youtubePage_.clear(); player_->stop(); timeline_->setDuration(0); source_ = QFileInfo(path).absoluteFilePath();
    title_->setText(title.isEmpty() ? QFileInfo(path).fileName() : title); title_->setToolTip(title_->text());
    player_->setSource(QUrl::fromLocalFile(source_)); player_->play(); status_->setText("Left-click the timeline for IN, right-click for OUT. Choose an export mode and Save selection.");
}
void EditorWindow::openYouTube() {
    if (busy_) return;
    player_->pause(); previewing_ = false;
    if (loadMode_->currentIndex() == 0) jobs_.resolveStream(url_->text()); else jobs_.download(url_->text());
}
void EditorWindow::openYouTubeUrl(const QString &url) { if (!busy_) { url_->setText(url); openYouTube(); } }
void EditorWindow::refreshSelection() {
    qint64 start = timeline_->start(), end = timeline_->end();
    { QSignalBlocker a(in_), b(out_); in_->setTime(QTime::fromMSecsSinceStartOfDay(int(start % 86400000))); out_->setTime(QTime::fromMSecsSinceStartOfDay(int(end % 86400000))); }
    bool valid = end > start && timeline_->duration() > 0;
    selection_->setText(valid ? timeText(end - start) + " selected" : "OUT must be after IN");
    save_->setEnabled(valid && !busy_ && !source_.isEmpty()); preview_->setEnabled(valid && !busy_);
}
void EditorWindow::setBusy(bool busy) {
    busy_ = busy; open_->setEnabled(!busy); local_->setEnabled(!busy); url_->setEnabled(!busy); loadMode_->setEnabled(!busy);
    exportMode_->setEnabled(!busy);
    progress_->setVisible(busy); cancel_->setVisible(busy); refreshSelection();
}
void EditorWindow::saveSelection() {
    if (!save_->isEnabled() || busy_) return;
    QString base = youtubePage_.isEmpty() ? QFileInfo(source_).completeBaseName() : "youtube";
    const auto mode = exportMode_->currentIndex() == 0 ? ExportMode::PreciseMp4 : ExportMode::OriginalStreams;
    const QString extension = mode == ExportMode::OriginalStreams ? ".mkv" : ".mp4";
    QString path = QFileDialog::getSaveFileName(this, "Save selection", QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/" + base + "-clip" + extension,
        mode == ExportMode::OriginalStreams ? "Matroska video (*.mkv)" : "MP4 video (*.mp4)");
    if (path.isEmpty()) return;
    if (!path.endsWith(extension, Qt::CaseInsensitive)) {
        path += extension;
        if (QFileInfo::exists(path) && QMessageBox::question(this, "Replace file?", "Replace " + path + "?") != QMessageBox::Yes) return;
    }
    if (youtubePage_.isEmpty()) jobs_.exportClip(source_, timeline_->start(), timeline_->end(), path, mode);
    else jobs_.exportYouTube(youtubePage_, timeline_->start(), timeline_->end(), path, mode);
}
void EditorWindow::closeEvent(QCloseEvent *event) {
    if (jobs_.busy()) {
        if (QMessageBox::question(this, "Operation in progress", "Cancel the operation and close?") != QMessageBox::Yes) { event->ignore(); return; }
        jobs_.cancel();
    }
    player_->stop(); player_->setSource(QUrl()); event->accept();
}
