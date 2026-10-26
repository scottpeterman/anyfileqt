// app/mainwindow.h
//
// The window: menus, the command bar, the navigation pane, two file panes
// side by side (one when Dual pane is off), and the operations strip.
//
// Every action applies to the ACTIVE pane -- the one most recently clicked or
// focused, marked with a highlight strip along its top.
#pragma once

#include <QMainWindow>
#include <QSet>
#include <QStringList>

#include "anyfile/fileop.h"

class QAction;
class QMimeData;
class QSplitter;

namespace anyfile {

class FilePane;
class NavPane;
class OperationQueue;
class OperationsPanel;
class Settings;
class VolumeMonitor;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(Settings *settings, QWidget *parent = nullptr);
    ~MainWindow() override;

    FilePane *activePane() const { return m_active; }
    FilePane *pane(int i) const { return m_panes[i]; }
    FilePane *otherPane() const;
    OperationQueue *queue() const { return m_queue; }
    OperationsPanel *operationsPanel() const { return m_ops; }

    void setDualPane(bool dual);
    bool isDualPane() const;

    // Paste of clipboard-format data into dir: a cut moves, a copy copies.
    // Public so the tests can drive it without the system clipboard.
    FileJob *pasteInto(const QMimeData *mime, const QString &dir);

    void saveSettings();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void createActions();
    void createMenus();
    void createCommandBar();
    void restoreSettings();
    void setActive(FilePane *pane);
    void updateActions();
    void showContextMenu(const QPoint &globalPos);
    void showError(const QString &message);

    FileJob *startJob(OpKind kind, const QStringList &paths, const QString &dest);
    void onJobFinished(FileJob *job, const JobResult &result);
    void handleDrop(const QStringList &paths, const QString &destDir);

    void cut();
    void copy();
    void paste();
    void deleteSelection(bool permanent);
    void copyPath();
    void transferToOtherPane(OpKind kind);
    void showProperties();

    Settings *m_settings;
    OperationQueue *m_queue = nullptr;
    VolumeMonitor *m_volumes = nullptr;
    NavPane *m_nav = nullptr;
    QSplitter *m_outer = nullptr;
    QSplitter *m_split = nullptr;
    FilePane *m_panes[2] = {nullptr, nullptr};
    FilePane *m_active = nullptr;
    OperationsPanel *m_ops = nullptr;
    QSet<FileJob *> m_cutJobs;  // pastes of a cut; the clipboard is cleared after
    bool m_canPaste = false;    // cached: reading the clipboard can block on X11

    QAction *m_back, *m_forward, *m_up, *m_refresh;
    QAction *m_open, *m_newFolder, *m_cut, *m_copy, *m_paste, *m_rename;
    QAction *m_delete, *m_deletePermanent, *m_copyPath, *m_selectAll;
    QAction *m_copyToOther, *m_moveToOther, *m_reveal, *m_terminal;
    QAction *m_details, *m_icons, *m_hidden, *m_dual;
    QAction *m_focusAddress, *m_search, *m_quit, *m_about, *m_properties;
};

}  // namespace anyfile
