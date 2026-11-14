#include "chunkcache.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>

ChunkCache::ChunkCache(const QString &directory, qint64 limit) : directory_(directory), limit_(limit) {
    if (directory_.isEmpty()) directory_ = qEnvironmentVariable("VIDEO_EDITOR_CACHE_DIR");
    if (directory_.isEmpty()) directory_ = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/stream-chunks-v1";
    directory_ = QDir(directory_).absolutePath();
    QDir().mkpath(directory_);
}
QString ChunkCache::path(const QString &resource, qint64 offset) const {
    // Hash even caller-provided identities so filenames always stay inside the cache.
    const auto hash = QCryptographicHash::hash(resource.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QDir(directory_).filePath(QString::fromLatin1(hash) + "-" + QString::number(offset) + ".chunk");
}
QByteArray ChunkCache::read(const QString &resource, qint64 offset, qint64 expectedSize) {
    QFile file(path(resource, offset));
    if (!file.open(QIODevice::ReadWrite) && !file.open(QIODevice::ReadOnly)) return {};
    if (expectedSize <= 0 || expectedSize > ChunkSize || file.size() != expectedSize + 32) {
        file.close(); file.remove(); return {};
    }
    const auto digest = file.read(32), data = file.readAll();
    if (data.size() != expectedSize || QCryptographicHash::hash(data, QCryptographicHash::Sha256) != digest) {
        file.close(); file.remove(); return {};
    }
    // Modification times implement least-recently-used eviction across restarts.
    file.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileModificationTime);
    return data;
}
void ChunkCache::write(const QString &resource, qint64 offset, const QByteArray &data) {
    const qint64 bytes = data.size() + 32;
    if (data.isEmpty() || data.size() > ChunkSize || bytes > limit_) return;
    QLockFile lock(QDir(directory_).filePath("cache.lock"));
    if (!lock.tryLock(0)) return; // A cache miss or unwritable disk never blocks playback.
    const QString destination = path(resource, offset);
    auto files = QDir(directory_).entryInfoList({"*.chunk"}, QDir::Files, QDir::Time | QDir::Reversed);
    qint64 used = 0;
    for (const auto &file : files) if (file.absoluteFilePath() != destination) used += file.size();
    for (const auto &file : files) {
        if (used + bytes <= limit_) break;
        if (file.absoluteFilePath() != destination && QFile::remove(file.absoluteFilePath())) used -= file.size();
    }
    if (used + bytes > limit_) return;
    QSaveFile output(destination);
    const auto digest = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
    if (!output.open(QIODevice::WriteOnly) || output.write(digest) != digest.size() || output.write(data) != data.size()) return;
    if (!output.commit()) return;
}
