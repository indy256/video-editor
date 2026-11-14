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
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QComboBox>
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
        Timeline timeline; timeline.resize(832, 100); timeline.show(); timeline.setDuration(10000);
        QSignalSpy seek(&timeline, &Timeline::seekRequested);
        QTest::mouseClick(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(176, 40));
        QCOMPARE(timeline.start(), 2000);
        QTest::mouseClick(&timeline, Qt::RightButton, Qt::NoModifier, QPoint(656, 40));
        QCOMPARE(timeline.end(), 8000); QCOMPARE(seek.count(), 2);
        QTest::mouseClick(&timeline, Qt::LeftButton, Qt::ShiftModifier, QPoint(416, 40));
        QCOMPARE(timeline.start(), 2000); QCOMPARE(seek.last().first().toLongLong(), 5000);
        QTest::mouseClick(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(736, 40));
        QVERIFY(timeline.start() > timeline.end());
        timeline.setDuration(5000); QCOMPARE(timeline.start(), 0); QCOMPARE(timeline.end(), 5000);
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
    void exactExportAndOverwrite() {
        MediaJobs jobs; QSignalSpy saved(&jobs, &MediaJobs::exported), failed(&jobs, &MediaJobs::failed);
        QString destination = temp.filePath("clip with spaces.mp4");
        QFile existing(destination); QVERIFY(existing.open(QIODevice::WriteOnly)); existing.write("old data"); existing.close();
        jobs.exportClip(source, 1250, 3500, destination);
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
                QCOMPARE(info.value("codec_name").toString(), QString("h264"));
                QCOMPARE(info.value("profile").toString(), QString("Main"));
                QCOMPARE(info.value("codec_tag_string").toString(), QString("avc1"));
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
        QTest::addColumn<bool>("withAudio");
        QTest::newRow("h264-flac") << true;
        QTest::newRow("video-only") << false;
    }
    void originalStreamExport() {
        QFETCH(bool, withAudio);
        const QString original = temp.filePath("original-streams.mkv"), destination = temp.filePath("copied-streams.mkv");
        QProcess ff;
        QStringList args{"-v", "error", "-y", "-f", "lavfi", "-i", "testsrc2=size=320x180:rate=30"};
        if (withAudio) args += QStringList{"-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000"};
        args += QStringList{"-t", "6", "-c:v", "libx264", "-g", "60", "-keyint_min", "60", "-sc_threshold", "0", "-pix_fmt", "yuv420p"};
        if (withAudio) args += QStringList{"-c:a", "flac"};
        args += original; ff.start(findTool("ffmpeg"), args); QVERIFY(ff.waitForFinished(30000)); QCOMPARE(ff.exitCode(), 0);
        QFile existing(destination); QVERIFY(existing.open(QIODevice::WriteOnly)); existing.write("replace only after successful copy"); existing.close();
        MediaJobs jobs; QSignalSpy saved(&jobs, &MediaJobs::exported), errors(&jobs, &MediaJobs::failed);
        jobs.exportClip(original, 3250, 4750, destination, ExportMode::OriginalStreams);
        QTRY_VERIFY_WITH_TIMEOUT(!saved.isEmpty() || !errors.isEmpty(), 30000);
        if (!errors.isEmpty()) qWarning() << errors.first().first();
        QCOMPARE(errors.count(), 0); QCOMPARE(saved.count(), 1); QVERIFY(jobs.copiesOriginalStreams());
        QVERIFY(jobs.actualExportStart() >= 1900 && jobs.actualExportStart() <= 2100);
        auto packets = [](const QString &path, const QString &track) {
            QProcess probe; probe.start(findTool("ffprobe"), {"-v", "error", "-select_streams", track, "-show_packets", "-show_data_hash", "sha256", "-show_entries", "packet=data_hash,flags,pts_time", "-of", "json", path});
            if (!probe.waitForFinished(15000) || probe.exitCode()) return QJsonArray();
            return QJsonDocument::fromJson(probe.readAllStandardOutput()).object().value("packets").toArray();
        };
        for (const auto &track : {QString("v:0"), QString("a:0")}) {
            const auto before = packets(original, track), after = packets(destination, track);
            if (track == "a:0" && !withAudio) { QVERIFY(after.isEmpty()); continue; }
            QVERIFY(!before.isEmpty()); QVERIFY(!after.isEmpty());
            QStringList beforeHashes, afterHashes;
            for (const auto &p : before) beforeHashes += p.toObject().value("data_hash").toString();
            for (const auto &p : after) afterHashes += p.toObject().value("data_hash").toString();
            const int first = beforeHashes.indexOf(afterHashes.first()); QVERIFY(first >= 0);
            QCOMPARE(afterHashes, beforeHashes.mid(first, afterHashes.size()));
            if (track == "v:0") {
                QVERIFY(after.first().toObject().value("flags").toString().contains('K'));
                QVERIFY(qAbs(before[first].toObject().value("pts_time").toString().toDouble() - 2.0) < 0.1);
            }
        }
        const auto video = packets(destination, "v:0"), audio = packets(destination, "a:0");
        if (withAudio) QVERIFY(qAbs(video.first().toObject().value("pts_time").toString().toDouble() - audio.first().toObject().value("pts_time").toString().toDouble()) < 0.15);
        QProcess probe; probe.start(findTool("ffprobe"), {"-v", "error", "-show_entries", "format=duration:stream=codec_name", "-of", "json", destination});
        QVERIFY(probe.waitForFinished(10000)); QCOMPARE(probe.exitCode(), 0);
        const auto info = QJsonDocument::fromJson(probe.readAllStandardOutput()).object();
        const double duration = info.value("format").toObject().value("duration").toString().toDouble();
        // Packet DTS cutoffs and reordered frames can extend the last PTS.
        QVERIFY2(duration >= 2.65 && duration <= 3.05, qPrintable(QString::number(duration)));
        if (withAudio) QCOMPARE(info.value("streams").toArray().last().toObject().value("codec_name").toString(), QString("flac"));
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
        jobs.exportClip(source, 0, 5000, destination, copy ? ExportMode::OriginalStreams : ExportMode::PreciseMp4); jobs.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(canceled.count(), 1, 10000);
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("keep me"));
        QCOMPARE(QDir(temp.path()).entryList({".video-editor-*"}, QDir::Files | QDir::Hidden).count(), 0);
        file.close();
        // A canceled job must reset its process state before another export.
        jobs.exportClip(source, 1000, 2500, destination, copy ? ExportMode::OriginalStreams : ExportMode::PreciseMp4);
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
        for (auto *button : window.findChildren<QPushButton *>()) if (button->text() == "Save selection") save = button;
        QVERIFY(save); QVERIFY(save->isEnabled());
        timeline->setSelection(4000, 1000); QVERIFY(!save->isEnabled());
        timeline->setSelection(1250, 3500); QVERIFY(save->isEnabled());
        QComboBox *exportMode = nullptr;
        for (auto *combo : window.findChildren<QComboBox *>()) if (combo->accessibleName() == "Export mode") exportMode = combo;
        QVERIFY(exportMode); QCOMPARE(exportMode->count(), 2); exportMode->setCurrentIndex(1); QVERIFY(save->isEnabled());
        auto screenshot = qEnvironmentVariable("VIDEO_EDITOR_SCREENSHOT");
        if (!screenshot.isEmpty()) { QTest::qWait(500); QVERIFY(window.grab().save(screenshot)); }
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
        const QString originalCopy = temp.filePath("youtube-original.mkv");
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
    QApplication app(argc, argv); EditorTests tests; return QTest::qExec(&tests, argc, argv);
}
#include "editor_tests.moc"
