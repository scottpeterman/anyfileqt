// app/mainwindow.cpp
#include "app/mainwindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QSettings>
#include <QSplitter>
#include <QStyle>
#include <QToolBar>
#include <QVBoxLayout>

#include "anyfile/clipboard.h"
#include "anyfile/naming.h"
#include "anyfile/operationqueue.h"
#include "anyfile/volumes.h"
#include "app/dropaction.h"
#include "app/filepane.h"
#include "app/icons.h"
#include "app/navpane.h"
#include "app/opspanel.h"
#include "app/platform/platform.h"
#include "app/propertiesdialog.h"
#include "app/settings.h"

namespace anyfile {

namespace {

QString cleaned(const QString &p) { return QDir::cleanPath(QDir::fromNativeSeparators(p)); }

QString verbFor(OpKind k) {
    switch (k) {
    case OpKind::Copy: return QObject::tr("copied");
    case OpKind::Move: return QObject::tr("moved");
    case OpKind::Trash: return QObject::tr("moved to the %1").arg(trashDisplayName());
    case OpKind::Delete: return QObject::tr("deleted");
    }
    return {};
}

}  // namespace

MainWindow::MainWindow(Settings *settings, QWidget *parent)
    : QMainWindow(parent), m_settings(settings) {
    m_queue = new OperationQueue(this);
    m_volumes = new VolumeMonitor(5000, this);

    m_nav = new NavPane(this);
    m_panes[0] = new FilePane(this);
    m_panes[1] = new FilePane(this);

    m_split = new QSplitter(Qt::Horizontal, this);
    m_split->addWidget(m_panes[0]);
    m_split->addWidget(m_panes[1]);
    m_split->setChildrenCollapsible(false);

    m_outer = new QSplitter(Qt::Horizontal, this);
    m_outer->addWidget(m_nav);
    m_outer->addWidget(m_split);
    m_outer->setStretchFactor(0, 0);
    m_outer->setStretchFactor(1, 1);
    m_outer->setSizes({210, 1000});
    m_outer->setChildrenCollapsible(false);

    m_ops = new OperationsPanel(m_queue, this);

    auto *central = new QWidget(this);
    auto *lay = new QVBoxLayout(central);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(m_outer, 1);
    lay->addWidget(m_ops);
    setCentralWidget(central);

    createActions();
    createMenus();
    createCommandBar();

    for (FilePane *p : m_panes) {
        connect(p, &FilePane::activated, this, &MainWindow::setActive);
        connect(p, &FilePane::pathChanged, this, [this, p] {
            if (p == m_active) updateActions();
        });
        connect(p, &FilePane::selectionChanged, this, [this, p] {
            if (p == m_active) updateActions();
        });
        connect(p, &FilePane::dropRequested, this, &MainWindow::handleDrop);
        connect(p, &FilePane::contextMenuRequested, this, &MainWindow::showContextMenu);
        connect(p, &FilePane::errorMessage, this, &MainWindow::showError);
    }
    connect(m_nav, &NavPane::navigate, this, [this](const QString &path) {
        if (!m_active->setPath(path))
            showError(tr("\"%1\" is not available.").arg(QDir::toNativeSeparators(path)));
        else
            m_active->focusView();
    });
    connect(m_volumes, &VolumeMonitor::volumesChanged, this,
            [this](const QList<VolumeInfo> &v) {
                m_nav->setVolumes(v);
                for (FilePane *p : m_panes) p->setVolumes(v);
            });
    connect(m_queue, &OperationQueue::jobFinished, this, &MainWindow::onJobFinished);
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, [this] {
        m_canPaste = !readClipboardData(QGuiApplication::clipboard()->mimeData()).paths.isEmpty();
        updateActions();
    });
    m_canPaste = !readClipboardData(QGuiApplication::clipboard()->mimeData()).paths.isEmpty();

    restoreSettings();
    setActive(m_panes[0]);
    m_panes[0]->focusView();
}

MainWindow::~MainWindow() = default;

FilePane *MainWindow::otherPane() const {
    return m_active == m_panes[0] ? m_panes[1] : m_panes[0];
}

// --- actions --------------------------------------------------------------------

