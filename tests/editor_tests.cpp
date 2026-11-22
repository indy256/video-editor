#include "timeline.h"
#include "jobs.h"
#include "editor.h"
#include <QtTest>
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QVideoSink>
#include <QVideoFrame>
#include <QTemporaryDir>
#include <QFile>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QSettings>
#include <QSlider>
#include <QStyleOptionSpinBox>
#include <QTimeEdit>
#include <QTimer>
#include <QVideoWidget>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QTcpSocket>
#include <QApplication>
#include <QScopeGuard>
#include <cstdio>

class EditorTests : public QObject {
    Q_OBJECT
    QTemporaryDir temp;
    QByteArray previousCacheDirectory;
    QString source;
    template <typename Widget>
    static Widget *findControl(EditorWindow &window, const QString &name) {
        for (auto *widget : window.findChildren<Widget *>())
            if (widget->accessibleName() == name) return widget;
        return nullptr;
    }
    double frameColorError(const QVideoFrame &frame, const QString &fixture) {
        const QImage actual = frame.toImage();
        if (actual.isNull()) return -1;
        const QString referencePath = temp.filePath("reference.png");
        QProcess ff;
        ff.start(findTool("ffmpeg"), {"-hide_banner", "-y", "-ss", QString::number(frame.startTime() / 1000000.0, 'f', 6), "-i", fixture, "-frames:v", "1", referencePath});
        if (!ff.waitForFinished(15000) || ff.exitCode()) return -1;
        const QImage expected(referencePath);
        if (expected.isNull()) return -1;
        auto a = actual.scaled(160, 90, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
        auto b = expected.scaled(160, 90, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
        double error = 0;
        for (int y = 0; y < 90; ++y) for (int x = 0; x < 160; ++x) {
            const QColor c = a.pixelColor(x, y), d = b.pixelColor(x, y);
            error += qAbs(c.red() - d.red()) + qAbs(c.green() - d.green()) + qAbs(c.blue() - d.blue());
        }
        return error / (160 * 90 * 3);
    }
private slots:
    void initTestCase() {
        QVERIFY(temp.isValid()); QVERIFY(!findTool("ffmpeg").isEmpty());
        QCoreApplication::setOrganizationName("VideoEditorTests");
        QCoreApplication::setApplicationName("VideoEditorTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.filePath("settings"));
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, temp.filePath("system-settings"));
        previousCacheDirectory = qgetenv("VIDEO_EDITOR_CACHE_DIR");
        qputenv("VIDEO_EDITOR_CACHE_DIR", temp.filePath("stream-cache").toUtf8());
        source = temp.filePath("test source.mp4");
        QProcess ff;
        ff.start(findTool("ffmpeg"), {"-hide_banner", "-y", "-f", "lavfi", "-i", "testsrc2=size=320x180:rate=30", "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000", "-t", "5", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac", source});
        QVERIFY(ff.waitForFinished(30000)); QCOMPARE(ff.exitCode(), 0);
    }
    void urlValidation() {
        QVERIFY(isYouTubeUrl("https://www.youtube.com/watch?v=test"));
        QVERIFY(isYouTubeUrl("https://youtu.be/test"));
        QVERIFY(!isYouTubeUrl("https://youtube.com.evil.test/video"));
        QVERIFY(!isYouTubeUrl("file:///tmp/video"));
        QVERIFY(!isYouTubeUrl("--exec something"));
    }
    void timelineMouseSelection() {
        Timeline timeline; timeline.resize(832, 108); timeline.show(); timeline.setDuration(10000);
        QVERIFY(!timeline.testAttribute(Qt::WA_SetCursor));
        QSignalSpy seek(&timeline, &Timeline::seekRequested);
        const QPoint middle(416, 40);
        QTest::mouseClick(&timeline, Qt::RightButton, Qt::NoModifier, middle);
        QTest::mouseClick(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(24, 14));
        QCOMPARE(timeline.start(), 0);
        QCOMPARE(timeline.end(), 10000);
        QVERIFY(seek.isEmpty());

        QTest::mousePress(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(24, 14));
        QTest::mouseMove(&timeline, QPoint(184, 14));
        QTRY_COMPARE(timeline.start(), 2000);
        QVERIFY(!timeline.testAttribute(Qt::WA_SetCursor));
        QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(184, 14));
        QCOMPARE(timeline.start(), 2000);
        auto drag = [&](QPoint from, QPoint to) {
            QTest::mousePress(&timeline, Qt::LeftButton, Qt::NoModifier, from);
            QTest::mouseMove(&timeline, to);
            QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::NoModifier, to);
        };
        drag(QPoint(816, 66), QPoint(656, 66));
        QCOMPARE(timeline.end(), 8000); QCOMPARE(seek.count(), 2);
        QTest::mouseClick(&timeline, Qt::LeftButton, Qt::ShiftModifier, middle);
        QCOMPARE(timeline.start(), 2000); QCOMPARE(timeline.end(), 8000);
        QCOMPARE(seek.count(), 3); QCOMPARE(seek.last().first().toLongLong(), 5000);

        drag(QPoint(176, 14), QPoint(656, 14));
        QCOMPARE(timeline.start(), timeline.end());
        drag(QPoint(656, 66), QPoint(576, 66));
        QCOMPARE(timeline.start(), 8000);
        QCOMPARE(timeline.end(), 7000);
        timeline.setDuration(5000); QCOMPARE(timeline.start(), 0); QCOMPARE(timeline.end(), 5000);

        seek.clear();
        QTest::mouseClick(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(216, 40));
        QCOMPARE(timeline.start(), 1250);
        QCOMPARE(timeline.end(), 5000);
        QCOMPARE(seek.count(), 1);
        QCOMPARE(seek.last().first().toLongLong(), 1250);
        QTest::mouseClick(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(616, 40));
        QCOMPARE(timeline.start(), 1250);
        QCOMPARE(timeline.end(), 3750);
        QCOMPARE(seek.count(), 2);
        QCOMPARE(seek.last().first().toLongLong(), 3750);
        QTest::mouseClick(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(218, 40));
        QCOMPARE(timeline.start(), 1263);
        QCOMPARE(timeline.end(), 3750);

        seek.clear();
        QTest::mouseClick(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(616, 40));
        QCOMPARE(timeline.end(), 3750);
        QCOMPARE(seek.count(), 1);
        QCOMPARE(seek.last().first().toLongLong(), 3750);

        seek.clear();
        QTest::mousePress(&timeline, Qt::LeftButton, Qt::ShiftModifier, middle);
        QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::ShiftModifier, QPoint(656, 40));
        QCOMPARE(seek.count(), 2);
        QCOMPARE(seek.last().first().toLongLong(), 4000);
        QCOMPARE(timeline.start(), 1263);
        QCOMPARE(timeline.end(), 3750);

        // The player can refine a stream's initial duration during the drag.
        QTest::mousePress(&timeline, Qt::LeftButton, Qt::ShiftModifier, middle);
        timeline.setDuration(10000);
        QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::ShiftModifier, QPoint(656, 40));
        QCOMPARE(seek.last().first().toLongLong(), 8000);

        timeline.setPosition(8000);
        QTest::mouseClick(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(24, 14));
        QCOMPARE(seek.last().first().toLongLong(), timeline.start());
        QTest::mouseClick(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(816, 66));
        QCOMPARE(seek.last().first().toLongLong(), timeline.end());
    }
    void playbackAndSeek() {
        QMediaPlayer player; QVideoSink sink; QAudioOutput audio; audio.setMuted(true);
        player.setVideoSink(&sink); player.setAudioOutput(&audio);
        QSignalSpy frames(&sink, &QVideoSink::videoFrameChanged);
        player.setSource(QUrl::fromLocalFile(source)); player.play();
        QTRY_VERIFY_WITH_TIMEOUT(frames.count() > 2, 15000);
        QVERIFY(player.duration() >= 4900);
        player.pause(); player.setPosition(3000);
        QTRY_VERIFY_WITH_TIMEOUT(sink.videoFrame().startTime() >= 2900000, 10000);
        QCOMPARE(player.playbackState(), QMediaPlayer::PausedState);
        player.play(); QTRY_VERIFY_WITH_TIMEOUT(player.position() > 3300, 5000);
    }
    void exactExportAndOverwrite_data() {
        QTest::addColumn<int>("modeValue");
        QTest::newRow("h264") << int(ExportMode::H264Mp4);
        QTest::newRow("h265-slow") << int(ExportMode::H265Mp4);
    }
    void exactExportAndOverwrite() {
        QFETCH(int, modeValue);
        const auto mode = static_cast<ExportMode>(modeValue);
        const bool hevc = mode != ExportMode::H264Mp4;
        MediaJobs jobs; QSignalSpy saved(&jobs, &MediaJobs::exported), failed(&jobs, &MediaJobs::failed);
        QString destination = temp.filePath(QString::fromLatin1(QTest::currentDataTag()) + " clip with spaces.mp4");
        QFile existing(destination); QVERIFY(existing.open(QIODevice::WriteOnly)); existing.write("old data"); existing.close();
        if (hevc) jobs.exportClip(source, 1250, 3500, destination, mode);
        else jobs.exportClip(source, 1250, 3500, destination);
        QTRY_VERIFY_WITH_TIMEOUT(saved.count() || failed.count(), 30000);
        if (failed.count()) qWarning() << failed.first().first();
        QCOMPARE(failed.count(), 0); QCOMPARE(saved.count(), 1); QVERIFY(!jobs.busy());
        QProcess probe;
        probe.start("ffprobe", {"-v", "error", "-show_entries", "format=duration:stream=codec_type,codec_name,codec_tag_string,profile,pix_fmt", "-of", "json", destination});
        QVERIFY(probe.waitForFinished(10000)); QCOMPARE(probe.exitCode(), 0);
        auto metadata = QJsonDocument::fromJson(probe.readAllStandardOutput()).object();
        double duration = metadata.value("format").toObject().value("duration").toString().toDouble();
        bool hasAudio = false, hasVideo = false;
        for (const auto &stream : metadata.value("streams").toArray()) {
            const auto info = stream.toObject();
            if (info.value("codec_type").toString() == "audio") {
                hasAudio = true;
                QCOMPARE(info.value("codec_name").toString(), QString("aac"));
            } else if (info.value("codec_type").toString() == "video") {
                hasVideo = true;
                QCOMPARE(info.value("codec_name").toString(), QString(hevc ? "hevc" : "h264"));
                QCOMPARE(info.value("profile").toString(), QString("Main"));
                QCOMPARE(info.value("codec_tag_string").toString(), QString(hevc ? "hvc1" : "avc1"));
                QCOMPARE(info.value("pix_fmt").toString(), QString("yuv420p"));
            }
        }
        QVERIFY(hasAudio); QVERIFY(hasVideo);
        QVERIFY2(qAbs(duration - 2.25) < 0.08, qPrintable(QString::number(duration)));
        QMediaPlayer player; QVideoSink sink; player.setVideoSink(&sink); player.setSource(QUrl::fromLocalFile(destination)); player.play();
        QTRY_VERIFY_WITH_TIMEOUT(sink.videoFrame().isValid(), 10000);
    }
    void invalidExportProtectsSource() {
        MediaJobs jobs; QSignalSpy errors(&jobs, &MediaJobs::failed);
        auto size = QFileInfo(source).size();
        jobs.exportClip(source, 1000, 2000, source); QCOMPARE(errors.count(), 1); QCOMPARE(QFileInfo(source).size(), size);
        jobs.exportClip(source, 2000, 1000, temp.filePath("invalid.mp4")); QCOMPARE(errors.count(), 2); QVERIFY(!QFile::exists(temp.filePath("invalid.mp4")));
    }
    void originalStreamExport_data() {
        QTest::addColumn<QString>("audioCodec");
        QTest::addColumn<bool>("mp4Supported");
        QTest::newRow("h264-aac") << QString("aac") << true;
        QTest::newRow("h264-flac") << QString("flac") << true;
        QTest::newRow("video-only") << QString() << true;
        QTest::newRow("unsupported-audio") << QString("wmav2") << false;
    }
    void originalStreamExport() {
        QFETCH(QString, audioCodec);
        QFETCH(bool, mp4Supported);
        const bool withAudio = !audioCodec.isEmpty();
        const QString original = temp.filePath("original-streams.mkv"), destination = temp.filePath("copied-streams.mp4");
        QProcess ff;
        QStringList args{"-v", "error", "-y", "-f", "lavfi", "-i", "testsrc2=size=320x180:rate=30"};
        if (withAudio) args += QStringList{"-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000"};
        args += QStringList{"-t", "6", "-c:v", "libx264", "-g", "60", "-keyint_min", "60", "-sc_threshold", "0", "-pix_fmt", "yuv420p"};
        if (withAudio) args += QStringList{"-c:a", audioCodec};
        args += original; ff.start(findTool("ffmpeg"), args); QVERIFY(ff.waitForFinished(30000)); QCOMPARE(ff.exitCode(), 0);
        QFile existing(destination); QVERIFY(existing.open(QIODevice::WriteOnly)); existing.write("replace only after successful copy"); existing.close();
        MediaJobs jobs; QSignalSpy saved(&jobs, &MediaJobs::exported), errors(&jobs, &MediaJobs::failed);
        jobs.exportClip(original, 3250, 4750, destination, ExportMode::OriginalStreams);
        QTRY_VERIFY_WITH_TIMEOUT(!saved.isEmpty() || !errors.isEmpty(), 30000);
        if (!mp4Supported) {
            QCOMPARE(saved.count(), 0); QCOMPARE(errors.count(), 1);
            QVERIFY(errors.first().first().toString().contains("Could not save the original streams as MP4"));
            QVERIFY(existing.open(QIODevice::ReadOnly));
            QCOMPARE(existing.readAll(), QByteArray("replace only after successful copy"));
            QVERIFY(QDir(temp.path()).entryList({".video-editor-*"}, QDir::Files | QDir::Hidden).isEmpty());
            return;
        }
        if (!errors.isEmpty()) qWarning() << errors.first().first();
        QCOMPARE(errors.count(), 0); QCOMPARE(saved.count(), 1); QVERIFY(jobs.copiesOriginalStreams());
        QVERIFY(jobs.actualExportStart() >= 1900 && jobs.actualExportStart() <= 2100);
        auto packets = [](const QString &path, const QString &track) {
            QProcess probe; probe.start(findTool("ffprobe"), {"-v", "error", "-select_streams", track, "-show_packets", "-show_data_hash", "sha256", "-show_entries", "packet=data_hash,flags,pts_time", "-of", "json", path});
            if (!probe.waitForFinished(15000) || probe.exitCode()) return QJsonArray();
            return QJsonDocument::fromJson(probe.readAllStandardOutput()).object().value("packets").toArray();
        };
        const auto video = packets(destination, "v:0"), audio = packets(destination, "a:0");
        for (const auto &track : {QString("v:0"), QString("a:0")}) {
            const bool videoTrack = track == "v:0";
            const auto &after = videoTrack ? video : audio;
            if (!videoTrack && !withAudio) { QVERIFY(after.isEmpty()); continue; }
            const auto before = packets(original, track);
            QVERIFY(!before.isEmpty()); QVERIFY(!after.isEmpty());
            QStringList beforeHashes, afterHashes;
            for (const auto &p : before) beforeHashes += p.toObject().value("data_hash").toString();
            for (const auto &p : after) afterHashes += p.toObject().value("data_hash").toString();
            const int first = beforeHashes.indexOf(afterHashes.first()); QVERIFY(first >= 0);
            QCOMPARE(afterHashes, beforeHashes.mid(first, afterHashes.size()));
            if (videoTrack) {
                QVERIFY(after.first().toObject().value("flags").toString().contains('K'));
                QVERIFY(qAbs(before[first].toObject().value("pts_time").toString().toDouble() - 2.0) < 0.1);
            }
        }
        if (withAudio) QVERIFY(qAbs(video.first().toObject().value("pts_time").toString().toDouble() - audio.first().toObject().value("pts_time").toString().toDouble()) < 0.15);
        QProcess probe; probe.start(findTool("ffprobe"), {"-v", "error", "-show_entries", "format=duration,format_name:stream=codec_name", "-of", "json", destination});
        QVERIFY(probe.waitForFinished(10000)); QCOMPARE(probe.exitCode(), 0);
        const auto info = QJsonDocument::fromJson(probe.readAllStandardOutput()).object();
        const auto format = info.value("format").toObject();
        QVERIFY(format.value("format_name").toString().split(',').contains("mp4"));
        const double duration = format.value("duration").toString().toDouble();
        // Packet DTS cutoffs and reordered frames can extend the last PTS.
        QVERIFY2(duration >= 2.65 && duration <= 3.05, qPrintable(QString::number(duration)));
        if (withAudio) QCOMPARE(info.value("streams").toArray().last().toObject().value("codec_name").toString(), audioCodec);
        QMediaPlayer player; QVideoSink sink; player.setVideoSink(&sink); player.setSource(QUrl::fromLocalFile(destination)); player.play();
        QTRY_VERIFY_WITH_TIMEOUT(sink.videoFrame().isValid(), 10000); player.stop(); player.setSource(QUrl());
    }
    void cancelExportPreservesDestination_data() {
        QTest::addColumn<bool>("copy"); QTest::newRow("encoded") << false; QTest::newRow("original") << true;
    }
    void cancelExportPreservesDestination() {
        QFETCH(bool, copy);
        MediaJobs jobs; QSignalSpy canceled(&jobs, &MediaJobs::canceled), saved(&jobs, &MediaJobs::exported), errors(&jobs, &MediaJobs::failed);
        QString destination = temp.filePath("canceled.mp4"); QFile file(destination); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("keep me"); file.close();
        jobs.exportClip(source, 0, 5000, destination, copy ? ExportMode::OriginalStreams : ExportMode::H264Mp4); jobs.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(canceled.count(), 1, 10000);
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("keep me"));
        QCOMPARE(QDir(temp.path()).entryList({".video-editor-*"}, QDir::Files | QDir::Hidden).count(), 0);
        file.close();
        // A canceled job must reset its process state before another export.
        jobs.exportClip(source, 1000, 2500, destination, copy ? ExportMode::OriginalStreams : ExportMode::H264Mp4);
        QTRY_VERIFY_WITH_TIMEOUT(!saved.isEmpty() || !errors.isEmpty(), 30000);
        QCOMPARE(errors.count(), 0); QCOMPARE(saved.count(), 1); QCOMPARE(canceled.count(), 1);
        QVERIFY(!jobs.busy()); QVERIFY(QFileInfo(destination).size() > 7);
    }
    void windowWorkflow() {
        EditorWindow window; window.show();
        auto *player = window.findChild<QMediaPlayer *>(); QVERIFY(player);
        auto *video = window.findChild<QVideoWidget *>(); QVERIFY(video);
        QSignalSpy states(player, &QMediaPlayer::playbackStateChanged);
        window.openFile(source);
        auto *timeline = window.findChild<Timeline *>(); QVERIFY(timeline);
        QTRY_VERIFY_WITH_TIMEOUT(timeline->duration() > 0, 10000);
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PausedState);
        QTRY_VERIFY_WITH_TIMEOUT(video->videoSink()->videoFrame().isValid(), 10000);
        QTest::qWait(150);
        QCOMPARE(player->position(), 0);
        for (const auto &state : states) QVERIFY(state.first().value<QMediaPlayer::PlaybackState>() != QMediaPlayer::PlayingState);
        QPushButton *save = nullptr;
        QPushButton *play = nullptr;
        QPushButton *playSelection = nullptr;
        for (auto *button : window.findChildren<QPushButton *>()) {
            if (button->text() == "Save selection") save = button;
            if (button->text() == "Play") play = button;
            if (button->text() == "Play selection") playSelection = button;
        }
        QVERIFY(save); QVERIFY(save->isEnabled());
        QVERIFY(play);
        QVERIFY(playSelection); QVERIFY(playSelection->isEnabled());
        QVERIFY(qAbs(playSelection->geometry().center().y() - play->geometry().center().y()) <= 1);
        QVERIFY(playSelection->x() > play->geometry().right());
        auto *in = findControl<QTimeEdit>(window, "Selection start");
        auto *outEdit = findControl<QTimeEdit>(window, "Selection end");
        QVERIFY(in); QVERIFY(outEdit);
        timeline->setSelection(1250, 3500);
        in->setFocus();
        in->setCurrentSection(QDateTimeEdit::HourSection);
        QTest::keyClick(in, Qt::Key_Up);
        QCOMPARE(timeline->start(), 1283);
        QCOMPARE(player->position(), timeline->start());
        auto *sink = video->videoSink();
        QTRY_VERIFY(qAbs(sink->videoFrame().startTime() - 1283000) <= 34000);
        const qint64 inFrame = sink->videoFrame().startTime();
        QTest::keyClick(in, Qt::Key_Up);
        QCOMPARE(timeline->start(), 1317);
        QCOMPARE(player->position(), timeline->start());
        QTRY_VERIFY(sink->videoFrame().startTime() > inFrame);
        QTest::keyClick(in, Qt::Key_Down);
        QCOMPARE(timeline->start(), 1283);
        QTRY_COMPARE(sink->videoFrame().startTime(), inFrame);
        outEdit->setFocus();
        outEdit->setCurrentSection(QDateTimeEdit::SecondSection);
        QTest::keyClick(outEdit, Qt::Key_Down);
        QCOMPARE(timeline->end(), 3467);
        QCOMPARE(player->position(), timeline->end());
        QTRY_VERIFY(qAbs(sink->videoFrame().startTime() - 3467000) <= 34000);
        QCOMPARE(player->playbackState(), QMediaPlayer::PausedState);
        QTest::keyClick(outEdit, Qt::Key_Up);
        QCOMPARE(timeline->end(), 3500);

        timeline->setSelection(10, timeline->duration() - 10);
        in->setFocus();
        QTest::keyClick(in, Qt::Key_Down);
        QCOMPARE(timeline->start(), 0);
        outEdit->setFocus();
        QTest::keyClick(outEdit, Qt::Key_Up);
        QCOMPARE(timeline->end(), timeline->duration());

        const QSize normalSize = window.size();
        window.resize(qMax(normalSize.width(), window.sizeHint().width()), normalSize.height());
        auto clickStepButton = [&](QTimeEdit *edit, QStyle::SubControl control) {
            QStyleOptionSpinBox option;
            option.initFrom(edit);
            option.buttonSymbols = edit->buttonSymbols();
            option.frame = edit->hasFrame();
            const auto rect = edit->style()->subControlRect(QStyle::CC_SpinBox, &option, control, edit);
            QVERIFY(rect.width() >= 24 && rect.height() >= 12);
            QVERIFY(edit->rect().contains(rect));
            const auto field = edit->style()->subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxEditField, edit);
            const int textWidth = edit->fontMetrics().horizontalAdvance(edit->text());
            const int compactWidth = edit->width() - field.width() + textWidth + 6;
            QVERIFY(qAbs(edit->width() - qRound(compactWidth * 1.10)) <= 2);
            QTest::mouseClick(window.windowHandle(), Qt::LeftButton, Qt::NoModifier, edit->mapTo(&window, rect.center()));
        };
        clickStepButton(in, QStyle::SC_SpinBoxUp);
        QCOMPARE(timeline->start(), 33);
        clickStepButton(outEdit, QStyle::SC_SpinBoxDown);
        QCOMPARE(timeline->end(), timeline->duration() - 33);
        clickStepButton(outEdit, QStyle::SC_SpinBoxUp);
        QCOMPARE(timeline->end(), timeline->duration());
        clickStepButton(outEdit, QStyle::SC_SpinBoxUp);
        QCOMPARE(timeline->end(), timeline->duration());
        timeline->setSelection(4000, 1000); QVERIFY(!save->isEnabled());
        QLabel *summary = nullptr;
        QLabel *formatLabel = nullptr;
        for (auto *label : window.findChildren<QLabel *>()) {
            if (label->text() == "OUT must be after IN") summary = label;
            if (label->text() == "Save as") formatLabel = label;
        }
        QVERIFY(summary); QVERIFY(formatLabel);
        timeline->setSelection(1250, 3500); QVERIFY(save->isEnabled());
        QCOMPARE(summary->text(), QString("00:00:02.3 selected"));
        auto *exportMode = findControl<QComboBox>(window, "Export mode");
        QVERIFY(exportMode); QCOMPARE(exportMode->count(), 3);
        exportMode->setCurrentIndex(exportMode->findData(int(ExportMode::OriginalStreams)));
        QCOMPARE(exportMode->currentData().toInt(), int(ExportMode::OriginalStreams));
        QVERIFY(save->isEnabled());
        for (int width : {normalSize.width(), window.minimumWidth()}) {
            window.resize(width, normalSize.height());
            const int rowY = exportMode->geometry().center().y();
            QVERIFY(qAbs(formatLabel->geometry().center().y() - rowY) <= 1);
            int previousRight = -1;
            const QWidget *clipControls[] = {in, outEdit, summary, formatLabel, exportMode, save};
            for (const auto *control : clipControls) {
                const QRect bounds = control->geometry();
                QVERIFY(bounds.left() > previousRight);
                QVERIFY(bounds.contains(bounds.center().x(), rowY));
                QVERIFY(window.centralWidget()->rect().contains(bounds));
                previousRight = bounds.right();
            }
        }
        window.resize(normalSize);
        auto screenshot = qEnvironmentVariable("VIDEO_EDITOR_SCREENSHOT");
        if (!screenshot.isEmpty()) { QTest::qWait(500); QVERIFY(window.grab().save(screenshot)); }
        player->audioOutput()->setMuted(true);
        const QPoint videoPoint = video->mapTo(&window, video->rect().center());
        const QPoint dragDistance(60, 40);
        const auto dragWindow = [&](const QPoint &from) {
            const QPoint destination = window.mapToGlobal(from + dragDistance);
            QTest::mousePress(window.windowHandle(), Qt::LeftButton, Qt::NoModifier, from);
            QTest::mouseMove(window.windowHandle(), from + dragDistance);
            QTest::mouseRelease(window.windowHandle(), Qt::LeftButton, Qt::NoModifier, window.mapFromGlobal(destination));
        };
        const QPoint originalPosition = window.pos();
        dragWindow(window.centralWidget()->mapTo(&window, QPoint(4, 4)));
        QCOMPARE(window.pos(), originalPosition + dragDistance);
        dragWindow(videoPoint);
        QCOMPARE(window.pos(), originalPosition + 2 * dragDistance);
        QCOMPARE(player->playbackState(), QMediaPlayer::PausedState);
        dragWindow(play->mapTo(&window, play->rect().center()));
        QCOMPARE(window.pos(), originalPosition + 2 * dragDistance);
        window.move(originalPosition);

        const auto clickVideo = [&](Qt::MouseButton button) {
            QTest::mouseClick(window.windowHandle(), button, Qt::NoModifier, videoPoint);
        };
        clickVideo(Qt::RightButton);
        QCOMPARE(player->playbackState(), QMediaPlayer::PausedState);

        const QPoint seekPoint(timeline->width() * 3 / 5, 40);
        QSignalSpy positions(player, &QMediaPlayer::positionChanged);
        QTest::mouseClick(timeline, Qt::LeftButton, Qt::ShiftModifier, seekPoint);
        QTRY_VERIFY(player->position() > timeline->start() + 1000);
        positions.clear();
        QTest::keyClick(timeline, Qt::Key_Left);
        QTRY_VERIFY(positions.contains(QVariantList{timeline->start()}));
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);

        const QPoint pastOut(timeline->width() * 4 / 5, 40);
        QTest::mouseClick(timeline, Qt::LeftButton, Qt::ShiftModifier, pastOut);
        QTRY_VERIFY(player->position() > timeline->end());
        QCOMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        QCOMPARE(timeline->start(), 1250);
        QCOMPARE(timeline->end(), 3500);

        const auto checkPauseAndResume = [&](auto toggle) {
            QTest::mouseClick(timeline, Qt::LeftButton, Qt::ShiftModifier, seekPoint);
            QTRY_VERIFY(player->position() > timeline->start() + 1000);
            toggle();
            QTRY_COMPARE(player->playbackState(), QMediaPlayer::PausedState);
            const qint64 pausedAt = player->position();
            QVERIFY(pausedAt > timeline->start() + 1000);
            QVERIFY(pausedAt < timeline->end());
            toggle();
            QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
            QVERIFY(player->position() >= pausedAt);
        };
        for (auto key : {Qt::Key_Space, Qt::Key_Pause})
            checkPauseAndResume([&] { QTest::keyClick(timeline, key); });
        checkPauseAndResume([&] { clickVideo(Qt::LeftButton); });
        const QPair<qint64, qint64> outSeeks[] = {{3500, 2500}, {750, 0}};
        for (const auto &[out, expected] : outSeeks) {
            timeline->setSelection(500, out);
            positions.clear();
            QTest::keyClick(timeline, Qt::Key_Right);
            QTRY_VERIFY(positions.contains(QVariantList{expected}));
            QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
            QTRY_COMPARE(player->playbackState(), QMediaPlayer::PausedState);
            QCOMPARE(player->position(), out);
        }
        player->setPosition(500);
        QTest::mouseClick(play, Qt::LeftButton);
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PausedState);
        QCOMPARE(player->position(), timeline->end());
        positions.clear();
        QTest::mouseClick(playSelection, Qt::LeftButton);
        QTRY_VERIFY(positions.contains(QVariantList{timeline->start()}));
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PausedState);
        QCOMPARE(player->position(), timeline->end());
        QTest::keyClick(&window, Qt::Key_Escape);
        QTRY_VERIFY(!window.isVisible());
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0) // QTest::wheelEvent
    void previewWheelVolume() {
        EditorWindow window; window.show();
        auto *volume = findControl<QSlider>(window, "Volume");
        auto *video = window.findChild<QVideoWidget *>();
        auto *player = window.findChild<QMediaPlayer *>();
        auto *in = findControl<QTimeEdit>(window, "Selection start");
        QVERIFY(volume); QVERIFY(video); QVERIFY(player); QVERIFY(in);
        window.openFile(source);
        QTRY_VERIFY(video->videoSink()->videoFrame().isValid());
        const auto scroll = [&](QWidget *target, QPoint delta) {
            QTest::wheelEvent(window.windowHandle(), target->mapTo(&window, target->rect().center()), delta);
        };
        volume->setValue(50);
        scroll(video, QPoint(0, 120));
        const int raisedVolume = volume->value();
        QVERIFY(raisedVolume > 50);
        QCOMPARE(player->audioOutput()->volume(), raisedVolume / 100.f);
        scroll(video, QPoint(0, -120));
        QCOMPARE(volume->value(), 50);
        scroll(video, QPoint(120, 0));
        QCOMPARE(volume->value(), 50);
        const QTime start = in->time();
        scroll(in, QPoint(0, 120));
        QVERIFY(in->time() > start);
        QCOMPARE(volume->value(), 50);
        scroll(volume, QPoint(0, 120));
        QCOMPARE(volume->value(), raisedVolume);
    }
