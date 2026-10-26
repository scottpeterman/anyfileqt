// app/navpane.cpp
#include "app/navpane.h"

#include <QDir>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QHeaderView>
#include <QLocale>
#include <QSet>
#include <QStandardPaths>

#include "anyfile/fsinfo.h"

namespace anyfile {

namespace {

constexpr int kPathRole = Qt::UserRole + 1;
constexpr int kNavigableRole = Qt::UserRole + 2;

bool isNetworkFs(const QString &fs) {
    static const QSet<QString> net = {"nfs", "nfs4", "cifs", "smbfs", "smb3", "afpfs",
                                      "webdav", "davfs", "fuse.sshfs", "fuse.gvfsd-fuse",
                                      "9p"};
    return net.contains(fs.toLower());
}


}  // namespace

NavPane::NavPane(QWidget *parent) : QTreeWidget(parent) {
    setHeaderHidden(true);
    setRootIsDecorated(true);
    setIndentation(14);
    setUniformRowHeights(true);
    setExpandsOnDoubleClick(false);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setFocusPolicy(Qt::ClickFocus);
    setMinimumWidth(150);

    QFileIconProvider icons;
    const QFont bold = [this] {
        QFont f = font();
        f.setBold(true);
        return f;
    }();

    m_quick = new QTreeWidgetItem(this, {tr("Quick access")});
    m_quick->setFont(0, bold);
    m_quick->setFlags(Qt::ItemIsEnabled);

    // In Explorer's order, each one only if it exists. On Linux several of
    // these resolve to $HOME when the XDG user-dirs are not set, and listing
    // Home five times is worse than listing it once.
    const QList<QPair<QString, QStandardPaths::StandardLocation>> places = {
        {tr("Home"), QStandardPaths::HomeLocation},
        {tr("Desktop"), QStandardPaths::DesktopLocation},
        {tr("Documents"), QStandardPaths::DocumentsLocation},
        {tr("Downloads"), QStandardPaths::DownloadLocation},
        {tr("Pictures"), QStandardPaths::PicturesLocation},
        {tr("Music"), QStandardPaths::MusicLocation},
        {tr("Videos"), QStandardPaths::MoviesLocation},
    };
    QSet<QString> seen;
    for (const auto &p : places) {
        const QString path = QStandardPaths::writableLocation(p.second);
        if (path.isEmpty() || seen.contains(path) || !QFileInfo(path).isDir()) continue;
        seen.insert(path);
        auto *it = new QTreeWidgetItem(m_quick, {p.first});
        it->setIcon(0, icons.icon(QFileIconProvider::Folder));
        it->setData(0, kPathRole, path);
        it->setData(0, kNavigableRole, true);
        it->setToolTip(0, QDir::toNativeSeparators(path));
    }

#ifdef Q_OS_WIN
    m_pc = new QTreeWidgetItem(this, {tr("This PC")});
    m_pc->setData(0, kPathRole, QString());
    m_pc->setData(0, kNavigableRole, true);
    m_pc->setIcon(0, icons.icon(QFileIconProvider::Computer));
#else
    m_pc = new QTreeWidgetItem(this, {tr("Locations")});
    m_pc->setFlags(Qt::ItemIsEnabled);
#endif
    m_pc->setFont(0, bold);

    expandAll();
    connect(this, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *i) { onItem(i); });
    connect(this, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *i) { onItem(i); });
}

void NavPane::onItem(QTreeWidgetItem *item) {
    if (!item || !item->data(0, kNavigableRole).toBool()) return;
    emit navigate(item->data(0, kPathRole).toString());
}

void NavPane::setVolumes(const QList<VolumeInfo> &volumes) {
    // Rebuilt rather than diffed: a handful of rows, and the volume monitor
    // only calls this when something actually changed.
    const QString selected =
        currentItem() && currentItem()->parent() == m_pc ? currentItem()->data(0, kPathRole).toString()
                                                         : QString();
    qDeleteAll(m_pc->takeChildren());

    QFileIconProvider icons;
    for (const VolumeInfo &v : volumes) {
        auto *it = new QTreeWidgetItem(m_pc, {v.name});
        it->setIcon(0, icons.icon(isNetworkFs(v.fsType) ? QFileIconProvider::Network
                                                        : QFileIconProvider::Drive));
        it->setData(0, kPathRole, v.root);
        it->setData(0, kNavigableRole, true);
        it->setToolTip(0, tr("%1\n%2 free of %3  (%4)")
                              .arg(QDir::toNativeSeparators(v.root), humanSize(v.bytesFree),
                                   humanSize(v.bytesTotal), v.fsType));
        if (!selected.isEmpty() && v.root == selected) setCurrentItem(it);
    }
    m_pc->setExpanded(true);
}

QStringList NavPane::volumeRoots() const {
    QStringList out;
    for (int i = 0; i < m_pc->childCount(); ++i)
        out.append(m_pc->child(i)->data(0, kPathRole).toString());
    return out;
}

}  // namespace anyfile
