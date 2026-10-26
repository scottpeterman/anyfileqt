// app/filepane.cpp
#include "app/filepane.h"

#include <QApplication>
#include <QCompleter>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelection>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QLocale>
#include <QStackedWidget>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

#include "anyfile/fsinfo.h"
#include "anyfile/naming.h"
#include "app/fsproxy.h"

namespace anyfile {

namespace {

enum Col { Name = 0, Size = 1, Type = 2, Modified = 3 };

// Explorer selects the name without its extension when a rename starts, so
// typing replaces "report" and leaves ".txt" alone. Deferred one turn of the
// event loop: the view selects all of the editor's text after setEditorData()
// returns, and a selection made inside it would be overwritten.
class RenameDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void setEditorData(QWidget *editor, const QModelIndex &index) const override {
        QStyledItemDelegate::setEditorData(editor, index);
        auto *le = qobject_cast<QLineEdit *>(editor);
        if (!le) return;
        const auto *fs = qobject_cast<const QSortFilterProxyModel *>(index.model());
        const QModelIndex src = fs ? fs->mapToSource(index) : QModelIndex();
        const auto *fsm = fs ? qobject_cast<const QFileSystemModel *>(fs->sourceModel()) : nullptr;
        const bool isDir = fsm && src.isValid() && fsm->isDir(src);
        const int len = int(splitName(le->text(), isDir).base.size());
        QTimer::singleShot(0, le, [le, len] { le->setSelection(0, len); });
    }
};


QDir::Filters filtersFor(bool showHidden) {
    QDir::Filters f = QDir::AllEntries | QDir::AllDirs | QDir::NoDotAndDotDot;
#ifdef Q_OS_WIN
    f |= QDir::Drives;
    if (showHidden) f |= QDir::Hidden | QDir::System;
#else
    // System on Unix is sockets, FIFOs and broken symlinks -- things that are
    // there and worth seeing, not the Windows "protected OS file" attribute.
    f |= QDir::System;
    if (showHidden) f |= QDir::Hidden;
#endif
    return f;
}

QToolButton *navButton(QWidget *parent, QStyle::StandardPixmap icon, const QString &tip) {
    auto *b = new QToolButton(parent);
    b->setIcon(parent->style()->standardIcon(icon));
    b->setToolTip(tip);
    b->setAutoRaise(true);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}

}  // namespace

