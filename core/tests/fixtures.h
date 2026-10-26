// core/tests/fixtures.h -- small filesystem helpers shared by the core tests.
#pragma once

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

namespace fx {

inline bool put(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    return f.write(bytes) == bytes.size();
}

inline QByteArray get(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QByteArray("<unreadable>");
    return f.readAll();
}

inline bool exists(const QString &path) {
    const QFileInfo fi(path);
    return fi.exists() || fi.isSymLink();
}

// Every name directly inside dir, hidden ones included, sorted.
inline QStringList names(const QString &dir) {
    return QDir(dir).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden |
                                   QDir::System,
                               QDir::Name);
}

inline QByteArray pattern(qint64 size) {
    QByteArray b(qsizetype(size), '\0');
    for (qint64 i = 0; i < size; ++i) b[qsizetype(i)] = char('a' + (i * 7) % 26);
    return b;
}

}  // namespace fx