void MainWindow::createActions() {
    QStyle *st = style();
    const auto make = [this](const QString &text, const QList<QKeySequence> &keys,
                             const QIcon &icon, auto fn) {
        auto *a = new QAction(icon, text, this);
        a->setShortcuts(keys);
        connect(a, &QAction::triggered, this, fn);
        // Added to the window as well as to a menu, so the shortcut works
        // when the menu bar is hidden and on macOS's global menu.
        addAction(a);
        return a;
    };
    using K = QKeySequence;

#ifdef Q_OS_MACOS
    // Qt's Control is the Command key here, and Alt is Option.
    const QList<K> backKeys{K(Qt::CTRL | Qt::Key_BracketLeft), K(Qt::CTRL | Qt::Key_Left)};
    const QList<K> fwdKeys{K(Qt::CTRL | Qt::Key_BracketRight), K(Qt::CTRL | Qt::Key_Right)};
    const QList<K> upKeys{K(Qt::CTRL | Qt::Key_Up)};
    const QList<K> delKeys{K(Qt::Key_Delete), K(Qt::CTRL | Qt::Key_Backspace)};
    const QList<K> permKeys{K(Qt::SHIFT | Qt::Key_Delete),
                            K(Qt::ALT | Qt::CTRL | Qt::Key_Backspace)};
    const QList<K> hiddenKeys{K(Qt::CTRL | Qt::SHIFT | Qt::Key_Period)};  // Finder's
    const QList<K> addressKeys{K(Qt::CTRL | Qt::Key_L)};
    const QList<K> propKeys{K(Qt::CTRL | Qt::Key_I)};  // Finder's Get Info
#else
    const QList<K> backKeys{K(Qt::ALT | Qt::Key_Left)};
    const QList<K> fwdKeys{K(Qt::ALT | Qt::Key_Right)};
    const QList<K> upKeys{K(Qt::ALT | Qt::Key_Up)};
    const QList<K> delKeys{K(Qt::Key_Delete)};
    const QList<K> permKeys{K(Qt::SHIFT | Qt::Key_Delete)};
    const QList<K> hiddenKeys{K(Qt::CTRL | Qt::Key_H)};
    const QList<K> addressKeys{K(Qt::CTRL | Qt::Key_L), K(Qt::ALT | Qt::Key_D)};
    const QList<K> propKeys{K(Qt::ALT | Qt::Key_Return), K(Qt::ALT | Qt::Key_Enter)};
#endif

    m_back = make(tr("Back"), backKeys, st->standardIcon(QStyle::SP_ArrowBack),
                  [this] { m_active->back(); });
    m_forward = make(tr("Forward"), fwdKeys, st->standardIcon(QStyle::SP_ArrowForward),
                     [this] { m_active->forward(); });
    m_up = make(tr("Up"), upKeys, st->standardIcon(QStyle::SP_FileDialogToParent),
                [this] { m_active->up(); });
    m_refresh = make(tr("Refresh"), {K(Qt::Key_F5), K(Qt::CTRL | Qt::Key_R)},
                     st->standardIcon(QStyle::SP_BrowserReload), [this] { m_active->refresh(); });

    m_open = make(tr("Open"), {}, QIcon(), [this] { m_active->openSelected(); });
    m_newFolder = make(tr("New folder"), {K(Qt::CTRL | Qt::SHIFT | Qt::Key_N)},
                       glyphIcon(Glyph::NewFolder),
                       [this] { m_active->newFolder(); });
    m_cut = make(tr("Cut"), K::keyBindings(K::Cut), glyphIcon(Glyph::Cut), [this] { cut(); });
    m_copy = make(tr("Copy"), K::keyBindings(K::Copy), glyphIcon(Glyph::Copy), [this] { copy(); });
    m_paste = make(tr("Paste"), K::keyBindings(K::Paste), glyphIcon(Glyph::Paste), [this] { paste(); });
    m_rename = make(tr("Rename"), {K(Qt::Key_F2)}, glyphIcon(Glyph::Rename),
                    [this] { m_active->renameCurrent(); });
    m_delete = make(tr("Delete"), delKeys, glyphIcon(Glyph::Delete),
                    [this] { deleteSelection(false); });
    m_delete->setToolTip(tr("Move to the %1").arg(trashDisplayName()));
    m_deletePermanent = make(tr("Delete permanently"), permKeys, QIcon(),
                             [this] { deleteSelection(true); });
    m_copyPath = make(tr("Copy as path"), {K(Qt::CTRL | Qt::SHIFT | Qt::Key_C)}, QIcon(),
                      [this] { copyPath(); });
    m_selectAll = make(tr("Select all"), K::keyBindings(K::SelectAll), QIcon(),
                       [this] { m_active->selectAll(); });
    m_copyToOther = make(tr("Copy to other pane"), {}, QIcon(),
                         [this] { transferToOtherPane(OpKind::Copy); });
    m_moveToOther = make(tr("Move to other pane"), {}, QIcon(),
                         [this] { transferToOtherPane(OpKind::Move); });
    m_reveal = make(tr("Show in %1").arg(platform::fileManagerName()), {}, QIcon(), [this] {
        const QStringList sel = m_active->selectedPaths();
        if (!sel.isEmpty() && !platform::revealInFileManager(sel.first()))
            showError(tr("Could not open %1.").arg(platform::fileManagerName()));
    });
    m_terminal = make(tr("Open in Terminal"), {}, glyphIcon(Glyph::Terminal), [this] {
        if (!platform::openTerminalAt(m_active->path()))
            showError(tr("No terminal application could be started."));
    });

    m_details = make(tr("Details"), {K(Qt::CTRL | Qt::SHIFT | Qt::Key_6)},
                     glyphIcon(Glyph::Details),
                     [this] { m_active->setViewMode(FilePane::ViewMode::Details); updateActions(); });
    m_icons = make(tr("Icons"), {K(Qt::CTRL | Qt::SHIFT | Qt::Key_2)},
                   glyphIcon(Glyph::Icons),
                   [this] { m_active->setViewMode(FilePane::ViewMode::Icons); updateActions(); });
    m_details->setCheckable(true);
    m_icons->setCheckable(true);
    auto *modes = new QActionGroup(this);
    modes->addAction(m_details);
    modes->addAction(m_icons);

    m_hidden = make(tr("Hidden items"), hiddenKeys, glyphIcon(Glyph::Hidden), [this](bool on) {
        for (FilePane *p : m_panes) p->setShowHidden(on);
    });
    m_hidden->setCheckable(true);
    m_dual = make(tr("Dual pane"), {K(Qt::CTRL | Qt::Key_Backslash)}, glyphIcon(Glyph::Dual),
                  [this](bool on) { setDualPane(on); });
    m_dual->setCheckable(true);

    m_properties = make(tr("Properties"), propKeys, glyphIcon(Glyph::Properties),
                        [this] { showProperties(); });

    m_focusAddress = make(tr("Go to address bar"), addressKeys, QIcon(),
                          [this] { m_active->focusAddressBar(); });
    m_search = make(tr("Search"), {K(Qt::CTRL | Qt::Key_F), K(Qt::CTRL | Qt::Key_E)}, QIcon(),
                    [this] { m_active->focusSearch(); });
    m_quit = make(tr("Quit"), K::keyBindings(K::Quit), QIcon(), [this] { close(); });
    m_quit->setMenuRole(QAction::QuitRole);
    m_about = make(tr("About AnyFile"), {}, QIcon(), [this] {
        QMessageBox::about(
            this, tr("About AnyFile"),
            tr("<b>AnyFile</b> %1<br><br>A Windows-styled file manager for Linux, macOS "
               "and Windows.<br><br>Qt %2<br>Licensed under the GNU GPL v3.")
                .arg(QCoreApplication::applicationVersion(), QString::fromLatin1(qVersion())));
    });
    m_about->setMenuRole(QAction::AboutRole);
}