FilePane::FilePane(QWidget *parent) : QWidget(parent) {
    m_proxy = new FsProxyModel(this);
    buildModel();

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // A two-pixel strip in the highlight colour marks the active pane; the
    // command bar and every shortcut act on that one.
    m_activeBar = new QWidget(this);
    m_activeBar->setFixedHeight(2);
    m_activeBar->setAutoFillBackground(true);
    outer->addWidget(m_activeBar);

    auto *row = new QHBoxLayout;
    row->setContentsMargins(4, 4, 4, 4);
    row->setSpacing(2);
    m_backBtn = navButton(this, QStyle::SP_ArrowBack, tr("Back (Alt+Left)"));
    m_fwdBtn = navButton(this, QStyle::SP_ArrowForward, tr("Forward (Alt+Right)"));
    m_upBtn = navButton(this, QStyle::SP_FileDialogToParent, tr("Up (Alt+Up)"));
    m_refreshBtn = navButton(this, QStyle::SP_BrowserReload, tr("Refresh (F5)"));
    connect(m_backBtn, &QToolButton::clicked, this, &FilePane::back);
    connect(m_fwdBtn, &QToolButton::clicked, this, &FilePane::forward);
    connect(m_upBtn, &QToolButton::clicked, this, &FilePane::up);
    connect(m_refreshBtn, &QToolButton::clicked, this, &FilePane::refresh);

    m_address = new QLineEdit(this);
    m_address->setClearButtonEnabled(false);
    {
        // Folder completion in the address bar. QCompleter recognises a
        // QFileSystemModel and completes path segment by segment.
        auto *completer = new QCompleter(this);
        auto *dirs = new QFileSystemModel(completer);
        dirs->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Drives);
        dirs->setRootPath(QString());
        completer->setModel(dirs);
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        m_address->setCompleter(completer);
    }
    connect(m_address, &QLineEdit::returnPressed, this, &FilePane::commitAddress);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search"));
    m_search->setClearButtonEnabled(true);
    m_search->setMaximumWidth(220);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &t) {
        m_proxy->setNameFilter(t);
        scheduleStatus();
    });

    row->addWidget(m_backBtn);
    row->addWidget(m_fwdBtn);
    row->addWidget(m_upBtn);
    row->addWidget(m_refreshBtn);
    row->addWidget(m_address, 3);
    row->addWidget(m_search, 1);
    outer->addLayout(row);

    // --- views --------------------------------------------------------------

    m_tree = new QTreeView(this);
    m_list = new QListView(this);
    for (QAbstractItemView *v : {static_cast<QAbstractItemView *>(m_tree),
                                 static_cast<QAbstractItemView *>(m_list)}) {
        v->setSelectionMode(QAbstractItemView::ExtendedSelection);
        v->setSelectionBehavior(QAbstractItemView::SelectRows);
        // A slow second click on a selected item renames, as in Explorer. F2
        // goes through the window's Rename action, which calls edit()
        // directly and ignores these triggers -- EditKeyPressed is left out
        // because on macOS Qt maps it to Return, and Return opens.
        v->setEditTriggers(QAbstractItemView::SelectedClicked);
        v->setDragEnabled(true);
        v->setAcceptDrops(true);
        v->setDropIndicatorShown(true);
        v->setDragDropMode(QAbstractItemView::DragDrop);
        v->setDefaultDropAction(Qt::CopyAction);
        v->setContextMenuPolicy(Qt::CustomContextMenu);
        v->setItemDelegate(new RenameDelegate(v));
        v->installEventFilter(this);
        v->viewport()->installEventFilter(this);
        connect(v, &QAbstractItemView::activated, this, &FilePane::openIndex);
        connect(v, &QWidget::customContextMenuRequested, this,
                [this, v](const QPoint &pos) { showViewContextMenu(v, pos); });
    }

    m_tree->setRootIsDecorated(false);
    m_tree->setItemsExpandable(false);
    m_tree->setExpandsOnDoubleClick(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setAllColumnsShowFocus(true);
    m_tree->setSortingEnabled(true);
    m_tree->setModel(m_proxy);
    m_tree->sortByColumn(Name, Qt::AscendingOrder);
    {
        // Explorer's column order: Name, Date modified, Type, Size.
        // QFileSystemModel's is Name, Size, Type, Date modified.
        QHeaderView *h = m_tree->header();
        h->moveSection(h->visualIndex(Modified), 1);
        h->moveSection(h->visualIndex(Type), 2);
        h->setStretchLastSection(false);
        h->setSectionsMovable(true);
        // Sized to fit a half-width pane at 1280 without a horizontal
        // scrollbar; the header state is saved, so a user's widths stick.
        // Type is wide enough for "Markdown Document" and "Configuration
        // File"; Name gives up the 20px.
        h->resizeSection(Name, 200);
        h->resizeSection(Modified, 130);
        h->resizeSection(Type, 140);
        h->resizeSection(Size, 70);
    }

    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(QSize(48, 48));
    m_list->setGridSize(QSize(104, 92));
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setWrapping(true);
    m_list->setWordWrap(true);
    m_list->setUniformItemSizes(true);
    m_list->setTextElideMode(Qt::ElideMiddle);
    m_list->setModel(m_proxy);
    // One selection for both views. setModel() gave the list a selection model
    // of its own; it is replaced, and the replaced one is the caller's to free.
    {
        QItemSelectionModel *own = m_list->selectionModel();
        m_list->setSelectionModel(m_tree->selectionModel());
        delete own;
    }

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(m_tree);
    m_stack->addWidget(m_list);
    outer->addWidget(m_stack, 1);

    auto *statusRow = new QHBoxLayout;
    statusRow->setContentsMargins(8, 2, 8, 3);
    m_status = new QLabel(this);
    m_free = new QLabel(this);
    m_free->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    statusRow->addWidget(m_status, 1);
    statusRow->addWidget(m_free);
    outer->addLayout(statusRow);

    m_address->installEventFilter(this);
    m_search->installEventFilter(this);

    m_statusTimer = new QTimer(this);
    m_statusTimer->setSingleShot(true);
    m_statusTimer->setInterval(0);
    connect(m_statusTimer, &QTimer::timeout, this, &FilePane::updateStatus);

    connect(m_tree->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
        scheduleStatus();
        emit selectionChanged();
    });
    connect(m_proxy, &QAbstractItemModel::rowsInserted, this, [this] {
        scheduleStatus();
        retrySelect();
    });
    connect(m_proxy, &QAbstractItemModel::rowsRemoved, this, &FilePane::scheduleStatus);
    connect(m_proxy, &QAbstractItemModel::layoutChanged, this, &FilePane::scheduleStatus);
    connect(m_proxy, &QAbstractItemModel::modelReset, this, &FilePane::scheduleStatus);
    connect(m_proxy, &FsProxyModel::dropRequested, this, &FilePane::dropRequested);
    connect(m_proxy, &FsProxyModel::renameFailed, this,
            [this](const QString &oldName, const QString &newName) {
                emit errorMessage(tr("Could not rename \"%1\" to \"%2\". The name may be "
                                     "in use or contain characters that are not allowed.")
                                      .arg(oldName, newName));
            });

    setActive(false);
}

