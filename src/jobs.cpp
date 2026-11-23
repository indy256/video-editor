#include "jobs.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#include <QUrlQuery>
#include <QTimer>
#include <algorithm>
#include <cstdio>
#include <cmath>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

// Qt's bundled FFmpeg on Windows ARM64 can produce green frames with AV1.
static const QString compatibleFormats =
    "bv*[height<=1080][vcodec^=avc1]+ba/"
    "b[height<=1080][vcodec^=avc1]/"
    "bv*[height<=1080][vcodec^=vp9]+ba/"
    "b[height<=1080][vcodec^=vp9]";
static const QString previewFormats =
    "b[height<=1080][vcodec^=avc1][protocol=https]/"
    "b[height<=1080][vcodec^=avc1][protocol=http]";

static QStringList downloaderArguments(const QString &formats, const QString &timeout) {
    QStringList arguments{"--ignore-config", "--no-playlist", "--no-colors",
                          "--format", formats, "--socket-timeout", timeout, "--retries", "2"};
    const auto deno = findTool("deno");
    if (!deno.isEmpty()) arguments += QStringList{"--js-runtimes", "deno:" + deno};
    return arguments;
}

static QStringList ffmpegInput(const QUrl &source) {
    if (source.isLocalFile()) return {"-i", source.toLocalFile()};
    return {"-rw_timeout", "20000000", "-i", source.toString(QUrl::FullyEncoded)};
}