void MainWindow::createMenus() {
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(m_open);
    file->addAction(m_newFolder);
    file->addSeparator();
    file->addAction(m_terminal);
    file->addAction(m_reveal);
    file->addAction(m_properties);
    file->addSeparator();
    file->addAction(m_deletePermanent);
    file->addSeparator();
    file->addAction(m_quit);

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(m_cut);
    edit->addAction(m_copy);
    edit->addAction(m_paste);
    edit->addSeparator();
    edit->addAction(m_rename);
    edit->addAction(m_delete);
    edit->addSeparator();
    edit->addAction(m_copyPath);
    edit->addAction(m_selectAll);
    edit->addSeparator();
    edit->addAction(m_copyToOther);
    edit->addAction(m_moveToOther);

    QMenu *view = menuBar()->addMenu(tr("&View"));
    view->addAction(m_details);
    view->addAction(m_icons);
    view->addSeparator();
    view->addAction(m_hidden);
    view->addAction(m_dual);
    view->addSeparator();
    view->addAction(m_refresh);

    QMenu *go = menuBar()->addMenu(tr("&Go"));
    go->addAction(m_back);
    go->addAction(m_forward);
    go->addAction(m_up);
    go->addSeparator();
    go->addAction(m_focusAddress);
    go->addAction(m_search);

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(m_about);
}

