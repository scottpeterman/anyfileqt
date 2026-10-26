// core/include/anyfile/volumes.h
//
// The drives and volumes the navigation pane lists under This PC, and the
// free-space figure in each pane's status bar.
//
// OFF THE UI THREAD, ALWAYS. QStorageInfo::mountedVolumes() calls statfs() on
// every mount, and statfs() on an NFS or SMB mount whose server has gone away
// blocks -- for the mount's timeout, which on a hard NFS mount is forever. On
// the UI thread that is a frozen window the moment a VPN drops. VolumeMonitor
// scans on a worker, and if a scan is still stuck when the next one is due it
// does not start another: it keeps showing the last good list and waits.
//
// Filtering, by platform:
//   macOS    "/" and /Volumes/* only. Everything under /System/Volumes is the
//            sealed system's plumbing (Data, VM, Preboot, Update...).
//   Linux    "/" always, then anything that is not a pseudo or container
//            filesystem and is not under /proc, /sys, /dev, /run or /snap --
//            except /run/media and /run/user/<uid>/gvfs, where removable
//            drives and GNOME's network mounts appear.
//   Windows  every valid volume, which there means drive letters and
//            folder mount points.
#pragma once

#include <QList>
#include <QObject>
#include <QString>

class QTimer;

namespace anyfile {

struct VolumeInfo {
    QString root;         // absolute, '/'-separated
    QString name;         // what the navigation pane shows
    QString fsType;
    qint64 bytesTotal = 0;
    qint64 bytesFree = 0;  // available to this user, not the raw free count
    bool readOnly = false;

    bool operator==(const VolumeInfo &o) const {
        return root == o.root && name == o.name && fsType == o.fsType &&
               readOnly == o.readOnly && bytesTotal == o.bytesTotal &&
               bytesFree == o.bytesFree;
    }
};

// Blocking. Call it from a worker.
QList<VolumeInfo> scanVolumes();

// The volume that holds path: the longest root that prefixes it. Pure string
// work over a list already scanned, so it is safe on the UI thread.
const VolumeInfo *volumeFor(const QList<VolumeInfo> &volumes, const QString &path);

class VolumeMonitor : public QObject {
    Q_OBJECT
public:
    explicit VolumeMonitor(int intervalMs = 5000, QObject *parent = nullptr);
    ~VolumeMonitor() override;

    const QList<VolumeInfo> &volumes() const { return m_volumes; }
    void refresh();

signals:
    // Emitted only when the list differs from the last one.
    void volumesChanged(const QList<anyfile::VolumeInfo> &volumes);

private:
    void apply(const QList<VolumeInfo> &v);

    QTimer *m_timer = nullptr;
    QList<VolumeInfo> m_volumes;
    bool m_scanning = false;
};

}  // namespace anyfile

Q_DECLARE_METATYPE(anyfile::VolumeInfo)