QString findTool(const QString &name) {
    const QString override = qEnvironmentVariable(("VIDEO_EDITOR_" + name.toUpper().replace('-', '_')).toUtf8().constData());
    if (!override.isEmpty()) return QFileInfo::exists(override) ? override : QString();
    for (const QString &dir : {QCoreApplication::applicationDirPath() + "/tools", QCoreApplication::applicationDirPath()}) {
        QString path = dir + "/" + name;
#ifdef Q_OS_WIN
        path += ".exe";
#endif
        if (QFileInfo::exists(path)) return path;
    }
    return QStandardPaths::findExecutable(name);
}
bool isYouTubeUrl(const QString &text) {
    QUrl url(text.trimmed());
    QString host = url.host().toLower();
    return (url.scheme() == "https" || url.scheme() == "http") && url.userInfo().isEmpty()
        && (host == "youtu.be" || host == "youtube.com" || host.endsWith(".youtube.com"));
}
MediaJobs::MediaJobs(QObject *parent) : QObject(parent) {
    connect(&process_, &QProcess::readyReadStandardOutput, this, &MediaJobs::readOutput);
    connect(&process_, &QProcess::readyReadStandardError, this, [this] {
        log_ = (log_ + QString::fromUtf8(process_.readAllStandardError())).right(12000);
    });
    connect(&process_, &QProcess::finished, this, &MediaJobs::finish);
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) fail("Could not start tool: " + process_.errorString());
    });
}
MediaJobs::~MediaJobs() {
    ++resolveGeneration_;
    if (probe_) { disconnect(probe_, nullptr, this, nullptr); probe_->abort(); }
    disconnect(&process_, nullptr, this, nullptr);
    if (process_.state() != QProcess::NotRunning) { process_.kill(); process_.waitForFinished(3000); }
    discardOutput();
}
void MediaJobs::startProcess(Operation operation, const QString &tool, const QStringList &arguments, const QString &message, int percent) {
    operation_ = operation;
    active_ = true;
    canceled_ = false;
    log_.clear(); lines_.clear(); metadata_.clear();
    const bool separate = operation == Operation::ResolvePreview || operation == Operation::ResolveExport || operation == Operation::FindKeyframe;
    process_.setProcessChannelMode(separate ? QProcess::SeparateChannels : QProcess::MergedChannels);
    emit progress(percent, message);
    process_.start(tool, arguments);
}
void MediaJobs::discardOutput() {
    if (!staging_.isEmpty()) {
        QFile::remove(staging_);
        staging_.clear();
    }
    pendingCache_.reset();
}
void MediaJobs::fail(const QString &message) {
    active_ = false;
    discardOutput();
    emit failed(message);
}
void MediaJobs::download(const QString &url) {
    if (busy()) return;
    if (!isYouTubeUrl(url)) { emit failed("Enter a valid youtube.com or youtu.be URL."); return; }
    const auto tool = findTool("yt-dlp"), ffmpeg = findTool("ffmpeg");
    if (tool.isEmpty() || ffmpeg.isEmpty()) { emit failed("YouTube needs yt-dlp and FFmpeg. Place them in the tools folder beside the app, or on PATH."); return; }
    pendingCache_ = std::make_unique<QTemporaryDir>(QDir::tempPath() + "/video-editor-XXXXXX");
    if (!pendingCache_->isValid()) { fail("Could not create the video cache."); return; }
    startProcess(Operation::Download, tool, downloaderArguments(compatibleFormats, "30") + QStringList{
        "--newline", "--no-mtime", "--ffmpeg-location", ffmpeg, "--merge-output-format", "mp4",
        "--write-info-json", "--progress-template", "download:PROGRESS:%(progress._percent_str)s",
        "--output", pendingCache_->path() + "/source.%(ext)s", "--", url.trimmed()},
        "Downloading YouTube video to a temporary cache...");
}
void MediaJobs::resolveStream(const QString &url) {
    if (busy()) return;
    resolve(url, false);
}
void MediaJobs::resolve(const QString &url, bool forExport) {
    if (!isYouTubeUrl(url)) { fail("Enter a valid youtube.com or youtu.be URL."); return; }
    const QString tool = findTool("yt-dlp");
    if (tool.isEmpty()) { fail("yt-dlp was not found. Add it to the tools folder or PATH."); return; }
    ++resolveGeneration_;
    startProcess(forExport ? Operation::ResolveExport : Operation::ResolvePreview, tool,
        downloaderArguments(forExport ? compatibleFormats : previewFormats, "20") + QStringList{
            "--skip-download", "--dump-single-json", "--", url.trimmed()},
        forExport ? "Resolving fresh streams for the selected interval..." : "Opening YouTube stream...");
}
bool MediaJobs::prepareExport(qint64 start, qint64 end, const QString &destination, ExportMode mode) {
    if (start < 0 || end <= start || destination.isEmpty()) { fail("Choose an end time after the start time and an output file."); return false; }
    if (findTool("ffmpeg").isEmpty()) { fail("FFmpeg was not found. Place ffmpeg in the tools folder beside the app, or on PATH."); return false; }
    exportMode_ = mode; actualStartUs_ = start * 1000;
    const QFileInfo target(destination);
    destination_ = target.absoluteFilePath();
    staging_ = target.absolutePath() + "/.video-editor-" + QUuid::createUuid().toString(QUuid::Id128) + ".mp4";
    exportStart_ = start; exportDuration_ = end - start;
    return true;
}
void MediaJobs::exportYouTube(const QString &url, qint64 start, qint64 end, const QString &destination, ExportMode mode) {
    if (busy() || !prepareExport(start, end, destination, mode)) return;
    resolve(url, true);
}
void MediaJobs::startExport(const QUrl &video, const QUrl &audio) {
    exportVideo_ = video;
    exportAudio_ = audio;
    if (!copiesOriginalStreams()) {
        writeOutput();
        return;
    }

    // Use the final copy's demuxer seek and normalized source timestamps to
    // identify its first keyframe without decoding or encoding the video.
    QStringList arguments{"-v", "error", "-nostdin", "-copyts", "-start_at_zero", "-ss", QString::number(exportStart_ / 1000.0, 'f', 3)};
    arguments += ffmpegInput(exportVideo_);
    arguments += QStringList{"-map", "0:v:0", "-c", "copy", "-frames:v", "1", "-f", "framehash", "pipe:1"};
    startProcess(Operation::FindKeyframe, findTool("ffmpeg"), arguments, "Locating the keyframe for an original stream copy...");
}
void MediaJobs::writeOutput() {
    const bool copy = copiesOriginalStreams();
    const bool separateAudio = !exportAudio_.isEmpty();
    const double actualStart = actualStartUs_ / 1000000.0;
    const double requestedStart = exportStart_ / 1000.0;
    const auto requestedSeek = QString::number(requestedStart, 'f', 3);
    QStringList arguments{"-hide_banner", "-nostdin", "-y", "-ss", requestedSeek};
    // Keep the video seek inside the desired GOP. Seeking to the keyframe's
    // PTS itself can choose the previous GOP when its DTS is earlier.
    if (copy) arguments += QStringList{"-itsoffset", QString::number(requestedStart - actualStart, 'f', 6)};
    arguments += ffmpegInput(exportVideo_);
    if (separateAudio) {
        arguments += QStringList{"-ss", copy ? QString::number(actualStart, 'f', 6) : requestedSeek};
        arguments += ffmpegInput(exportAudio_);
    }
    const double duration = (exportStart_ + exportDuration_) / 1000.0 - actualStart;
    arguments += QStringList{"-t", QString::number(duration, 'f', 6), "-map", "0:v:0", "-map", separateAudio ? "1:a:0" : "0:a:0?"};
    if (copy) {
        // Discard audio seek preroll and retain the preceding video keyframe.
        arguments += QStringList{"-c", "copy", "-copypriorss:v", "1", "-copypriorss:a", "0", "-avoid_negative_ts", "make_zero"};
    } else {
        if (exportMode_ == ExportMode::H264Mp4) {
            arguments += QStringList{"-c:v", "libx264", "-preset", "veryslow", "-tag:v", "avc1"};
        } else {
            arguments += QStringList{"-c:v", "libx265", "-preset", "slow", "-tag:v", "hvc1"};
        }
        arguments += QStringList{"-crf", "32", "-pix_fmt", "yuv420p", "-profile:v", "main", "-c:a", "aac", "-b:a", "128k"};
    }
    arguments += QStringList{"-movflags", "+faststart", "-progress", "pipe:1", "-nostats", staging_};
    startProcess(Operation::Export, findTool("ffmpeg"), arguments,
        copy ? "Copying original video and audio streams..." : "Fetching and encoding selection...", 0);
}
void MediaJobs::exportClip(const QString &source, qint64 start, qint64 end, const QString &destination, ExportMode mode) {
    if (busy()) return;
    if (!QFileInfo(source).isFile()) { emit failed("Choose a valid source video."); return; }
    const QString sourcePath = QFileInfo(source).canonicalFilePath();
    const QString destinationPath = QFileInfo(destination).canonicalFilePath();
#ifdef Q_OS_WIN
    const bool sameFile = sourcePath.compare(destinationPath, Qt::CaseInsensitive) == 0;
#else
    const bool sameFile = sourcePath == destinationPath;
#endif
    if (sameFile) { emit failed("Choose a different file; the source video cannot be overwritten."); return; }
    if (!prepareExport(start, end, destination, mode)) return;
    startExport(QUrl::fromLocalFile(source));
}
void MediaJobs::cancel() {
    if (!busy()) return;
    canceled_ = true;
    ++resolveGeneration_;
    if (process_.state() == QProcess::NotRunning) {
        if (probe_) { auto *reply = probe_.data(); probe_.clear(); disconnect(reply, nullptr, this, nullptr); reply->abort(); reply->deleteLater(); }
        discardOutput();
        active_ = false; emit canceled(); return;
    }
#ifdef Q_OS_WIN
    // yt-dlp may spawn FFmpeg; kill the whole owned process tree before removing its cache.
    QProcess killer;
    killer.start("taskkill", {"/PID", QString::number(process_.processId()), "/T", "/F"});
    killer.waitForFinished(3000);
#endif
    process_.kill();
}
void MediaJobs::readOutput() {
    if (process_.processChannelMode() == QProcess::SeparateChannels) { metadata_ += process_.readAllStandardOutput(); return; }
    const auto chunk = QString::fromUtf8(process_.readAllStandardOutput());
    log_ = (log_ + chunk).right(12000); lines_ += chunk;
    int index;
    while ((index = lines_.indexOf('\n')) >= 0) {
        QString line = lines_.left(index).trimmed(); lines_.remove(0, index + 1);
        if (line.startsWith("PROGRESS:")) {
            bool ok; double value = line.mid(9).remove('%').trimmed().toDouble(&ok);
            if (ok) emit progress(std::clamp(int(value), 0, 100), "Downloading video...");
        } else if (line.startsWith("out_time_us=")) {
            bool ok; qint64 us = line.mid(12).toLongLong(&ok);
            if (ok && exportDuration_) emit progress(std::clamp(int(us / 10 / exportDuration_), 0, 99), copiesOriginalStreams() ? "Copying original streams..." : "Encoding selection...");
        }
    }
    if (lines_.size() > 16000) lines_ = lines_.right(16000);
}
void MediaJobs::finish(int code, QProcess::ExitStatus status) {
    readOutput(); active_ = false;
    if (canceled_) {
        discardOutput(); emit canceled(); return;
    }
    const bool resolving = operation_ == Operation::ResolvePreview || operation_ == Operation::ResolveExport;
    if (code || status != QProcess::NormalExit) {
        QString message = "Export failed.";
        if (operation_ == Operation::Download || resolving) {
            message = "YouTube operation failed. Update yt-dlp if YouTube has changed. Try Download first if a streaming preview is unavailable.";
        } else if (operation_ == Operation::Export && copiesOriginalStreams()) {
            message = "Could not save the original streams as MP4. If the source codecs are incompatible with MP4, choose H.264 or H.265.";
        }
        fail(message + "\n\n" + log_.right(6000));
        return;
    }
    if (operation_ == Operation::FindKeyframe) {
        const auto report = QString::fromUtf8(metadata_); metadata_.clear();
        const auto timebase = QRegularExpression("#tb 0: (\\d+)/(\\d+)").match(report);
        bool ok = false; double pts = 0;
        for (const auto &line : report.split('\n')) {
            const auto values = line.split(',');
            if (line.trimmed().startsWith("0,") && values.size() >= 6) { pts = values[2].trimmed().toDouble(&ok); break; }
        }
        const double denominator = timebase.captured(2).toDouble();
        pts = denominator > 0 ? pts * timebase.captured(1).toDouble() / denominator : -1;
        if (!ok || !timebase.hasMatch() || !std::isfinite(pts) || pts < 0 || pts > qMax(0.1, exportStart_ / 1000.0) + 0.001) {
            fail("Could not locate a keyframe at or before IN. Choose H.264 or H.265 for this video."); return;
        }
        actualStartUs_ = qMin(exportStart_ * 1000, qMax(qint64(0), qRound64(pts * 1000000)));
        writeOutput(); return;
    }
    if (resolving) {
        QJsonParseError error;
        const auto info = QJsonDocument::fromJson(metadata_, &error).object(); metadata_.clear();
        if (error.error != QJsonParseError::NoError || info.isEmpty()) { fail("Could not read YouTube stream information."); return; }
        if (info.value("is_live").toBool() || info.value("duration").toDouble() <= 0) { fail("Streaming selection requires a finite video with a known duration."); return; }
        QJsonArray formats;
        if (operation_ == Operation::ResolveExport) formats = info.value("requested_formats").toArray();
        if (formats.isEmpty()) formats.append(info);
        prepareResolved(info, formats);
        return;
    }
    if (operation_ == Operation::Download) {
        const QDir dir(pendingCache_->path());
        const auto files = dir.entryList({"source.mp4", "source.mkv", "source.webm", "source.mov"}, QDir::Files | QDir::CaseSensitive);
        if (files.isEmpty()) { fail("The download finished without a playable video file.\n" + log_.right(4000)); return; }
        QFile json(dir.filePath("source.info.json"));
        if (!json.open(QIODevice::ReadOnly)) { fail("Download metadata is missing."); return; }
        auto metadata = QJsonDocument::fromJson(json.readAll()).object();
        downloadCaches_.push_back(std::move(pendingCache_));
        emit downloaded(dir.absoluteFilePath(files.last()), metadata.value("id").toString());
    } else {
        if (QFileInfo(staging_).size() == 0) { fail("FFmpeg produced an empty file."); return; }
#ifdef Q_OS_WIN
        bool saved = MoveFileExW(reinterpret_cast<LPCWSTR>(staging_.utf16()), reinterpret_cast<LPCWSTR>(destination_.utf16()), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
        bool saved = ::rename(QFile::encodeName(staging_).constData(), QFile::encodeName(destination_).constData()) == 0;
#endif
        if (!saved) { fail("Could not move the finished clip to its destination. Check permissions and whether the file is open."); return; }
        staging_.clear(); emit exported(destination_);
    }
}
void MediaJobs::prepareResolved(const QJsonObject &info, const QJsonArray &formats, int attempt, bool rangeProbe) {
    for (int index = 0; index < formats.size(); ++index) {
        const auto format = formats[index].toObject(); const QUrl url(format.value("url").toString());
        if (QUrlQuery(url).queryItemValue("clen").toLongLong() > 0 || format.value("filesize").toDouble() > 0) continue;
        if (url.scheme() != "http" && url.scheme() != "https") { fail("Unsupported media stream URL."); return; }
        active_ = true; emit progress(-1, "Checking stream length...");
        QNetworkRequest request(url); request.setTransferTimeout(20000);
        request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        auto headers = format.value("http_headers").toObject(); if (headers.isEmpty()) headers = info.value("http_headers").toObject();
        for (auto it = headers.begin(); it != headers.end(); ++it) request.setRawHeader(it.key().toUtf8(), it.value().toString().toUtf8());
        request.setRawHeader("Accept-Encoding", "identity");
        if (rangeProbe) request.setRawHeader("Range", "bytes=0-0");
        auto *reply = rangeProbe ? probeNetwork_.get(request) : probeNetwork_.head(request);
        probe_ = reply; reply->setReadBufferSize(1024);
        const auto generation = resolveGeneration_;
        auto complete = [this, reply, info, formats, index, attempt, rangeProbe, generation](bool headersOnly) {
            if (generation != resolveGeneration_ || probe_ != reply) return;
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            qint64 size = 0;
            if (status == 206) size = reply->rawHeader("Content-Range").split('/').last().toLongLong();
            else if (status == 200) size = reply->rawHeader("Content-Length").toLongLong();
            if (headersOnly && size <= 0) return;
            const bool success = size > 0 && (headersOnly || reply->error() == QNetworkReply::NoError);
            probe_.clear(); disconnect(reply, nullptr, this, nullptr);
            if (headersOnly) reply->abort();
            reply->deleteLater();
            if (success) {
                auto updatedFormats = formats;
                auto format = formats[index].toObject();
                format.insert("filesize", double(size));
                updatedFormats[index] = format;
                prepareResolved(info, updatedFormats);
                return;
            }
            if (!rangeProbe) { prepareResolved(info, formats, attempt, true); return; }
            if (attempt < 3) {
                QTimer::singleShot(1500, this, [this, info, formats, attempt, generation] {
                    if (generation == resolveGeneration_ && active_) prepareResolved(info, formats, attempt + 1);
                });
            } else fail("Could not determine the exact media length from the server. Try Download first.");
        };
        connect(reply, &QNetworkReply::finished, this, [complete] { complete(false); });
        if (rangeProbe) connect(reply, &QNetworkReply::metaDataChanged, this, [complete] { complete(true); });
        return;
    }
    active_ = false; useResolved(info, formats);
}
void MediaJobs::useResolved(const QJsonObject &info, const QJsonArray &formats) {
    const QString videoId = info.value("id").toString();
    if (operation_ == Operation::ResolvePreview) {
        const QUrl url = bridge_.add(formats.first().toObject(), {}, videoId);
        if (url.isEmpty()) { fail("No seekable HTTP preview is available. Choose Download first."); return; }
        emit streamReady(url, qRound64(info.value("duration").toDouble() * 1000), info.value("height").toInt(), videoId);
        return;
    }
    if (formats.size() > 2) { fail("Unexpected number of YouTube media streams."); return; }
    const auto headers = info.value("http_headers").toObject();
    QList<QUrl> sources;
    for (const auto &value : formats) {
        const QUrl url = bridge_.add(value.toObject(), headers, videoId);
        if (url.isEmpty()) { fail("No seekable video/audio stream is available for this selection. Try Download first."); return; }
        sources.append(url);
    }
    startExport(sources.first(), sources.value(1));
}
