#pragma once
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QUrl>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QNetworkReply>
#include "rangebridge.h"
#include <memory>
#include <vector>

QString findTool(const QString &name);
bool isYouTubeUrl(const QString &text);
enum class ExportMode { H264Mp4 = 0, H265Mp4 = 1, OriginalStreams = 2 };

class MediaJobs : public QObject {
    Q_OBJECT
public:
    explicit MediaJobs(QObject *parent = nullptr);
    ~MediaJobs() override;
    bool busy() const { return process_.state() != QProcess::NotRunning || active_; }
    qint64 streamedBytes() const { return bridge_.bytesReceived(); }
    qint64 cachedBytesServed() const { return bridge_.cachedBytesServed(); }
    void download(const QString &url);
    void resolveStream(const QString &url);
    void exportYouTube(const QString &url, qint64 start, qint64 end, const QString &destination, ExportMode mode = ExportMode::H264Mp4);
    void exportClip(const QString &source, qint64 start, qint64 end, const QString &destination, ExportMode mode = ExportMode::H264Mp4);
    bool copiesOriginalStreams() const { return exportMode_ == ExportMode::OriginalStreams; }
    qint64 actualExportStart() const { return actualStartUs_ / 1000; }
    void cancel();
signals:
    void progress(int percent, const QString &message);
    void downloaded(const QString &path, const QString &videoId);
    void streamReady(const QUrl &url, qint64 duration, int height, const QString &videoId);
    void exported(const QString &path);
    void failed(const QString &message);
    void canceled();
private:
    enum class Operation { Idle, Download, ResolvePreview, ResolveExport, FindKeyframe, Export };
    void startProcess(Operation operation, const QString &tool, const QStringList &arguments, const QString &message, int percent = -1);
    void discardOutput();
    void readOutput();
    void finish(int code, QProcess::ExitStatus status);
    void fail(const QString &message);
    void resolve(const QString &url, bool forExport);
    void prepareResolved(const QJsonObject &info, const QJsonArray &formats, int attempt = 0, bool rangeProbe = false);
    void useResolved(const QJsonObject &info, const QJsonArray &formats);
    bool prepareExport(qint64 start, qint64 end, const QString &destination, ExportMode mode);
    void startExport(const QUrl &video, const QUrl &audio = {});
    void writeOutput();
    QProcess process_;
    RangeBridge bridge_;
    QNetworkAccessManager probeNetwork_;
    QPointer<QNetworkReply> probe_;
    unsigned resolveGeneration_ = 0;
    std::unique_ptr<QTemporaryDir> pendingCache_;
    std::vector<std::unique_ptr<QTemporaryDir>> downloadCaches_;
    Operation operation_ = Operation::Idle;
    QString destination_, staging_, log_, lines_;
    QByteArray metadata_;
    qint64 exportStart_ = 0;
    qint64 exportDuration_ = 0;
    qint64 actualStartUs_ = 0;
    ExportMode exportMode_ = ExportMode::H264Mp4;
    QUrl exportVideo_, exportAudio_;
    bool active_ = false, canceled_ = false;
};