void FilePane::buildModel() {
    QFileSystemModel *old = m_fs;
    m_fs = new QFileSystemModel(this);
    m_fs->setReadOnly(false);  // renaming in place goes through setData
    m_fs->setFilter(filtersFor(m_showHidden));
    m_proxy->setFileSystemModel(m_fs);
    connect(m_fs, &QFileSystemModel::directoryLoaded, this, [this](const QString &) {
        scheduleStatus();
        retrySelect();
    });
    delete old;
}

// --- navigation ---------------------------------------------------------------

QString FilePane::displayPath() const {
#ifdef Q_OS_WIN
    if (m_path.isEmpty()) return tr("This PC");
#endif
    return QDir::toNativeSeparators(m_path);
}

QString FilePane::normalize(const QString &input) const {
    QString s = input.trimmed();
    if (s.isEmpty()) return {};
#ifdef Q_OS_WIN
    if (s.compare(tr("This PC"), Qt::CaseInsensitive) == 0) return {};
#endif
    if (s == QLatin1String("~") || s.startsWith(QLatin1String("~/")))
        s = QDir::homePath() + s.mid(1);
    s = QDir::fromNativeSeparators(s);
#ifdef Q_OS_WIN
    if (s.size() == 2 && s.at(1) == QLatin1Char(':')) s += QLatin1Char('/');
#endif
    QFileInfo fi(s);
    // A relative name typed into the address bar is a folder inside the
    // current one, as in Explorer.
    if (fi.isRelative())
        fi = QFileInfo(QDir(m_path.isEmpty() ? QDir::homePath() : m_path), s);
    return QDir::cleanPath(fi.absoluteFilePath());
}

bool FilePane::setPath(const QString &path, bool addHistory) {
    QString p = normalize(path);
#ifdef Q_OS_WIN
    const bool thisPC = p.isEmpty();
#else
    const bool thisPC = false;
    if (p.isEmpty()) p = QStringLiteral("/");
#endif
    if (!thisPC && !QFileInfo(p).isDir()) return false;

    if (m_hasPath && addHistory && p != m_path) {
        m_back.append(m_path);
        m_forward.clear();
    }
    m_path = p;
    m_hasPath = true;

    m_search->blockSignals(true);
    m_search->clear();
    m_search->blockSignals(false);
    m_proxy->setNameFilter(QString());

    const QModelIndex src = m_fs->setRootPath(p);
    m_proxy->setRoot(src);
    const QModelIndex root = m_proxy->mapFromSource(src);
    m_tree->selectionModel()->clear();
    m_tree->setRootIndex(root);
    m_list->setRootIndex(root);
    m_tree->scrollToTop();
    m_list->scrollToTop();

    m_address->setText(displayPath());
    m_search->setPlaceholderText(
        tr("Search %1").arg(thisPC ? displayPath() : QFileInfo(p).fileName()));
    updateButtons();
    scheduleStatus();
    emit pathChanged(m_path);
    return true;
}

