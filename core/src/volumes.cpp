// core/src/volumes.cpp
#include "anyfile/volumes.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QSet>
#include <QStorageInfo>
#include <QTimer>

#include <algorithm>
#include <thread>

namespace anyfile {

namespace {

constexpr Qt::CaseSensitivity kPathCase =
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    Qt::CaseInsensitive;
#else
    Qt::CaseSensitive;
#endif

#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
bool keepLinux(const QString &root, const QString &fs) {
    if (root == QLatin1String("/")) return true;

    static const QSet<QString> pseudo = {
        "proc", "sysfs", "devtmpfs", "devpts", "tmpfs", "ramfs", "cgroup", "cgroup2",
        "pstore", "bpf", "securityfs", "debugfs", "tracefs", "configfs", "fusectl",
        "mqueue", "hugetlbfs", "autofs", "overlay", "squashfs", "nsfs", "efivarfs",
        "binfmt_misc", "rpc_pipefs", "nfsd", "fuse.portal", "fuse.lxcfs",
        "fuse.snapfuse", "selinuxfs", "zfs_snapshot"};
    if (pseudo.contains(fs)) return false;

    if (root.startsWith(QLatin1String("/run/media/"))) return true;
    if (root.startsWith(QLatin1String("/run/user/")) && root.endsWith(QLatin1String("/gvfs")))
        return true;
    for (const char *p : {"/proc", "/sys", "/dev", "/run", "/snap", "/boot", "/var/lib/docker",
                          "/var/snap"}) {
        const QString pre = QString::fromLatin1(p);
        if (root == pre || root.startsWith(pre + QLatin1Char('/'))) return false;
    }
    return true;
}
#endif

bool keep(const QString &root, const QString &fs) {
#if defined(Q_OS_MACOS)
    Q_UNUSED(fs);
    return root == QLatin1String("/") ||
           (root.startsWith(QLatin1String("/Volumes/")) && root.count(QLatin1Char('/')) == 2);
#elif defined(Q_OS_WIN)
    Q_UNUSED(root);
    Q_UNUSED(fs);
    return true;
#else
    return keepLinux(root, fs);
#endif
}

QString displayName(const QStorageInfo &s, const QString &root) {
    const QString label = s.name();
#if defined(Q_OS_WIN)
    // "Windows (C:)", or "Local Disk (C:)" for an unlabelled one -- what
    // Explorer shows. A volume mounted on a folder has no letter; show the folder.
    if (root.size() == 3 && root.at(1) == QLatin1Char(':')) {
        return QStringLiteral("%1 (%2)")
            .arg(label.isEmpty() ? QStringLiteral("Local Disk") : label, root.left(2));
    }
    return label.isEmpty() ? QDir::toNativeSeparators(root) : label;
#else
    if (!label.isEmpty()) return label;
    if (root == QLatin1String("/")) return QStringLiteral("File System");
    const QString base = QFileInfo(root).fileName();
    return base.isEmpty() ? root : base;
#endif
}

}  // namespace

QList<VolumeInfo> scanVolumes() {
    QList<VolumeInfo> out;
    const QList<QStorageInfo> all = QStorageInfo::mountedVolumes();
    for (const QStorageInfo &s : all) {
        if (!s.isValid() || !s.isReady()) continue;
        const QString root = QDir::fromNativeSeparators(s.rootPath());
        const QString fs = QString::fromLatin1(s.fileSystemType());
        if (!keep(root, fs)) continue;

        VolumeInfo v;
        v.root = root;
        v.fsType = fs;
        v.bytesTotal = s.bytesTotal();
        v.bytesFree = s.bytesAvailable();
        v.readOnly = s.isReadOnly();
        v.name = displayName(s, root);
        out.append(v);
    }
    std::sort(out.begin(), out.end(), [](const VolumeInfo &a, const VolumeInfo &b) {
        if (a.root == QLatin1String("/")) return b.root != QLatin1String("/");
        if (b.root == QLatin1String("/")) return false;
        return a.root.compare(b.root, Qt::CaseInsensitive) < 0;
    });
    return out;
}

const VolumeInfo *volumeFor(const QList<VolumeInfo> &volumes, const QString &path) {
    const QString p = QDir::cleanPath(QDir::fromNativeSeparators(path));
    const VolumeInfo *best = nullptr;
    for (const VolumeInfo &v : volumes) {
        const QString r = v.root.endsWith(QLatin1Char('/')) ? v.root : v.root + QLatin1Char('/');
        const bool match = p.compare(QDir::cleanPath(v.root), kPathCase) == 0 ||
                           (p + QLatin1Char('/')).startsWith(r, kPathCase);
        if (match && (!best || v.root.size() > best->root.size())) best = &v;
    }
    return best;
}

VolumeMonitor::VolumeMonitor(int intervalMs, QObject *parent) : QObject(parent) {
    qRegisterMetaType<QList<anyfile::VolumeInfo>>();
    m_timer = new QTimer(this);
    m_timer->setInterval(intervalMs);
    connect(m_timer, &QTimer::timeout, this, &VolumeMonitor::refresh);
    m_timer->start();
    refresh();
}

VolumeMonitor::~VolumeMonitor() = default;

void VolumeMonitor::refresh() {
    if (m_scanning) return;  // the last one is still stuck in a statfs()
    m_scanning = true;
    QPointer<VolumeMonitor> self(this);
    // A detached std::thread rather than a QThread. A scan blocked on a dead
    // NFS server can outlive this object and the whole application, and a
    // QThread destroyed while running aborts the process on the way out. A
    // detached thread is simply abandoned when the process exits.
    std::thread([self] {
        QList<VolumeInfo> v = scanVolumes();
        QCoreApplication *app = QCoreApplication::instance();
        if (!app) return;
        // Delivered through the application object, which outlives every
        // monitor; the QPointer is checked on the UI thread, where it is safe
        // to check.
        QMetaObject::invokeMethod(
            app,
            [self, v] {
                if (self) self->apply(v);
            },
            Qt::QueuedConnection);
    }).detach();
}

void VolumeMonitor::apply(const QList<VolumeInfo> &v) {
    m_scanning = false;
    if (v == m_volumes) return;
    m_volumes = v;
    emit volumesChanged(m_volumes);
}

}  // namespace anyfile
