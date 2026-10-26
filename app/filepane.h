// app/filepane.h
//
// One pane: the address row (back, forward, up, refresh, address, search), the
// file list as either a details table or an icon grid, and a status line.
//
// Both views share one proxy model AND one selection model, so switching
// between Details and Icons keeps the selection, the way Explorer does.
//
// Each pane owns its own QFileSystemModel. One model can serve two panes in
// principle, but QFileSystemModel has a single rootPath that drives what it
// watches, and two panes fighting over it means one of them stops updating.
#pragma once

#include <QList>
#include <QSet>
#include <QStringList>
#include <QWidget>

#include "anyfile/volumes.h"

class QAbstractItemView;
class QFileSystemModel;
class QLabel;
class QLineEdit;
class QListView;
class QStackedWidget;
class QTimer;
class QToolButton;
class QTreeView;

namespace anyfile {

class FsProxyModel;

class FilePane : public QWidget {
    Q_OBJECT
public:
    enum class ViewMode { Details, Icons };

    explicit FilePane(QWidget *parent = nullptr);

    // The folder on screen, '/'-separated. Empty means This PC (Windows only).
    QString path() const { return m_path; }
    QString displayPath() const;
    bool setPath(const QString &path, bool addHistory = true);

    bool canGoBack() const { return !m_back.isEmpty(); }
    bool canGoForward() const { return !m_forward.isEmpty(); }
    bool canGoUp() const;
    void back();
    void forward();
    void up();
    void refresh();

    QStringList selectedPaths() const;
    void selectPaths(const QStringList &paths);
    void selectAll();
    void renameCurrent();
    // Creates "New folder" (or "New folder (2)", ...), selects it and opens
    // its name for editing. Returns the path, or empty on failure.
    QString newFolder();
    void openSelected();

    void setViewMode(ViewMode mode);
    ViewMode viewMode() const { return m_mode; }
    void setShowHidden(bool show);
    bool showHidden() const { return m_showHidden; }

    void setActive(bool active);
    void focusView();
    void focusAddressBar();
    void focusSearch();
    void setVolumes(const QList<VolumeInfo> &volumes);

    QByteArray headerState() const;
    void restoreHeaderState(const QByteArray &state);

    // For the tests.
    QAbstractItemView *currentView() const;
    QTreeView *detailsView() const { return m_tree; }
    FsProxyModel *proxy() const { return m_proxy; }
    QFileSystemModel *fileSystemModel() const { return m_fs; }
    QStringList visibleNames() const;

signals:
    void activated(anyfile::FilePane *pane);
    void pathChanged(const QString &path);
    void selectionChanged();
    void dropRequested(const QStringList &paths, const QString &destDir);
    void contextMenuRequested(const QPoint &globalPos);
    void errorMessage(const QString &message);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildModel();
    QString normalize(const QString &input) const;
    void openIndex(const QModelIndex &proxyIndex);
    void scheduleStatus();
    void updateStatus();
    void updateButtons();
    void retrySelect();
    void showViewContextMenu(QAbstractItemView *view, const QPoint &pos);
    void commitAddress();

    QFileSystemModel *m_fs = nullptr;
    FsProxyModel *m_proxy = nullptr;
    QTreeView *m_tree = nullptr;
    QListView *m_list = nullptr;
    QStackedWidget *m_stack = nullptr;
    QWidget *m_activeBar = nullptr;
    QToolButton *m_backBtn = nullptr;
    QToolButton *m_fwdBtn = nullptr;
    QToolButton *m_upBtn = nullptr;
    QToolButton *m_refreshBtn = nullptr;
    QLineEdit *m_address = nullptr;
    QLineEdit *m_search = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_free = nullptr;
    QTimer *m_statusTimer = nullptr;

    QString m_path;
    bool m_hasPath = false;
    QStringList m_back;
    QStringList m_forward;
    ViewMode m_mode = ViewMode::Details;
    bool m_showHidden = false;
    QList<VolumeInfo> m_volumes;

    QSet<QString> m_pendingSelect;
    QString m_pendingRename;
    int m_selectGen = 0;
    bool m_inRetrySelect = false;
};

}  // namespace anyfile