#endif
    void openFileShortcut() {
        EditorWindow window; window.show(); window.activateWindow();
        QTRY_VERIFY(window.isActiveWindow());
        int dialogs = 0;
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, &window, [&] {
            if (auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
                ++dialogs;
                dialog->reject();
            }
        });
        dismiss.start(10);
        QTest::keyClick(window.windowHandle(), Qt::Key_O, Qt::ControlModifier);
        QCOMPARE(dialogs, 0);
        QTest::keyClick(window.windowHandle(), Qt::Key_O);
        QCOMPARE(dialogs, 1);
        auto *url = findControl<QLineEdit>(window, "YouTube URL");
        QVERIFY(url);
        url->setFocus(); url->selectAll();
        QTest::keyClicks(url, "video");
        QCOMPARE(url->text(), QString("video"));
        QCOMPARE(dialogs, 1);
    }
    void playbackSliderSeeks() {
        EditorWindow window; window.show();
        auto *slider = findControl<QSlider>(window, "Playback position");
        auto *player = window.findChild<QMediaPlayer *>();
        auto *video = window.findChild<QVideoWidget *>();
        QVERIFY(slider); QVERIFY(player); QVERIFY(video);
        window.openFile(source);
        QTRY_VERIFY(player->isSeekable() && player->playbackState() == QMediaPlayer::PausedState);
        const auto point = [&](int percent) {
            return slider->mapTo(&window, QPoint(slider->width() * percent / 100, slider->height() / 2));
        };
        auto *handle = window.windowHandle();
        for (int percent : {75, 20, 80, 25}) {
            QTest::mouseClick(handle, Qt::LeftButton, Qt::NoModifier, point(percent));
            QVERIFY(qAbs(player->position() - player->duration() * percent / 100) < 75);
        }
        QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, point(25));
        QTest::mouseMove(handle, point(65));
        QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, point(50));
        QVERIFY(qAbs(player->position() - 2500) < 75);
        QTRY_VERIFY(qAbs(video->videoSink()->videoFrame().startTime() - 2500000) < 75000);
        QCOMPARE(player->playbackState(), QMediaPlayer::PausedState);
    }
    void timelineSeekWhileLoading() {
        EditorWindow window;
        auto *jobs = window.findChild<MediaJobs *>();
        auto *timeline = window.findChild<Timeline *>();
        auto *player = window.findChild<QMediaPlayer *>();
        auto *video = window.findChild<QVideoWidget *>();
        QVERIFY(jobs); QVERIFY(timeline); QVERIFY(player); QVERIFY(video);
        jobs->streamReady(QUrl::fromLocalFile(source), "Seek fixture", 5000, 180, "seek-fixture");
        timeline->seekRequested(1000);
        timeline->seekRequested(2500);
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PausedState);
        QTRY_COMPARE(player->position(), 2500);
        QTRY_VERIFY(qAbs(video->videoSink()->videoFrame().startTime() - 2500000) <= 34000);
    }
    void settingsPersistence() {
        QSettings settings;
        settings.clear();
        const auto clearSettings = qScopeGuard([&settings] { settings.clear(); });
        settings.setValue("export/directory", temp.path());
        const QString youtubeUrl = "https://www.youtube.com/watch?v=example1234";
        QByteArray savedGeometry;
        {
            EditorWindow window;
            auto *volume = findControl<QSlider>(window, "Volume");
            auto *loadMode = findControl<QComboBox>(window, "YouTube loading mode");
            auto *exportMode = findControl<QComboBox>(window, "Export mode");
            auto *url = findControl<QLineEdit>(window, "YouTube URL");
            QVERIFY(volume); QVERIFY(loadMode); QVERIFY(exportMode);
            QVERIFY(url); QVERIFY(url->text().isEmpty());
            QCOMPARE(volume->value(), 70);
            QCOMPARE(loadMode->currentIndex(), 0);
            QCOMPARE(exportMode->currentData().toInt(), int(ExportMode::H264Mp4));
            settings.setValue("export/originalStreams", true);
            volume->setValue(37);
            loadMode->setCurrentIndex(1);
            exportMode->setCurrentIndex(exportMode->findData(int(ExportMode::H265Mp4)));
            url->setText(youtubeUrl);
            window.move(25, 40);
            window.resize(940, 720);
            window.show();
            savedGeometry = window.saveGeometry();
            QVERIFY(window.close());
        }
        settings.sync();
        QCOMPARE(settings.value("export/directory").toString(), temp.path());
        QVERIFY(!savedGeometry.isEmpty());
        QCOMPARE(settings.value("window/geometry").toByteArray(), savedGeometry);

        EditorWindow restored;
        auto *volume = findControl<QSlider>(restored, "Volume");
        auto *loadMode = findControl<QComboBox>(restored, "YouTube loading mode");
        auto *exportMode = findControl<QComboBox>(restored, "Export mode");
        auto *url = findControl<QLineEdit>(restored, "YouTube URL");
        auto *player = restored.findChild<QMediaPlayer *>();
        QVERIFY(volume); QVERIFY(loadMode); QVERIFY(exportMode); QVERIFY(player);
        QVERIFY(url); QCOMPARE(url->text(), youtubeUrl);
        QVERIFY(player->source().isEmpty());
        QCOMPARE(volume->value(), 37);
        QCOMPARE(player->audioOutput()->volume(), 0.37f);
        QCOMPARE(loadMode->currentIndex(), 1);
        QCOMPARE(exportMode->currentData().toInt(), int(ExportMode::H265Mp4));
        QMainWindow expectedWindow;
        expectedWindow.setMinimumSize(restored.minimumSize());
        QVERIFY(expectedWindow.restoreGeometry(savedGeometry));
        QCOMPARE(restored.geometry(), expectedWindow.geometry());

        settings.setValue("export/mode", 3); // Retired H.265 veryslow preference.
        {
            EditorWindow upgraded;
            auto *upgradedMode = findControl<QComboBox>(upgraded, "Export mode");
            QVERIFY(upgradedMode);
            QCOMPARE(upgradedMode->currentData().toInt(), int(ExportMode::H265Mp4));
        }
        url->clear();
        settings.remove("export/mode");
        EditorWindow cleared;
        auto *clearedUrl = findControl<QLineEdit>(cleared, "YouTube URL");
        QVERIFY(clearedUrl); QVERIFY(clearedUrl->text().isEmpty());
        auto *legacyExportMode = findControl<QComboBox>(cleared, "Export mode");
        QVERIFY(legacyExportMode);
        QCOMPARE(legacyExportMode->currentData().toInt(), int(ExportMode::OriginalStreams));
    }
    void localSelectionPersistence() {
        const auto firstPath = temp.filePath("first selection.mp4");
        const auto secondPath = temp.filePath("second selection.mp4");
        QVERIFY(QFile::copy(source, firstPath));
        QVERIFY(QFile::copy(source, secondPath));
        {
            EditorWindow window;
            auto *timeline = window.findChild<Timeline *>();
            auto *in = findControl<QTimeEdit>(window, "Selection start");
            auto *out = findControl<QTimeEdit>(window, "Selection end");
            QVERIFY(timeline); QVERIFY(in); QVERIFY(out);
            window.openFile(firstPath);
            QTRY_VERIFY(timeline->duration() > 0);
            in->setTime(QTime::fromMSecsSinceStartOfDay(1250));
            out->setTime(QTime::fromMSecsSinceStartOfDay(3750));
            window.openFile(secondPath);
            QTRY_VERIFY(timeline->duration() > 0);
            QCOMPARE(timeline->start(), 0);
            QCOMPARE(timeline->end(), timeline->duration());
            timeline->setSelection(500, 3000);
            QVERIFY(window.close());
        }

        EditorWindow restored;
        auto *timeline = restored.findChild<Timeline *>();
        auto *in = findControl<QTimeEdit>(restored, "Selection start");
        auto *out = findControl<QTimeEdit>(restored, "Selection end");
        auto *player = restored.findChild<QMediaPlayer *>();
        QVERIFY(timeline); QVERIFY(in); QVERIFY(out); QVERIFY(player);
        restored.openFile(firstPath);
        QTRY_COMPARE(timeline->start(), 1250);
        QCOMPARE(timeline->end(), 3750);
        QCOMPARE(in->time().msecsSinceStartOfDay(), 1250);
        QCOMPARE(out->time().msecsSinceStartOfDay(), 3750);
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PausedState);
        QCOMPARE(player->position(), 0);
        restored.openFile(secondPath);
        QTRY_COMPARE(timeline->start(), 500);
        QCOMPARE(timeline->end(), 3000);
        restored.openFile(secondPath);
        QCOMPARE(timeline->start(), 500);
        QCOMPARE(timeline->end(), 3000);
    }
    void youtubeSelectionPersistence() {
        const auto downloadedPath = temp.filePath("downloaded selection.mp4");
        QVERIFY(QFile::copy(source, downloadedPath));
        const QString videoId = "selection01";
        {
            EditorWindow window;
            auto *jobs = window.findChild<MediaJobs *>();
            auto *timeline = window.findChild<Timeline *>();
            auto *url = findControl<QLineEdit>(window, "YouTube URL");
            QVERIFY(jobs); QVERIFY(timeline); QVERIFY(url);
            url->setText("https://www.youtube.com/watch?v=" + videoId);
            jobs->streamReady(QUrl::fromLocalFile(source), "Selection fixture", 5000, 180, videoId);
            timeline->setSelection(600, 2700);
            QVERIFY(window.close());
        }

        EditorWindow restored;
        auto *jobs = restored.findChild<MediaJobs *>();
        auto *timeline = restored.findChild<Timeline *>();
        auto *url = findControl<QLineEdit>(restored, "YouTube URL");
        QVERIFY(jobs); QVERIFY(timeline); QVERIFY(url);
        url->setText("https://youtu.be/" + videoId);
        jobs->downloaded(downloadedPath, "Selection fixture", videoId);
        QTRY_COMPARE(timeline->start(), 600);
        QCOMPARE(timeline->end(), 2700);
    }
    void youtubeIntegration() {
        QString url = qEnvironmentVariable("VIDEO_EDITOR_TEST_YOUTUBE");
        if (url.isEmpty()) QSKIP("Set VIDEO_EDITOR_TEST_YOUTUBE to run a live YouTube download and export.");
        MediaJobs jobs; QSignalSpy loaded(&jobs, &MediaJobs::downloaded), errors(&jobs, &MediaJobs::failed), saved(&jobs, &MediaJobs::exported);
        jobs.download(url);
        QTRY_VERIFY_WITH_TIMEOUT(loaded.count() || errors.count(), 180000);
        if (errors.count()) qWarning() << errors.first().first();
        QCOMPARE(errors.count(), 0); QCOMPARE(loaded.count(), 1);
        QString cached = loaded.first().first().toString(); QVERIFY(QFileInfo(cached).size() > 0);
        QMediaPlayer player; QVideoSink sink; player.setVideoSink(&sink); player.setSource(QUrl::fromLocalFile(cached)); player.play();
        QTRY_VERIFY_WITH_TIMEOUT(sink.videoFrame().isValid(), 15000);
        player.pause();
        qint64 seekTime = qMin(qint64(10000), player.duration() / 2);
        player.setPosition(seekTime);
        QTRY_VERIFY_WITH_TIMEOUT(sink.videoFrame().startTime() >= (seekTime - 100) * 1000, 15000);
        double error = frameColorError(sink.videoFrame(), cached);
        qInfo() << "YouTube decoded RGB mean absolute error versus FFmpeg:" << error;
        QVERIFY2(error >= 0 && error < 20, qPrintable(QString("YouTube frame colors differ from reference: MAE %1").arg(error)));
        jobs.exportClip(cached, 1000, 3000, temp.filePath("youtube-clip.mp4"));
        QTRY_VERIFY_WITH_TIMEOUT(saved.count() || errors.count(), 30000);
        QCOMPARE(errors.count(), 0); QCOMPARE(saved.count(), 1);
        player.stop(); player.setSource(QUrl());
    }
    void videoColorRegression() {
        const QString fixture = qEnvironmentVariable("VIDEO_EDITOR_REGRESSION_VIDEO");
        if (fixture.isEmpty()) QSKIP("Set VIDEO_EDITOR_REGRESSION_VIDEO to check decoded colors against FFmpeg.");
        EditorWindow window; window.show(); window.raise(); window.activateWindow(); window.openFile(fixture);
        auto *player = window.findChild<QMediaPlayer *>();
        auto *video = window.findChild<QVideoWidget *>();
        QVERIFY(player); QVERIFY(video);
        QTRY_VERIFY_WITH_TIMEOUT(video->videoSink()->videoFrame().isValid(), 15000);
        player->pause(); player->setPosition(10000);
        QTRY_VERIFY_WITH_TIMEOUT(video->videoSink()->videoFrame().startTime() >= 9900000, 15000);
        QTest::qWait(300);
        const auto frame = video->videoSink()->videoFrame();
        const QImage actual = frame.toImage(); QVERIFY(!actual.isNull());
        const auto prefix = qEnvironmentVariable("VIDEO_EDITOR_REGRESSION_CAPTURE");
        if (!prefix.isEmpty()) {
            QVERIFY(actual.save(prefix + "-decoded.png"));
        }
        double error = frameColorError(frame, fixture);
        qInfo() << "Decoded RGB mean absolute error versus FFmpeg:" << error;
        QVERIFY2(error >= 0 && error < 20, qPrintable(QString("Decoded colors differ from FFmpeg: MAE %1").arg(error)));
        player->stop(); player->setSource(QUrl());
    }
    void onDemandYouTube() {
        const QString url = qEnvironmentVariable("VIDEO_EDITOR_TEST_STREAM");
        if (url.isEmpty()) QSKIP("Set VIDEO_EDITOR_TEST_STREAM for on-demand playback, distant seeking, and interval export.");
        MediaJobs jobs;
        QSignalSpy ready(&jobs, &MediaJobs::streamReady), errors(&jobs, &MediaJobs::failed), saved(&jobs, &MediaJobs::exported), downloaded(&jobs, &MediaJobs::downloaded);
        jobs.resolveStream(url);
        QTRY_VERIFY_WITH_TIMEOUT(ready.count() || errors.count(), 60000);
        if (!errors.isEmpty()) qWarning() << errors.first().first();
        QCOMPARE(errors.count(), 0); QCOMPARE(ready.count(), 1); QCOMPARE(downloaded.count(), 0);
        const QUrl stream = ready.first().at(0).toUrl();
        QCOMPARE(stream.host(), QString("127.0.0.1"));
        qint64 duration = ready.first().at(2).toLongLong(); QVERIFY(duration > 0);
        qInfo() << "Stream preview height:" << ready.first().at(3).toInt() << "duration ms:" << duration;
        QMediaPlayer player; QVideoSink sink; QAudioOutput audio; audio.setMuted(true);
        player.setVideoSink(&sink); player.setAudioOutput(&audio); player.setSource(stream); player.play();
        QTRY_VERIFY_WITH_TIMEOUT(sink.videoFrame().isValid() || player.error() != QMediaPlayer::NoError, 30000);
        QVERIFY2(player.error() == QMediaPlayer::NoError, qPrintable(player.errorString()));
        QVERIFY(sink.videoFrame().isValid()); QVERIFY(player.hasAudio()); QVERIFY(player.isSeekable());
        qint64 seekTime = duration / 2;
        player.pause(); player.setPosition(seekTime);
        QTRY_VERIFY_WITH_TIMEOUT(sink.videoFrame().startTime() >= (seekTime - 200) * 1000, 30000);
        QVERIFY(!sink.videoFrame().toImage().isNull());
        qInfo() << "Distant seek reached frame at ms:" << sink.videoFrame().startTime() / 1000;
        player.stop(); player.setSource(QUrl());
        QString destination = temp.filePath("on-demand-clip.mp4");
        jobs.exportYouTube(url, seekTime, seekTime + 2250, destination);
        QTRY_VERIFY_WITH_TIMEOUT(saved.count() || errors.count(), 90000);
        if (!errors.isEmpty()) qWarning() << errors.first().first();
        QCOMPARE(errors.count(), 0); QCOMPARE(saved.count(), 1); QCOMPARE(downloaded.count(), 0);
        QProcess probe;
        probe.start("ffprobe", {"-v", "error", "-show_entries", "format=duration:stream=codec_type,height", "-of", "json", destination});
        QVERIFY(probe.waitForFinished(10000)); QCOMPARE(probe.exitCode(), 0);
        const auto info = QJsonDocument::fromJson(probe.readAllStandardOutput()).object();
        double seconds = info.value("format").toObject().value("duration").toString().toDouble();
        QVERIFY2(qAbs(seconds - 2.25) < 0.1, qPrintable(QString::number(seconds)));
        bool hasAudio = false, hasVideo = false;
        for (const auto &entry : info.value("streams").toArray()) {
            auto track = entry.toObject();
            if (track.value("codec_type") == "audio") hasAudio = true;
            if (track.value("codec_type") == "video") { hasVideo = true; qInfo() << "Saved clip height:" << track.value("height").toInt(); }
        }
        QVERIFY(hasAudio); QVERIFY(hasVideo);
        qInfo() << "Total source bytes fetched for playback, distant seek, and export:" << jobs.streamedBytes();
        if (duration > 3600000) QVERIFY(jobs.streamedBytes() < 100 * 1024 * 1024);
        player.setSource(QUrl::fromLocalFile(destination)); player.play();
        QTRY_VERIFY_WITH_TIMEOUT(sink.videoFrame().isValid(), 10000);
        player.stop(); player.setSource(QUrl());
        // Resolving again rotates signed URLs but must retain the same media
        // identity. Chunk zero was loaded by the initial playback above.
        jobs.resolveStream(url);
        QTRY_VERIFY_WITH_TIMEOUT(ready.count() == 2 || errors.count(), 60000);
        QCOMPARE(errors.count(), 0); QCOMPARE(ready.count(), 2);
        QTest::qWait(100);
        const qint64 beforeReuse = jobs.streamedBytes();
        QNetworkAccessManager client;
        QNetworkRequest cachedRequest(ready.last().at(0).toUrl()); cachedRequest.setRawHeader("Range", "bytes=1024-2047");
        auto *cachedReply = client.get(cachedRequest); QSignalSpy cacheDone(cachedReply, &QNetworkReply::finished);
        QTRY_VERIFY_WITH_TIMEOUT(!cacheDone.isEmpty(), 10000);
        QCOMPARE(cachedReply->error(), QNetworkReply::NoError); QCOMPARE(cachedReply->readAll().size(), 1024);
        QCOMPARE(jobs.streamedBytes(), beforeReuse);
        qInfo() << "Reopened stream reused cached bytes without a CDN request; total cache bytes served:" << jobs.cachedBytesServed();
        cachedReply->deleteLater();
        const QString originalCopy = temp.filePath("youtube-original.mp4");
        jobs.exportYouTube(url, seekTime + 1250, seekTime + 3500, originalCopy, ExportMode::OriginalStreams);
        QTRY_VERIFY_WITH_TIMEOUT(saved.count() == 2 || !errors.isEmpty(), 90000);
        if (!errors.isEmpty()) qWarning() << errors.first().first();
        QCOMPARE(errors.count(), 0); QCOMPARE(saved.count(), 2);
        QVERIFY(jobs.actualExportStart() <= seekTime + 1250);
        probe.start(findTool("ffprobe"), {"-v", "error", "-show_entries", "stream=codec_name,start_time:format=duration", "-of", "json", originalCopy});
        QVERIFY(probe.waitForFinished(10000)); QCOMPARE(probe.exitCode(), 0);
        const auto copiedInfo = QJsonDocument::fromJson(probe.readAllStandardOutput()).object();
        qInfo() << "Original YouTube stream codecs:" << copiedInfo;
        const auto copiedTracks = copiedInfo.value("streams").toArray(); QCOMPARE(copiedTracks.size(), 2);
        const double copiedDuration = copiedInfo.value("format").toObject().value("duration").toString().toDouble();
        const double expectedDuration = (seekTime + 3500 - jobs.actualExportStart()) / 1000.0;
        QVERIFY2(qAbs(copiedDuration - expectedDuration) < 0.5, qPrintable(QString("Copied %1s, expected about %2s").arg(copiedDuration).arg(expectedDuration)));
        const double videoStart = copiedTracks[0].toObject().value("start_time").toString().toDouble();
        const double audioStart = copiedTracks[1].toObject().value("start_time").toString().toDouble();
        QVERIFY2(qAbs(videoStart - audioStart) < 0.25, qPrintable(QString("Video/audio start difference: %1s").arg(videoStart - audioStart)));
        player.setSource(QUrl::fromLocalFile(originalCopy)); player.play();
        QTRY_VERIFY_WITH_TIMEOUT(sink.videoFrame().isValid(), 10000); player.stop(); player.setSource(QUrl());
    }
    void rangeBridgeRequests() {
        // A local CDN fixture applies the query range first, matching YouTube's
        // behavior. Exercise seeks crossing multiple upstream chunks.
        QByteArray content(6 * 1024 * 1024, Qt::Uninitialized);
        for (qsizetype i = 0; i < content.size(); ++i) content[i] = char((i / 1021 + i) % 251);
        QTcpServer origin; QVERIFY(origin.listen(QHostAddress::LocalHost, 0));
        int requests = 0;
        connect(&origin, &QTcpServer::newConnection, &origin, [&] {
            auto *socket = origin.nextPendingConnection();
            auto buffer = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket, buffer] {
                *buffer += socket->readAll(); if (!buffer->contains("\r\n\r\n")) return;
                ++requests;
                const QUrl url("http://localhost" + QString::fromUtf8(buffer->split(' ').at(1)));
                const auto range = QUrlQuery(url).queryItemValue("range").split('-');
                qint64 first = range.value(0).toLongLong(), last = range.value(1).toLongLong();
                const auto data = content.mid(first, last - first + 1);
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(data.size()) + "\r\nConnection: close\r\n\r\n" + data);
                socket->disconnectFromHost();
            });
        });
        auto bridge = std::make_unique<RangeBridge>();
        const QJsonObject format{{"url", QString("http://127.0.0.1:%1/file?clen=%2").arg(origin.serverPort()).arg(content.size())}};
        const QUrl source = bridge->add(format);
        QVERIFY(!source.isEmpty()); QNetworkAccessManager client;
        QNetworkRequest request(source); request.setRawHeader("Range", "bytes=1048576-5767167");
        auto *reply = client.get(request); QSignalSpy done(reply, &QNetworkReply::finished);
        QTRY_VERIFY_WITH_TIMEOUT(!done.isEmpty(), 10000);
        QCOMPARE(reply->error(), QNetworkReply::NoError);
        QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 206);
        QCOMPARE(reply->rawHeader("Content-Range"), QByteArray("bytes 1048576-5767167/6291456"));
        QCOMPARE(reply->readAll(), content.mid(1048576, 4718592)); QVERIFY(requests >= 3); reply->deleteLater();
        request.setRawHeader("Range", "bytes=-16"); reply = client.get(request); QSignalSpy suffixDone(reply, &QNetworkReply::finished);
        QTRY_VERIFY_WITH_TIMEOUT(!suffixDone.isEmpty(), 5000); QCOMPARE(reply->readAll(), content.right(16)); reply->deleteLater();
        request.setRawHeader("Range", "bytes=999999999-"); reply = client.get(request); QSignalSpy invalidDone(reply, &QNetworkReply::finished);
        QTRY_VERIFY_WITH_TIMEOUT(!invalidDone.isEmpty(), 5000); QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 416); reply->deleteLater();
        const int requestsBeforeReuse = requests;
        bridge.reset();
        RangeBridge reopened;
        request.setUrl(reopened.add(format)); request.setRawHeader("Range", "bytes=1200000-3500000");
        reply = client.get(request); QSignalSpy reuseDone(reply, &QNetworkReply::finished);
        QTRY_VERIFY_WITH_TIMEOUT(!reuseDone.isEmpty(), 5000);
        QCOMPARE(reply->error(), QNetworkReply::NoError);
        QCOMPARE(reply->readAll(), content.mid(1200000, 2300001));
        QCOMPARE(requests, requestsBeforeReuse); QCOMPARE(reopened.bytesReceived(), 0);
        QCOMPARE(reopened.cachedBytesServed(), 2300001); reply->deleteLater();
    }
    void cacheIntegrityAndEviction() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const QByteArray a(1000, 'a'), b(1000, 'b'), c(1000, 'c');
        const qint64 limit = 2 * (a.size() + 32);
        ChunkCache cache(directory.path(), limit);
        cache.write("media", 0, a); cache.write("media", ChunkCache::ChunkSize, b);
        for (const auto &name : QDir(directory.path()).entryList({"*.chunk"}, QDir::Files)) {
            QFile file(directory.filePath(name)); QVERIFY(file.open(QIODevice::ReadWrite));
            QVERIFY(file.setFileTime(QDateTime::currentDateTimeUtc().addSecs(-60), QFileDevice::FileModificationTime));
        }
        QCOMPARE(cache.read("media", 0, a.size()), a); // Make A more recent than B.
        cache.write("media", 2 * ChunkCache::ChunkSize, c);
        QCOMPARE(cache.read("media", 0, a.size()), a);
        QVERIFY(cache.read("media", ChunkCache::ChunkSize, b.size()).isEmpty());
        QCOMPARE(cache.read("media", 2 * ChunkCache::ChunkSize, c.size()), c);
        qint64 used = 0;
        for (const auto &file : QDir(directory.path()).entryInfoList({"*.chunk"}, QDir::Files)) used += file.size();
        QVERIFY(used <= limit);
        // A same-sized corrupt entry must fail its digest, not reach the decoder.
        auto files = QDir(directory.path()).entryList({"*-0.chunk"}, QDir::Files); QCOMPARE(files.size(), 1);
        QFile damaged(directory.filePath(files.first())); QVERIFY(damaged.open(QIODevice::ReadWrite));
        QVERIFY(damaged.seek(40)); QCOMPARE(damaged.write("z", 1), 1); damaged.close();
        QVERIFY(cache.read("media", 0, a.size()).isEmpty()); QVERIFY(!damaged.exists());
        cache.write("media", 0, a);
        QFile partial(directory.filePath(files.first())); QVERIFY(partial.open(QIODevice::ReadWrite)); QVERIFY(partial.resize(100)); partial.close();
        QVERIFY(cache.read("media", 0, a.size()).isEmpty()); QVERIFY(!partial.exists());
    }
    void streamLengthProbe_data() {
        QTest::addColumn<bool>("headSupported"); QTest::addColumn<bool>("cancelProbe");
        QTest::newRow("head-length") << true << false;
        QTest::newRow("range-fallback") << false << false;
        QTest::newRow("cancel") << true << true;
    }
    void streamLengthProbe() {
        QFETCH(bool, headSupported); QFETCH(bool, cancelProbe);
        QTcpServer origin; QVERIFY(origin.listen(QHostAddress::LocalHost, 0));
        int heads = 0, gets = 0; constexpr qint64 exactSize = 524288;
        connect(&origin, &QTcpServer::newConnection, &origin, [&] {
            auto *socket = origin.nextPendingConnection(); auto buffer = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket, buffer] {
                *buffer += socket->readAll(); if (!buffer->contains("\r\n\r\n")) return;
                if (buffer->startsWith("HEAD ")) {
                    ++heads; if (cancelProbe) return;
                    if (headSupported) socket->write("HTTP/1.1 200 OK\r\nContent-Length: 524288\r\nConnection: close\r\n\r\n");
                    else socket->write("HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                } else {
                    ++gets;
                    socket->write("HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 0-0/524288\r\nContent-Length: 1\r\nConnection: close\r\n\r\nx");
                }
                socket->disconnectFromHost();
            });
        });
        const QString fixture = temp.filePath("resolver.json"); QFile file(fixture); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(QJsonObject{{"id", "size-probe"}, {"duration", 10}, {"height", 360}, {"title", "Size probe"},
            {"url", QString("http://127.0.0.1:%1/media").arg(origin.serverPort())}, {"filesize_approx", 999}}).toJson()); file.close();
        const auto oldTool = qgetenv("VIDEO_EDITOR_YT_DLP"), oldFixture = qgetenv("VIDEO_EDITOR_RESOLVER_FIXTURE");
        auto restore = qScopeGuard([&] {
            if (oldTool.isNull()) qunsetenv("VIDEO_EDITOR_YT_DLP"); else qputenv("VIDEO_EDITOR_YT_DLP", oldTool);
            if (oldFixture.isNull()) qunsetenv("VIDEO_EDITOR_RESOLVER_FIXTURE"); else qputenv("VIDEO_EDITOR_RESOLVER_FIXTURE", oldFixture);
        });
        qputenv("VIDEO_EDITOR_YT_DLP", QCoreApplication::applicationFilePath().toUtf8()); qputenv("VIDEO_EDITOR_RESOLVER_FIXTURE", fixture.toUtf8());
        MediaJobs jobs; QSignalSpy ready(&jobs, &MediaJobs::streamReady), errors(&jobs, &MediaJobs::failed), canceled(&jobs, &MediaJobs::canceled);
        jobs.resolveStream("https://youtube.com/watch?v=size-probe");
        QTRY_VERIFY_WITH_TIMEOUT(heads > 0 || !errors.isEmpty(), 10000); QCOMPARE(errors.count(), 0);
        if (cancelProbe) {
            QVERIFY(jobs.busy()); jobs.cancel(); QCOMPARE(canceled.count(), 1); QVERIFY(!jobs.busy());
            QTest::qWait(100); QCOMPARE(ready.count(), 0); QCOMPARE(errors.count(), 0); return;
        }
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 5000); QVERIFY(!jobs.busy()); QCOMPARE(heads, 1); QCOMPARE(gets, headSupported ? 0 : 1);
        QCOMPARE(ready.first().at(4).toString(), QString("size-probe"));
        QNetworkAccessManager client; auto *reply = client.head(QNetworkRequest(ready.first().first().toUrl())); QSignalSpy done(reply, &QNetworkReply::finished);
        QTRY_VERIFY_WITH_TIMEOUT(!done.isEmpty(), 5000); QCOMPARE(reply->error(), QNetworkReply::NoError);
        QCOMPARE(reply->rawHeader("Content-Length").toLongLong(), exactSize); reply->deleteLater();
    }
    void cleanupTestCase() {
        if (previousCacheDirectory.isNull()) qunsetenv("VIDEO_EDITOR_CACHE_DIR"); else qputenv("VIDEO_EDITOR_CACHE_DIR", previousCacheDirectory);
    }
};
int main(int argc, char **argv) {
    // Test-only stand-in for yt-dlp, allowing deterministic resolver metadata.
    const auto fixture = qgetenv("VIDEO_EDITOR_RESOLVER_FIXTURE");
    bool resolving = false;
    for (int i = 1; i < argc; ++i) if (QByteArray(argv[i]) == "--dump-single-json") resolving = true;
    if (!fixture.isEmpty() && resolving) {
        QFile file(QString::fromUtf8(fixture)), output;
        if (!file.open(QIODevice::ReadOnly) || !output.open(stdout, QIODevice::WriteOnly)) return 1;
        auto data = file.readAll(); return output.write(data) == data.size() && output.flush() ? 0 : 1;
    }
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv); EditorTests tests; return QTest::qExec(&tests, argc, argv);
}
#include "editor_tests.moc"
