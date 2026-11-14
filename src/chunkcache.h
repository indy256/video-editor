#pragma once
#include <QByteArray>
#include <QString>

class ChunkCache {
public:
    static constexpr qint64 ChunkSize = 2 * 1024 * 1024;
    explicit ChunkCache(const QString &directory = {}, qint64 limit = 2LL * 1024 * 1024 * 1024);
    QByteArray read(const QString &resource, qint64 offset, qint64 expectedSize);
    void write(const QString &resource, qint64 offset, const QByteArray &data);
    QString directory() const { return directory_; }
private:
    QString path(const QString &resource, qint64 offset) const;
    QString directory_;
    qint64 limit_;
};