bool FilePane::canGoUp() const {
    if (m_path.isEmpty()) return false;
#ifdef Q_OS_WIN
    return true;  // a drive root goes up to This PC
#else
    return m_path != QLatin1String("/");
#endif
}

void FilePane::back() {
    while (!m_back.isEmpty()) {
        const QString p = m_back.takeLast();
        const QString here = m_path;
        if (setPath(p, false)) {
            m_forward.append(here);
            updateButtons();
            return;
        }
        // That folder is gone; try the one before it.
    }
    updateButtons();
}

void FilePane::forward() {
    while (!m_forward.isEmpty()) {
        const QString p = m_forward.takeLast();
        const QString here = m_path;
        if (setPath(p, false)) {
            m_back.append(here);
            updateButtons();
            return;
        }
    }
    updateButtons();
}

void FilePane::up() {
    if (!canGoUp()) return;
    const QString child = m_path;
    QDir d(m_path);
#ifdef Q_OS_WIN
    if (d.isRoot()) {
        setPath(QString());
        selectPaths({child});
        return;
    }
#endif
    if (d.cdUp() && setPath(d.absolutePath())) selectPaths({child});  // Explorer selects where you came from
}

void FilePane::refresh() {
    // QFileSystemModel has no "reload": it trusts its watcher, and on a network
    // share or some FUSE mounts the watcher hears nothing. A fresh model is the
    // only reliable re-read.
    const QStringList sel = selectedPaths();
    const QString p = m_path;
    buildModel();
    setPath(p, false);
    selectPaths(sel);
}

void FilePane::commitAddress() {
    const QString typed = m_address->text();
    if (setPath(typed)) {
        focusView();
        return;
    }
    emit errorMessage(tr("Can't find \"%1\". Check the spelling and try again.").arg(typed));
    m_address->setText(displayPath());
}

void FilePane::updateButtons() {
    m_backBtn->setEnabled(canGoBack());
    m_fwdBtn->setEnabled(canGoForward());
    m_upBtn->setEnabled(canGoUp());
}

// --- opening ------------------------------------------------------------------

