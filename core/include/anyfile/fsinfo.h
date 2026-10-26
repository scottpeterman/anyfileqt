// anyfileqt/core/include/anyfile/fsinfo.h
//
// Sizes the way Explorer prints them, the facts a Properties dialog shows, and
// a background size counter for folders.
//
// ONE FORMATTER, used by every label in the application. Before this the Size
// column said "1.77 KiB" (Qt's) and the status bar said "1.8 kB" (another of
// Qt's), for the same file.
#pragma once

#include <QFileDevice>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>

#include <atomic>
#include <memory>

namespace anyfile {

// "812 bytes", "1.76 KB", "17.6 KB", "176 KB", "1.20 MB" -- three significant
// figures, 1024-based, TRUNCATED rather than rounded, as Explorer does it: a
// 1,023-byte file never shows as "1.00 KB".
QString humanSize(qint64 bytes);

// The details-view Size column: always whole KB, rounded UP, with thousands
// separators -- "0 KB", "1 KB", "4,883 KB". Explorer never switches the column
// to MB, so a column of sizes lines up and sorts by eye.
QString columnSize(qint64 bytes);

// "1.76 KB (1,812 bytes)", Properties' Size line.
QString sizeWithBytes(qint64 bytes);

// Allocated size, or -1 when the platform does not say. Unix: st_blocks * 512
// from lstat(). Windows: -1 for now; the honest number needs the volume's
// cluster size as well.
qint64 sizeOnDisk(const QString &path);

// "drwxr-x---" and "750". Only the nine permission bits Qt models; setuid,
// setgid and sticky are not in QFileDevice::Permissions.
QString modeString(QFileDevice::Permissions p, bool isDir, bool isLink);
QString modeOctal(QFileDevice::Permissions p);

struct SizeTotals {
    qint64 bytes = 0;
    qint64 onDisk = 0;
    bool onDiskKnown = true;
    int files = 0;
    int folders = 0;
    int unreadable = 0;
    bool done = false;
};

// Counts files, folders and bytes under a set of paths on a worker thread,
// reporting as it goes -- Explorer's Properties counts up live, and a large
// tree on a network share takes long enough that a dialog which waited for
// the total would look hung.
//
// Symlinks are counted as one file of size zero and never followed, so a link
// to / does not count the whole disk. Selected folders count toward "folders"
// only when more than one path was given: for a single folder, Explorer's
// "Contains" is what is inside it.
//
// The worker is a detached std::thread that posts to the application object
// and checks a QPointer there, the same arrangement as VolumeMonitor: a count
// stuck on a dead mount can outlive this object, and the dialog that owns it
// must still close.
class SizeScanner : public QObject {
    Q_OBJECT
public:
    explicit SizeScanner(QObject *parent = nullptr);
    ~SizeScanner() override;

    void start(const QStringList &paths);
    void cancel();
    const SizeTotals &totals() const { return m_totals; }

signals:
    void updated(const anyfile::SizeTotals &totals);

private:
    void apply(const SizeTotals &t);

    SizeTotals m_totals;
    std::shared_ptr<std::atomic<bool>> m_cancel;
};

}  // namespace anyfile

Q_DECLARE_METATYPE(anyfile::SizeTotals)