void MainWindow::createCommandBar() {
    // Explorer's command bar: the verbs, labelled, in one row above the panes.
    QToolBar *bar = addToolBar(tr("Commands"));
    bar->setObjectName(QStringLiteral("commandBar"));
    bar->setMovable(false);
    bar->setFloatable(false);
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    bar->setIconSize(QSize(18, 18));
    bar->addAction(m_newFolder);
    bar->addSeparator();
    bar->addAction(m_cut);
    bar->addAction(m_copy);
    bar->addAction(m_paste);
    bar->addAction(m_rename);
    bar->addAction(m_delete);
    bar->addAction(m_properties);
    bar->addSeparator();
    bar->addAction(m_details);
    bar->addAction(m_icons);
    bar->addSeparator();
    bar->addAction(m_hidden);
    bar->addAction(m_dual);
}

void MainWindow::updateActions() {
    if (!m_active) return;
    const QStringList sel = m_active->selectedPaths();
    const bool hasSel = !sel.isEmpty();
    const bool real = !m_active->path().isEmpty();
    const bool dual = isDualPane();

    m_back->setEnabled(m_active->canGoBack());
    m_forward->setEnabled(m_active->canGoForward());
    m_up->setEnabled(m_active->canGoUp());
    m_open->setEnabled(hasSel);
    m_newFolder->setEnabled(real);
    m_cut->setEnabled(hasSel && real);
    m_copy->setEnabled(hasSel);
    m_paste->setEnabled(real && m_canPaste);
    m_rename->setEnabled(sel.size() == 1 && real);
    m_delete->setEnabled(hasSel && real);
    m_deletePermanent->setEnabled(hasSel && real);
    m_copyPath->setEnabled(hasSel);
    m_reveal->setEnabled(hasSel);
    m_terminal->setEnabled(real);
    // With nothing selected, Properties describes the folder on screen.
    m_properties->setEnabled(hasSel || real);
    const bool otherReal = !otherPane()->path().isEmpty();
    m_copyToOther->setEnabled(dual && hasSel && otherReal);
    m_moveToOther->setEnabled(dual && hasSel && real && otherReal);

    const bool details = m_active->viewMode() == FilePane::ViewMode::Details;
    m_details->setChecked(details);
    m_icons->setChecked(!details);

    QString title = QFileInfo(m_active->path()).fileName();
    if (title.isEmpty()) title = m_active->displayPath();
    setWindowTitle(tr("%1 - AnyFile").arg(title));
}

void MainWindow::setActive(FilePane *pane) {
    if (!pane) return;
    m_active = pane;
    // The strip only means something when there is a choice of pane.
    for (FilePane *p : m_panes) p->setActive(p == pane && isDualPane());
    updateActions();
}

void MainWindow::setDualPane(bool dual) {
    m_panes[1]->setVisible(dual);
    m_dual->blockSignals(true);
    m_dual->setChecked(dual);
    m_dual->blockSignals(false);
    setActive(!dual && m_active == m_panes[1] ? m_panes[0] : m_active);
}

bool MainWindow::isDualPane() const { return !m_panes[1]->isHidden(); }

// --- clipboard and operations ---------------------------------------------------

void MainWindow::cut() {
    const QStringList sel = m_active->selectedPaths();
    if (!sel.isEmpty()) QGuiApplication::clipboard()->setMimeData(makeClipboardData(sel, true));
}

void MainWindow::copy() {
    const QStringList sel = m_active->selectedPaths();
    if (!sel.isEmpty()) QGuiApplication::clipboard()->setMimeData(makeClipboardData(sel, false));
}

void MainWindow::paste() {
    if (m_active->path().isEmpty()) return;
    pasteInto(QGuiApplication::clipboard()->mimeData(), m_active->path());
}

