// app/fsproxy.h
//
// The sort/filter layer between a QFileSystemModel and the two views of one
// pane. It exists for four things QFileSystemModel does not do the Explorer
// way:
//
//   FOLDERS FIRST ON EVERY PLATFORM. QFileSystemModel's own sort puts
//   directories first everywhere except macOS, where it follows Finder and
//   mixes them in. This sorts folders first on all three. As in Explorer, a
//   descending sort reverses the groups too.
//
//   NATURAL ORDER. file2 before file10 -- Explorer's StrCmpLogicalW
//   ordering, through naturalCompare() in core/naming.h.
//
//   SEARCH THAT FILTERS ONLY THE FOLDER ON SCREEN. A QSortFilterProxyModel
//   filter applies to every row at every depth, including the ancestors of the
//   folder being shown -- so a naive "name contains x" filter removes the row
//   for /home, and with it the view's root index, and the view goes blank. The
//   filter here applies only to the children of the root.
//
//   DROPS GO TO THE OPERATION QUEUE. With setReadOnly(false), which renaming
//   needs, QFileSystemModel's own dropMimeData copies or moves synchronously on
//   the UI thread, with no progress and no way to cancel. dropMimeData() here
//   never forwards to it: it emits dropRequested() and returns.
#pragma once

#include <QPersistentModelIndex>
#include <QSortFilterProxyModel>
#include <QStringList>

class QFileSystemModel;

namespace anyfile {

class FsProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit FsProxyModel(QObject *parent = nullptr);

    void setFileSystemModel(QFileSystemModel *fs);
    QFileSystemModel *fileSystemModel() const { return m_fs; }

    // The directory on screen, as a SOURCE index. Invalid means the model's
    // top level: drives on Windows.
    void setRoot(const QModelIndex &sourceRoot);
    void setNameFilter(const QString &text);
    QString nameFilter() const { return m_filter; }

    // The directory a drop onto proxyIndex lands in: the folder itself, the
    // folder a file is in, or the root for the empty area of the view.
    QString dirForDrop(const QModelIndex &proxyIndex) const;

    // Size and Type columns in Explorer's words: whole KB, folders blank, and
    // a type description instead of QFileSystemModel's MIME name.
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    Qt::DropActions supportedDropActions() const override;
    bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                         const QModelIndex &parent) const override;
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                      const QModelIndex &parent) override;

signals:
    void dropRequested(const QStringList &paths, const QString &destDir);
    void renameFailed(const QString &oldName, const QString &newName);

protected:
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    QFileSystemModel *m_fs = nullptr;
    QPersistentModelIndex m_root;
    QString m_filter;
};

}  // namespace anyfile