void FilePane::openIndex(const QModelIndex &proxyIndex) {
    if (!proxyIndex.isValid()) return;
    const QString path = m_fs->filePath(m_proxy->mapToSource(proxyIndex));
    const QFileInfo fi(path);
#ifdef Q_OS_MACOS
    // An .app is a directory that the user thinks of as a file.
    if (fi.isBundle()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        return;
    }
#endif
#ifdef Q_OS_WIN
    if (fi.isShortcut()) {
        const QString target = fi.symLinkTarget();
        if (QFileInfo(target).isDir()) {
            setPath(target);
            return;
        }
    }
#endif
    if (fi.isDir()) {
        if (!setPath(path))
            emit errorMessage(tr("\"%1\" is not available.").arg(QDir::toNativeSeparators(path)));
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
        emit errorMessage(tr("There is no application set to open \"%1\".").arg(fi.fileName()));
}

void FilePane::openSelected() {
    const QModelIndexList rows = m_tree->selectionModel()->selectedRows(Name);
    if (rows.isEmpty()) return;
    // One folder navigates; files all open. A folder and files together: the
    // files open and the first folder is entered.
    QModelIndex firstDir;
    for (const QModelIndex &r : rows) {
        if (m_fs->isDir(m_proxy->mapToSource(r))) {
            if (!firstDir.isValid()) firstDir = r;
        } else {
            openIndex(r);
        }
    }
    if (firstDir.isValid()) openIndex(firstDir);
}

// --- selection ----------------------------------------------------------------

QStringList FilePane::selectedPaths() const {
    QModelIndexList rows = m_tree->selectionModel()->selectedRows(Name);
    std::sort(rows.begin(), rows.end(),
              [](const QModelIndex &a, const QModelIndex &b) { return a.row() < b.row(); });
    QStringList out;
    for (const QModelIndex &r : rows) out.append(m_fs->filePath(m_proxy->mapToSource(r)));
    return out;
}

void FilePane::selectAll() { currentView()->selectAll(); }

void FilePane::selectPaths(const QStringList &paths) {
    m_pendingSelect = QSet<QString>(paths.begin(), paths.end());
    const int gen = ++m_selectGen;
    retrySelect();
    // New files appear when the watcher reports them, which is not now. What
    // has not turned up in three seconds is dropped rather than selected out
    // of nowhere later. The generation check stops an old timer from
    // clearing a newer request.
    if (!m_pendingSelect.isEmpty()) {
        QTimer::singleShot(3000, this, [this, gen] {
            if (gen != m_selectGen) return;
            m_pendingSelect.clear();
            m_pendingRename.clear();
        });
    }
}

void FilePane::retrySelect() {
    // Re-entrant through the model: QFileSystemModel::index(path) can create
    // the node on the spot and emit rowsInserted, which lands back here. The
    // guard stops the inner call, and the loop works on a copy so nothing it
    // iterates is modified underneath it.
    if (m_pendingSelect.isEmpty() || m_inRetrySelect) return;
    m_inRetrySelect = true;
    QItemSelection sel;
    QModelIndex first;
    QModelIndex renameIdx;
    const int lastCol = m_proxy->columnCount(m_tree->rootIndex()) - 1;
    const QStringList wanted(m_pendingSelect.begin(), m_pendingSelect.end());
    for (const QString &path : wanted) {
        const QModelIndex s = m_fs->index(path);
        const QModelIndex px = s.isValid() ? m_proxy->mapFromSource(s) : QModelIndex();
        if (!px.isValid() || px.parent() != m_tree->rootIndex()) continue;
        sel.select(px, px.siblingAtColumn(qMax(0, lastCol)));
        if (!first.isValid() || px.row() < first.row()) first = px;
        if (path == m_pendingRename) renameIdx = px;
        m_pendingSelect.remove(path);
    }
    m_inRetrySelect = false;
    if (sel.isEmpty()) return;
    QItemSelectionModel *sm = m_tree->selectionModel();
    sm->select(sel, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    sm->setCurrentIndex(first, QItemSelectionModel::NoUpdate);
    currentView()->scrollTo(first);
    if (renameIdx.isValid()) {
        m_pendingRename.clear();
        focusView();
        currentView()->edit(renameIdx.siblingAtColumn(Name));
    }
}

void FilePane::renameCurrent() {
    QModelIndex cur = m_tree->selectionModel()->currentIndex();
    if (!cur.isValid() || !m_tree->selectionModel()->isSelected(cur)) {
        const QModelIndexList rows = m_tree->selectionModel()->selectedRows(Name);
        if (rows.isEmpty()) return;
        cur = rows.first();
    }
    focusView();
    currentView()->edit(cur.siblingAtColumn(Name));
}

QString FilePane::newFolder() {
    if (m_path.isEmpty()) return {};
    const QString name = freeName(m_path, tr("New folder"), true);
    const QModelIndex idx = m_fs->mkdir(m_fs->index(m_path), name);
    if (!idx.isValid()) {
        emit errorMessage(tr("Could not create a folder in \"%1\".").arg(displayPath()));
        return {};
    }
    const QString p = QDir(m_path).filePath(name);
    m_pendingRename = p;
    selectPaths({p});
    return p;
}

// --- view mode, hidden files, focus -----------------------------------------

QAbstractItemView *FilePane::currentView() const {
    return m_mode == ViewMode::Details ? static_cast<QAbstractItemView *>(m_tree)
                                       : static_cast<QAbstractItemView *>(m_list);
}

void FilePane::setViewMode(ViewMode mode) {
    const bool hadFocus = currentView()->hasFocus();
    m_mode = mode;
    m_stack->setCurrentWidget(currentView());
    const QModelIndex cur = m_tree->selectionModel()->currentIndex();
    if (cur.isValid()) currentView()->scrollTo(cur);
    if (hadFocus) focusView();
}

void FilePane::setShowHidden(bool show) {
    if (show == m_showHidden) return;
    m_showHidden = show;
    m_fs->setFilter(filtersFor(show));
}

void FilePane::setActive(bool active) {
    QPalette pal = m_activeBar->palette();
    pal.setColor(QPalette::Window, active ? palette().color(QPalette::Highlight)
                                          : palette().color(QPalette::Window));
    m_activeBar->setPalette(pal);
}

void FilePane::focusView() { currentView()->setFocus(Qt::OtherFocusReason); }

void FilePane::focusAddressBar() {
    m_address->setFocus(Qt::ShortcutFocusReason);
    m_address->selectAll();
}

void FilePane::focusSearch() {
    m_search->setFocus(Qt::ShortcutFocusReason);
    m_search->selectAll();
}

void FilePane::setVolumes(const QList<VolumeInfo> &volumes) {
    m_volumes = volumes;
    scheduleStatus();
}

QByteArray FilePane::headerState() const { return m_tree->header()->saveState(); }

void FilePane::restoreHeaderState(const QByteArray &state) {
    if (!state.isEmpty()) m_tree->header()->restoreState(state);
}

QStringList FilePane::visibleNames() const {
    QStringList out;
    const QModelIndex root = m_tree->rootIndex();
    for (int r = 0; r < m_proxy->rowCount(root); ++r)
        out.append(m_proxy->index(r, Name, root).data(Qt::DisplayRole).toString());
    return out;
}

// --- status line ----------------------------------------------------------------

void FilePane::scheduleStatus() { m_statusTimer->start(); }

void FilePane::updateStatus() {
    const int n = m_proxy->rowCount(m_tree->rootIndex());
    QString s = n == 1 ? tr("1 item") : tr("%1 items").arg(n);

    const QModelIndexList rows = m_tree->selectionModel()->selectedRows(Name);
    if (!rows.isEmpty()) {
        qint64 bytes = 0;
        bool anyFile = false;
        for (const QModelIndex &r : rows) {
            const QModelIndex src = m_proxy->mapToSource(r);
            if (!m_fs->isDir(src)) {
                bytes += m_fs->size(src);
                anyFile = true;
            }
        }
        s += QStringLiteral("     ") +
             (rows.size() == 1 ? tr("1 item selected") : tr("%1 items selected").arg(rows.size()));
        if (anyFile) s += QStringLiteral("  ") + humanSize(bytes);
    }
    m_status->setText(s);

    const VolumeInfo *v = m_path.isEmpty() ? nullptr : volumeFor(m_volumes, m_path);
    m_free->setText(v && v->bytesTotal > 0 ? tr("%1 free of %2")
                                                 .arg(humanSize(v->bytesFree),
                                                      humanSize(v->bytesTotal))
                                           : QString());
}

// --- events -----------------------------------------------------------------------

void FilePane::showViewContextMenu(QAbstractItemView *view, const QPoint &pos) {
    const QModelIndex idx = view->indexAt(pos);
    QItemSelectionModel *sm = view->selectionModel();
    if (!idx.isValid()) {
        // Right-click on empty space acts on the folder, not on a selection
        // the user may not even be able to see.
        sm->clearSelection();
        sm->setCurrentIndex(QModelIndex(), QItemSelectionModel::NoUpdate);
    } else if (!sm->isSelected(idx)) {
        sm->select(idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        sm->setCurrentIndex(idx, QItemSelectionModel::NoUpdate);
    }
    emit activated(this);
    emit contextMenuRequested(view->viewport()->mapToGlobal(pos));
}

bool FilePane::eventFilter(QObject *watched, QEvent *event) {
    switch (event->type()) {
    case QEvent::FocusIn:
    case QEvent::MouseButtonPress:
        emit activated(this);
        break;
    case QEvent::KeyPress: {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (watched == m_address && ke->key() == Qt::Key_Escape) {
            m_address->setText(displayPath());
            focusView();
            return true;
        }
        if (watched == m_search && ke->key() == Qt::Key_Escape) {
            m_search->clear();
            focusView();
            return true;
        }
        if ((watched == m_tree || watched == m_list) && ke->key() == Qt::Key_Backspace &&
            ke->modifiers() == Qt::NoModifier) {
            back();  // Explorer's Backspace
            return true;
        }
        break;
    }
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

}  // namespace anyfile
