// app/fsproxy.cpp
#include "app/fsproxy.h"

#include <QFileSystemModel>
#include <QMimeData>
#include <QUrl>

#include "anyfile/fsinfo.h"
#include "anyfile/naming.h"
#include "app/filetypes.h"

namespace anyfile {

namespace {
// QFileSystemModel's columns.
enum Col { Name = 0, Size = 1, Type = 2, Modified = 3 };
}  // namespace

FsProxyModel::FsProxyModel(QObject *parent) : QSortFilterProxyModel(parent) {
    setDynamicSortFilter(true);
}

void FsProxyModel::setFileSystemModel(QFileSystemModel *fs) {
    m_fs = fs;
    m_root = QPersistentModelIndex();
    setSourceModel(fs);
}

void FsProxyModel::setRoot(const QModelIndex &sourceRoot) {
    m_root = sourceRoot;
    if (!m_filter.isEmpty()) invalidateFilter();
}

void FsProxyModel::setNameFilter(const QString &text) {
    if (text == m_filter) return;
    m_filter = text;
    invalidateFilter();
}

QString FsProxyModel::dirForDrop(const QModelIndex &proxyIndex) const {
    if (!m_fs) return {};
    QModelIndex src = proxyIndex.isValid() ? mapToSource(proxyIndex) : QModelIndex(m_root);
    if (src.isValid() && !m_fs->isDir(src)) src = src.parent();
    // Invalid here is Windows' This PC: no folder to drop into.
    return src.isValid() ? m_fs->filePath(src) : QString();
}

QVariant FsProxyModel::data(const QModelIndex &index, int role) const {
    if (m_fs && role == Qt::DisplayRole && (index.column() == Size || index.column() == Type)) {
        const QModelIndex src = mapToSource(index);
        if (index.column() == Size) return m_fs->isDir(src) ? QString() : columnSize(m_fs->size(src));
        return friendlyType(m_fs->fileInfo(src));
    }
    return QSortFilterProxyModel::data(index, role);
}

QVariant FsProxyModel::headerData(int section, Qt::Orientation o, int role) const {
    // Explorer's wording. QFileSystemModel says "Date Modified" everywhere and
    // "Kind" on macOS.
    if (o == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
        case Name: return tr("Name");
        case Size: return tr("Size");
        case Type: return tr("Type");
        case Modified: return tr("Date modified");
        default: break;
        }
    }
    return QSortFilterProxyModel::headerData(section, o, role);
}

bool FsProxyModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    // QFileSystemModel renames on disk in setData and, in Qt 6, reports a
    // failure only by returning false -- the message box it showed in Qt 5
    // went when the model moved to QtGui. Without this a refused rename just
    // snaps back to the old name and says nothing.
    const QString oldName = index.data(Qt::DisplayRole).toString();
    const bool ok = QSortFilterProxyModel::setData(index, value, role);
    if (!ok && role == Qt::EditRole && value.toString() != oldName)
        emit renameFailed(oldName, value.toString());
    return ok;
}

Qt::DropActions FsProxyModel::supportedDropActions() const {
    return Qt::CopyAction | Qt::MoveAction | Qt::LinkAction;
}

bool FsProxyModel::canDropMimeData(const QMimeData *data, Qt::DropAction, int, int,
                                   const QModelIndex &parent) const {
    return data && data->hasUrls() && !dirForDrop(parent).isEmpty();
}

bool FsProxyModel::dropMimeData(const QMimeData *data, Qt::DropAction, int, int,
                                const QModelIndex &parent) {
    if (!data || !data->hasUrls()) return false;
    const QString dir = dirForDrop(parent);
    if (dir.isEmpty()) return false;
    QStringList paths;
    for (const QUrl &u : data->urls())
        if (u.isLocalFile()) paths.append(u.toLocalFile());
    if (paths.isEmpty()) return false;
    // Returned true, and the drag source -- if it is one of our own views --
    // may then call removeRows() for a move. QFileSystemModel does not
    // implement removeRows, so that is a no-op; the files leave the view when
    // the job moves them and the watcher sees it.
    emit dropRequested(paths, dir);
    return true;
}

bool FsProxyModel::lessThan(const QModelIndex &l, const QModelIndex &r) const {
    if (!m_fs) return QSortFilterProxyModel::lessThan(l, r);
    const bool ld = m_fs->isDir(l), rd = m_fs->isDir(r);
    if (ld != rd) return ld;

    switch (l.column()) {
    case Size: {
        const qint64 a = m_fs->size(l), b = m_fs->size(r);
        if (a != b) return a < b;
        break;
    }
    case Type: {
        // The text on screen, so the sort agrees with what is displayed.
        const int c = naturalCompare(friendlyType(m_fs->fileInfo(l)), friendlyType(m_fs->fileInfo(r)));
        if (c != 0) return c < 0;
        break;
    }
    case Modified: {
        const QDateTime a = m_fs->lastModified(l), b = m_fs->lastModified(r);
        if (a != b) return a < b;
        break;
    }
    default: break;
    }
    // Ties in every other column fall back to the name, as Explorer does.
    return naturalCompare(m_fs->fileName(l), m_fs->fileName(r)) < 0;
}

bool FsProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const {
    if (m_filter.isEmpty() || !m_fs) return true;
    if (sourceParent != QModelIndex(m_root)) return true;  // ancestors and their siblings
    const QString name = m_fs->fileName(m_fs->index(sourceRow, 0, sourceParent));
    return name.contains(m_filter, Qt::CaseInsensitive);
}

}  // namespace anyfile
