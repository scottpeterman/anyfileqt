// anyfileqt/core/src/fsinfo.cpp
#include "anyfile/fsinfo.h"

#include <QCoreApplication>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QPointer>

#include <cmath>
#include <thread>

#ifndef Q_OS_WIN
#include <sys/stat.h>
#endif

namespace anyfile {

QString humanSize(qint64 bytes) {
    const QLocale loc;
    if (bytes < 1024) return QStringLiteral("%1 bytes").arg(loc.toString(qlonglong(qMax<qint64>(0, bytes))));
    static const char *units[] = {"KB", "MB", "GB", "TB", "PB", "EB"};
    double v = double(bytes) / 1024.0;
    int u = 0;
    while (v >= 1024.0 && u < 5) {
        v /= 1024.0;
        ++u;
    }
    // Three significant figures, truncated: 1.769 -> 1.76, 17.69 -> 17.6.
    const int decimals = v < 10 ? 2 : v < 100 ? 1 : 0;
    const double scale = std::pow(10.0, decimals);
    // The epsilon keeps 1.20 from printing as 1.19 when 1.2 is not exact in
    // binary.
    const double t = std::floor(v * scale + 1e-9) / scale;
    return QStringLiteral("%1 %2").arg(loc.toString(t, 'f', decimals), QLatin1String(units[u]));
}

QString columnSize(qint64 bytes) {
    const qint64 kb = bytes <= 0 ? 0 : (bytes + 1023) / 1024;
    return QStringLiteral("%1 KB").arg(QLocale().toString(qlonglong(kb)));
}

QString sizeWithBytes(qint64 bytes) {
    if (bytes < 1024) return humanSize(bytes);
    return QStringLiteral("%1 (%2 bytes)").arg(humanSize(bytes), QLocale().toString(qlonglong(bytes)));
}

qint64 sizeOnDisk(const QString &path) {
#ifdef Q_OS_WIN
    Q_UNUSED(path);
    return -1;
#else
    struct stat st;
    if (::lstat(QFile::encodeName(path).constData(), &st) != 0) return -1;
    return qint64(st.st_blocks) * 512;
#endif
}

QString modeString(QFileDevice::Permissions p, bool isDir, bool isLink) {
    QString s(10, QLatin1Char('-'));
    s[0] = isLink ? QLatin1Char('l') : isDir ? QLatin1Char('d') : QLatin1Char('-');
    const QFileDevice::Permission bits[9] = {
        QFileDevice::ReadOwner, QFileDevice::WriteOwner, QFileDevice::ExeOwner,
        QFileDevice::ReadGroup, QFileDevice::WriteGroup, QFileDevice::ExeGroup,
        QFileDevice::ReadOther, QFileDevice::WriteOther, QFileDevice::ExeOther};
    const char letters[3] = {'r', 'w', 'x'};
    for (int i = 0; i < 9; ++i)
        if (p & bits[i]) s[i + 1] = QLatin1Char(letters[i % 3]);
    return s;
}

QString modeOctal(QFileDevice::Permissions p) {
    const auto digit = [p](QFileDevice::Permission r, QFileDevice::Permission w,
                           QFileDevice::Permission x) {
        return ((p & r) ? 4 : 0) + ((p & w) ? 2 : 0) + ((p & x) ? 1 : 0);
    };
    return QStringLiteral("%1%2%3")
        .arg(digit(QFileDevice::ReadOwner, QFileDevice::WriteOwner, QFileDevice::ExeOwner))
        .arg(digit(QFileDevice::ReadGroup, QFileDevice::WriteGroup, QFileDevice::ExeGroup))
        .arg(digit(QFileDevice::ReadOther, QFileDevice::WriteOther, QFileDevice::ExeOther));
}

// --- SizeScanner ---------------------------------------------------------------

SizeScanner::SizeScanner(QObject *parent) : QObject(parent) {
    qRegisterMetaType<anyfile::SizeTotals>();
}

SizeScanner::~SizeScanner() { cancel(); }

void SizeScanner::cancel() {
    if (m_cancel) m_cancel->store(true);
}

void SizeScanner::start(const QStringList &paths) {
    cancel();
    m_totals = {};
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    auto stop = m_cancel;
    QPointer<SizeScanner> self(this);

    std::thread([paths, stop, self] {
        SizeTotals t;
        QElapsedTimer clock;
        clock.start();
        qint64 lastPost = 0;

        const auto post = [&](const SizeTotals &snapshot) {
            QCoreApplication *app = QCoreApplication::instance();
            if (!app || stop->load()) return;
            QMetaObject::invokeMethod(
                app,
                [self, snapshot, stop] {
                    if (self && !stop->load()) self->apply(snapshot);
                },
                Qt::QueuedConnection);
        };
        const auto addFile = [&](const QFileInfo &fi) {
            ++t.files;
            if (fi.isSymLink()) return;  // the link, not what it points at
            t.bytes += fi.size();
            const qint64 d = sizeOnDisk(fi.filePath());
            if (d < 0)
                t.onDiskKnown = false;
            else
                t.onDisk += d;
        };

        for (const QString &p : paths) {
            if (stop->load()) return;
            const QFileInfo top(p);
            if (top.isSymLink() || !top.isDir()) {
                if (top.exists() || top.isSymLink()) addFile(top);
                continue;
            }
            if (paths.size() > 1) ++t.folders;
            if (!top.isReadable()) {
                ++t.unreadable;
                continue;
            }
            // No FollowSymlinks: a symlinked directory is listed, not entered.
            QDirIterator it(p, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                if (stop->load()) return;
                it.next();
                const QFileInfo fi = it.fileInfo();
                if (fi.isDir() && !fi.isSymLink()) {
                    ++t.folders;
                    if (!fi.isReadable()) ++t.unreadable;
                } else {
                    addFile(fi);
                }
                if (clock.elapsed() - lastPost >= 100) {
                    lastPost = clock.elapsed();
                    post(t);
                }
            }
        }
        t.done = true;
        post(t);
    }).detach();
}

void SizeScanner::apply(const SizeTotals &t) {
    m_totals = t;
    emit updated(m_totals);
}

}  // namespace anyfile