FileJob *MainWindow::pasteInto(const QMimeData *data, const QString &dir) {
    const ClipboardFiles f = readClipboardData(data);
    if (f.paths.isEmpty() || dir.isEmpty()) return nullptr;
    FileJob *job = startJob(f.cut ? OpKind::Move : OpKind::Copy, f.paths, dir);
    if (f.cut) m_cutJobs.insert(job);
    return job;
}

void MainWindow::deleteSelection(bool permanent) {
    const QStringList sel = m_active->selectedPaths();
    if (sel.isEmpty() || m_active->path().isEmpty()) return;
    if (!permanent) {
        startJob(OpKind::Trash, sel, QString());
        return;
    }
    const QString q =
        sel.size() == 1
            ? tr("Are you sure you want to permanently delete \"%1\"?")
                  .arg(QFileInfo(sel.first()).fileName())
            : tr("Are you sure you want to permanently delete these %1 items?").arg(sel.size());
    if (QMessageBox::warning(this, tr("Delete permanently"), q,
                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
        startJob(OpKind::Delete, sel, QString());
}

void MainWindow::copyPath() {
    QStringList out;
    for (const QString &p : m_active->selectedPaths()) {
#ifdef Q_OS_WIN
        // Explorer's "Copy as path" quotes each one, so a path with spaces
        // pastes into cmd intact.
        out << QLatin1Char('"') + QDir::toNativeSeparators(p) + QLatin1Char('"');
#else
        out << p;
#endif
    }
    if (!out.isEmpty()) QGuiApplication::clipboard()->setText(out.join(QLatin1Char('\n')));
}

void MainWindow::transferToOtherPane(OpKind kind) {
    const QStringList sel = m_active->selectedPaths();
    const QString dest = otherPane()->path();
    if (sel.isEmpty() || dest.isEmpty() || !isDualPane()) return;
    if (isPointlessDrop(sel, dest)) return;
    startJob(kind, sel, dest);
}

void MainWindow::handleDrop(const QStringList &paths, const QString &destDir) {
    if (isPointlessDrop(paths, destDir)) return;
    // queryKeyboardModifiers, not keyboardModifiers: the latter is the state
    // as of the last input event this application saw, and during a drag from
    // another application that can be from before the drag began.
    const Qt::DropAction a = chooseDropAction(
        paths, destDir, QGuiApplication::queryKeyboardModifiers(), m_volumes->volumes());
    startJob(a == Qt::MoveAction ? OpKind::Move : OpKind::Copy, paths, destDir);
}

FileJob *MainWindow::startJob(OpKind kind, const QStringList &paths, const QString &dest) {
    return m_queue->enqueue(kind, paths, dest);
}

void MainWindow::onJobFinished(FileJob *job, const JobResult &result) {
    if (m_cutJobs.remove(job)) {
        // Explorer clears a cut once it has been pasted: the originals are
        // gone, and a second paste would fail on every file. Only if the
        // clipboard still holds that same cut.
        QClipboard *cb = QGuiApplication::clipboard();
        const ClipboardFiles now = readClipboardData(cb->mimeData());
        if (now.cut && now.paths == job->sources()) cb->clear();
    }

    if (!result.created.isEmpty()) {
        const QString dest = cleaned(job->destination());
        for (FilePane *p : m_panes)
            if (cleaned(p->path()) == dest) p->selectPaths(result.created);
    }

    if (!result.errors.isEmpty()) {
        const int n = int(result.errors.size());
        QStringList lines;
        for (const JobError &e : result.errors) {
            if (lines.size() == 200) {
                lines << tr("... and %1 more").arg(n - 200);
                break;
            }
            lines << QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(e.path), e.message);
        }
        auto *box = new QMessageBox(QMessageBox::Warning, tr("AnyFile"),
                                    n == 1 ? tr("1 item could not be %1.").arg(verbFor(job->kind()))
                                           : tr("%1 items could not be %2.")
                                                 .arg(n)
                                                 .arg(verbFor(job->kind())),
                                    QMessageBox::Ok, this);
        box->setInformativeText(result.errors.first().message);
        box->setDetailedText(lines.join(QLatin1Char('\n')));
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();
    }
}

void MainWindow::showProperties() {
    QStringList paths = m_active->selectedPaths();
    if (paths.isEmpty() && !m_active->path().isEmpty()) paths << m_active->path();
    if (paths.isEmpty()) return;
    // Modeless and self-deleting, like Explorer's: several can be open, and
    // the window stays usable while a big folder is counted.
    auto *dlg = new PropertiesDialog(paths, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

// --- menus, errors, lifetime ----------------------------------------------------

void MainWindow::showContextMenu(const QPoint &globalPos) {
    updateActions();
    QMenu m(this);
    if (!m_active->selectedPaths().isEmpty()) {
        m.addAction(m_open);
        m.addSeparator();
        m.addAction(m_cut);
        m.addAction(m_copy);
        m.addSeparator();
        m.addAction(m_rename);
        m.addAction(m_delete);
        m.addAction(m_deletePermanent);
        m.addSeparator();
        m.addAction(m_copyPath);
        m.addAction(m_reveal);
        if (isDualPane()) {
            m.addSeparator();
            m.addAction(m_copyToOther);
            m.addAction(m_moveToOther);
        }
        m.addSeparator();
        m.addAction(m_properties);
    } else {
        m.addAction(m_paste);
        m.addAction(m_newFolder);
        m.addSeparator();
        m.addAction(m_terminal);
        m.addAction(m_refresh);
        m.addSeparator();
        m.addAction(m_details);
        m.addAction(m_icons);
        m.addAction(m_hidden);
        m.addSeparator();
        m.addAction(m_properties);  // of the folder itself
    }
    m.exec(globalPos);
}

void MainWindow::showError(const QString &message) {
    QMessageBox::warning(this, tr("AnyFile"), message);
}

void MainWindow::restoreSettings() {
    QSettings &s = m_settings->q();
    if (!restoreGeometry(s.value(QStringLiteral("window/geometry")).toByteArray()))
        resize(1280, 780);
    m_outer->restoreState(s.value(QStringLiteral("layout/outer")).toByteArray());
    m_split->restoreState(s.value(QStringLiteral("layout/panes")).toByteArray());

    const bool hidden = s.value(QStringLiteral("view/hidden"), false).toBool();
    m_hidden->setChecked(hidden);
    for (FilePane *p : m_panes) p->setShowHidden(hidden);

    for (int i = 0; i < 2; ++i) {
        const QString key = QStringLiteral("pane%1/").arg(i);
        FilePane *p = m_panes[i];
        const QString saved = s.value(key + QStringLiteral("path")).toString();
        // A key that is present with an empty value is This PC on Windows;
        // an absent key is a first run. Either way, a folder that has gone
        // since last time falls back to home rather than to nowhere.
        if (!s.contains(key + QStringLiteral("path")) || !p->setPath(saved))
            p->setPath(QDir::homePath(), false);
        p->setViewMode(s.value(key + QStringLiteral("mode")).toString() == QLatin1String("icons")
                           ? FilePane::ViewMode::Icons
                           : FilePane::ViewMode::Details);
        p->restoreHeaderState(s.value(key + QStringLiteral("header")).toByteArray());
    }
    setDualPane(s.value(QStringLiteral("layout/dual"), true).toBool());
}

void MainWindow::saveSettings() {
    QSettings &s = m_settings->q();
    s.setValue(QStringLiteral("window/geometry"), saveGeometry());
    s.setValue(QStringLiteral("layout/outer"), m_outer->saveState());
    s.setValue(QStringLiteral("layout/panes"), m_split->saveState());
    s.setValue(QStringLiteral("layout/dual"), isDualPane());
    s.setValue(QStringLiteral("view/hidden"), m_hidden->isChecked());
    for (int i = 0; i < 2; ++i) {
        const QString key = QStringLiteral("pane%1/").arg(i);
        s.setValue(key + QStringLiteral("path"), m_panes[i]->path());
        s.setValue(key + QStringLiteral("mode"),
                   m_panes[i]->viewMode() == FilePane::ViewMode::Icons ? QStringLiteral("icons")
                                                                       : QStringLiteral("details"));
        s.setValue(key + QStringLiteral("header"), m_panes[i]->headerState());
    }
    m_settings->sync();
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (m_queue->isBusy()) {
        const int n = m_queue->waiting() + (m_queue->current() ? 1 : 0);
        const auto r = QMessageBox::question(
            this, tr("AnyFile"),
            n == 1 ? tr("A file operation is still running. Cancel it and quit?")
                   : tr("%1 file operations are still running or queued. Cancel them and quit?")
                         .arg(n),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (r != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        m_queue->cancelAll();
    }
    saveSettings();
    event->accept();
}

}  // namespace anyfile
