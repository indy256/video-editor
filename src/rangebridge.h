#pragma once
#include <QTcpServer>
#include <QNetworkAccessManager>
#include <QJsonObject>
#include <QHash>
#include "chunkcache.h"

// A loopback-only HTTP bridge. Google CDN requests are bounded, while players
// see an ordinary seekable file. Socket/reply backpressure bounds memory use.
class RangeBridge : public QObject {
    Q_OBJECT
public:
    explicit RangeBridge(QObject *parent = nullptr);
    QUrl add(const QJsonObject &format, const QJsonObject &fallbackHeaders = {}, const QString &videoId = {});
    qint64 bytesReceived() const { return received_; }
    qint64 cachedBytesServed() const { return cached_; }
private:
    struct Resource { QUrl url; QJsonObject headers; qint64 size; QString cacheKey; };
    ChunkCache cache_;
    QTcpServer server_;
    QNetworkAccessManager network_;
    QHash<QString, Resource> resources_;
    qint64 received_ = 0;
    qint64 cached_ = 0;
    void acceptConnection();
};
