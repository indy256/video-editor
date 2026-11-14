#include "rangebridge.h"
#include <QTcpSocket>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QUuid>
#include <QTimer>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <memory>
#include <algorithm>

RangeBridge::RangeBridge(QObject *parent) : QObject(parent) {
    server_.listen(QHostAddress::LocalHost, 0);
    connect(&server_, &QTcpServer::newConnection, this, &RangeBridge::acceptConnection);
}
QUrl RangeBridge::add(const QJsonObject &format, const QJsonObject &fallbackHeaders, const QString &videoId) {
    const QUrl remote(format.value("url").toString());
    qint64 size = QUrlQuery(remote).queryItemValue("clen").toLongLong();
    if (size <= 0) size = qint64(format.value("filesize").toDouble());
    if (!server_.isListening() || size <= 0 || (remote.scheme() != "https" && remote.scheme() != "http")) return {};
    auto headers = format.value("http_headers").toObject(); if (headers.isEmpty()) headers = fallbackHeaders;
    QUrlQuery query(remote);
    QString cacheKey;
    // Signed hosts/expiry tokens rotate. The content ID, format and media version
    // identify the bytes independently of those transient access credentials.
    if (remote.host().endsWith(".googlevideo.com") && !query.queryItemValue("id").isEmpty() && !query.queryItemValue("lmt").isEmpty()) {
        // The CDN's `id` can itself rotate, so prefer the stable YouTube video ID.
        QJsonArray identity{videoId.isEmpty() ? query.queryItemValue("id") : videoId, query.queryItemValue("itag"),
            query.queryItemValue("lmt"), QString::number(size), query.queryItemValue("mime"),
            format.value("format_id"), format.value("language"), format.value("vcodec"), format.value("acodec")};
        cacheKey = QString::fromUtf8(QJsonDocument(identity).toJson(QJsonDocument::Compact));
    } else {
        // Unknown origins must change identity if any part of their URL changes.
        cacheKey = remote.toString(QUrl::FullyEncoded) + "\n" + QString::number(size);
    }
    QString key = "/" + QUuid::createUuid().toString(QUuid::Id128);
    resources_.insert(key, {remote, headers, size, cacheKey});
    return QUrl(QString("http://127.0.0.1:%1%2").arg(server_.serverPort()).arg(key));
}
void RangeBridge::acceptConnection() {
    while (server_.hasPendingConnections()) {
        auto *socket = server_.nextPendingConnection();
        auto request = std::make_shared<QByteArray>();
        auto started = std::make_shared<bool>(false);
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        QTimer::singleShot(15000, socket, [socket, started] { if (!*started) socket->disconnectFromHost(); });
        connect(socket, &QTcpSocket::readyRead, socket, [this, socket, request, started] {
            if (*started) { socket->readAll(); return; }
            *request += socket->readAll();
            if (request->size() > 16384) { socket->disconnectFromHost(); return; }
            if (!request->contains("\r\n\r\n")) return;
            *started = true;
            const auto lines = request->split('\n'); const auto words = lines.first().trimmed().split(' ');
            if (words.size() < 2 || (words[0] != "GET" && words[0] != "HEAD") || !resources_.contains(QString::fromUtf8(words[1]))) {
                socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"); socket->disconnectFromHost(); return;
            }
            const auto resource = resources_.value(QString::fromUtf8(words[1]));
            qint64 first = 0, last = resource.size - 1; bool partial = false, valid = true;
            for (auto line : lines) if (line.trimmed().toLower().startsWith("range:")) {
                partial = true; const auto range = line.mid(line.indexOf(':') + 1).trimmed();
                if (!range.startsWith("bytes=") || range.contains(',')) { valid = false; break; }
                const auto ends = range.mid(6).split('-');
                if (ends.size() != 2) { valid = false; break; }
                bool ok = false;
                if (ends[0].isEmpty()) {
                    qint64 count = ends[1].toLongLong(&ok); valid = ok && count > 0; first = std::max(qint64(0), resource.size - count);
                } else {
                    first = ends[0].toLongLong(&ok); valid = ok;
                    if (!ends[1].isEmpty()) { last = ends[1].toLongLong(&ok); valid = valid && ok; }
                    last = std::min(last, resource.size - 1);
                }
            }
            if (!valid || first < 0 || first > last || first >= resource.size) {
                socket->write("HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */" + QByteArray::number(resource.size) + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"); socket->disconnectFromHost(); return;
            }
            QByteArray header = partial ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
            header += "Accept-Ranges: bytes\r\nContent-Type: application/octet-stream\r\nContent-Length: " + QByteArray::number(last - first + 1) + "\r\n";
            if (partial) header += "Content-Range: bytes " + QByteArray::number(first) + "-" + QByteArray::number(last) + "/" + QByteArray::number(resource.size) + "\r\n";
            header += "Connection: close\r\n\r\n"; socket->write(header);
            if (words[0] == "HEAD") { socket->disconnectFromHost(); return; }
            struct Transfer { qint64 next, last, chunkStart = 0, chunkLast = 0, retryAt = 0; int retries = 0; QPointer<QNetworkReply> reply; bool finished = false; QByteArray chunk, pending; };
            auto transfer = std::make_shared<Transfer>(); transfer->next = first; transfer->last = last;
            auto *pump = new QTimer(socket); pump->setInterval(10);
            connect(pump, &QTimer::timeout, socket, [this, socket, resource, transfer] {
                if (socket->state() != QAbstractSocket::ConnectedState) return;
                if (QDateTime::currentMSecsSinceEpoch() < transfer->retryAt) return;
                if (transfer->reply) {
                    auto *reply = transfer->reply.data();
                    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                    // Fresh signed CDN URLs may need a short settling period.
                    if ((status == 403 || status >= 500) && transfer->retries++ < 4) {
                        reply->abort(); reply->deleteLater(); transfer->reply.clear();
                        transfer->chunk.clear();
                        transfer->retryAt = QDateTime::currentMSecsSinceEpoch() + 1500; return;
                    }
                    if (status >= 400) { qWarning() << "Stream range HTTP status:" << status; reply->abort(); socket->abort(); return; }
                    auto data = reply->readAll(); received_ += data.size(); transfer->chunk += data;
                    const qint64 expected = transfer->chunkLast - transfer->chunkStart + 1;
                    if (transfer->chunk.size() > expected) { reply->abort(); socket->abort(); return; }
                    if (!transfer->finished) return;
                    if (reply->error() != QNetworkReply::NoError || transfer->chunk.size() != expected) {
                        reply->deleteLater(); transfer->reply.clear(); transfer->chunk.clear();
                        if (transfer->retries++ < 4) { transfer->retryAt = QDateTime::currentMSecsSinceEpoch() + 1500; return; }
                        qWarning() << "Stream range repeatedly ended before the requested bytes arrived"; socket->abort(); return;
                    }
                    cache_.write(resource.cacheKey, transfer->chunkStart, transfer->chunk);
                    transfer->pending = transfer->chunk.mid(transfer->next - transfer->chunkStart,
                        std::min(transfer->last, transfer->chunkLast) - transfer->next + 1);
                    transfer->chunk.clear();
                    reply->deleteLater(); transfer->reply.clear(); transfer->retries = 0;
                }
                if (!transfer->pending.isEmpty()) {
                    if (socket->bytesToWrite() < 262144) {
                        const auto count = std::min(qsizetype(262144), transfer->pending.size());
                        socket->write(transfer->pending.constData(), count); transfer->pending.remove(0, count); transfer->next += count;
                    }
                    return;
                }
                if (socket->bytesToWrite() > 131072) return;
                if (transfer->next > transfer->last) { socket->disconnectFromHost(); return; }
                // YouTube may reject open-ended ranges. Query ranges also avoid
                // CDN implementations that ignore the HTTP Range header.
                transfer->chunkStart = transfer->next / ChunkCache::ChunkSize * ChunkCache::ChunkSize;
                transfer->chunkLast = std::min(resource.size - 1, transfer->chunkStart + ChunkCache::ChunkSize - 1);
                const auto cached = cache_.read(resource.cacheKey, transfer->chunkStart, transfer->chunkLast - transfer->chunkStart + 1);
                if (!cached.isEmpty()) {
                    transfer->pending = cached.mid(transfer->next - transfer->chunkStart,
                        std::min(transfer->last, transfer->chunkLast) - transfer->next + 1);
                    cached_ += transfer->pending.size(); return;
                }
                // Preserve the signed query's exact percent encoding.
                QUrl remote = QUrl::fromEncoded(resource.url.toEncoded() + "&range=" + QByteArray::number(transfer->chunkStart) + "-" + QByteArray::number(transfer->chunkLast));
                QNetworkRequest request(remote); request.setTransferTimeout(20000);
                request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
                request.setRawHeader("Accept-Encoding", "identity");
                request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
                for (auto it = resource.headers.begin(); it != resource.headers.end(); ++it) request.setRawHeader(it.key().toUtf8(), it.value().toString().toUtf8());
                // Do not combine a query range with an HTTP Range header: the
                // CDN applies that header *within* the already sliced resource.
                auto *reply = network_.get(request); transfer->reply = reply; transfer->finished = false; reply->setReadBufferSize(262144);
                connect(reply, &QNetworkReply::finished, socket, [transfer] { transfer->finished = true; });
                connect(socket, &QTcpSocket::disconnected, reply, [reply] { reply->abort(); reply->deleteLater(); });
            });
            pump->start();
        });
    }
}
