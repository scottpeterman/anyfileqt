// app/tests/test_app.cpp
//
// The application layer, offscreen: panes, the proxy's ordering and filter,
// navigation history, the drop rules, paste through the queue, and settings.
//
//   QT_QPA_PLATFORM=offscreen anyfile_app_tests [substr]
#include <QApplication>
#include <QElapsedTimer>
#include <QFileSystemModel>
#include <QHash>
#include <QLocale>
#include <QMimeData>
#include <QSettings>
#include <QTemporaryDir>
#include <QTreeView>

#include <functional>
#include <memory>

#include "anyfile/clipboard.h"
#include "anyfile/operationqueue.h"
#include "app/dropaction.h"
#include "app/filepane.h"
#include "app/fsproxy.h"
#include "app/mainwindow.h"
#include "app/navpane.h"
#include "app/opspanel.h"
#include "app/propertiesdialog.h"
#include "app/settings.h"
#include "fixtures.h"
#include "harness.h"

using namespace anyfile;

namespace {

// QFileSystemModel loads on its own thread; everything visible waits on it.
bool waitFor(const std::function<bool()> &cond, int ms = 5000) {
    QElapsedTimer t;
    t.start();
    while (!cond()) {
        if (t.elapsed() > ms) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return true;
}

}  // namespace

TEST(pane_lists_folders_first_in_natural_order) {
    QTemporaryDir t;
    QDir(t.path()).mkdir("b_dir");
    QDir(t.path()).mkdir("C_dir");
    fx::put(t.path() + "/a_file.txt", "a");
    fx::put(t.path() + "/file10.txt", "x");
    fx::put(t.path() + "/file2.txt", "x");

    FilePane p;
    CHECK(p.setPath(t.path()));
    CHECK(waitFor([&] { return p.visibleNames().size() == 5; }));
    // Folders first on every platform -- QFileSystemModel alone would mix
    // them in on macOS -- case-insensitive, and file2 before file10.
    CHECK_EQ(p.visibleNames(),
             (QStringList{"b_dir", "C_dir", "a_file.txt", "file2.txt", "file10.txt"}));
}

TEST(pane_search_filters_only_the_folder_on_screen) {
    QTemporaryDir t;
    fx::put(t.path() + "/alpha.txt", "a");
    fx::put(t.path() + "/beta.txt", "b");
    fx::put(t.path() + "/alphabet.md", "c");

    FilePane p;
    p.setPath(t.path());
    CHECK(waitFor([&] { return p.visibleNames().size() == 3; }));
    p.proxy()->setNameFilter("ALPHA");
    CHECK(waitFor([&] { return p.visibleNames().size() == 2; }));
    // The root survived the filter. A filter applied at every depth would have
    // removed an ancestor of this folder and with it the root index.
    CHECK(p.detailsView()->rootIndex().isValid());
    CHECK_EQ(p.visibleNames(), (QStringList{"alpha.txt", "alphabet.md"}));
}

TEST(pane_history_back_forward_up) {
    QTemporaryDir t;
    QDir().mkpath(t.path() + "/one/two");
    const QString one = QDir::cleanPath(t.path() + "/one");
    const QString two = QDir::cleanPath(t.path() + "/one/two");

    FilePane p;
    p.setPath(t.path());
    p.setPath(one);
    p.setPath(two);
    CHECK(p.canGoBack());
    CHECK(!p.canGoForward());
    p.back();
    CHECK_EQ(p.path(), one);
    CHECK(p.canGoForward());
    p.forward();
    CHECK_EQ(p.path(), two);
    p.up();
    CHECK_EQ(p.path(), one);
    // Up selects the folder you came from.
    CHECK(waitFor([&] { return p.selectedPaths() == QStringList{two}; }));
}

TEST(pane_rejects_a_missing_folder_and_stays_put) {
    QTemporaryDir t;
    FilePane p;
    p.setPath(t.path());
    CHECK(!p.setPath(t.path() + "/nope"));
    CHECK_EQ(p.path(), QDir::cleanPath(t.path()));
}

TEST(pane_relative_address_is_inside_the_current_folder) {
    QTemporaryDir t;
    QDir().mkpath(t.path() + "/sub");
    FilePane p;
    p.setPath(t.path());
    CHECK(p.setPath("sub"));
    CHECK_EQ(p.path(), QDir::cleanPath(t.path() + "/sub"));
}

TEST(pane_new_folder_is_created_selected_and_numbered) {
    QTemporaryDir t;
    FilePane p;
    p.show();
    p.setPath(t.path());
    const QString a = p.newFolder();
    CHECK_EQ(QFileInfo(a).fileName(), QString("New folder"));
    CHECK(QFileInfo(a).isDir());
    CHECK(waitFor([&] { return p.selectedPaths() == QStringList{a}; }));
    const QString b = p.newFolder();
    CHECK_EQ(QFileInfo(b).fileName(), QString("New folder (2)"));
}

TEST(pane_rename_in_place_renames_on_disk) {
    QTemporaryDir t;
    fx::put(t.path() + "/old.txt", "o");
    FilePane p;
    p.setPath(t.path());
    CHECK(waitFor([&] { return p.visibleNames() == QStringList{"old.txt"}; }));
    const QModelIndex idx = p.proxy()->index(0, 0, p.detailsView()->rootIndex());
    CHECK(p.proxy()->setData(idx, "new.txt", Qt::EditRole));
    CHECK(fx::exists(t.path() + "/new.txt"));
    CHECK(!fx::exists(t.path() + "/old.txt"));
}

TEST(pane_refresh_keeps_folder_selection_and_history) {
    QTemporaryDir t;
    QDir().mkpath(t.path() + "/sub");
    fx::put(t.path() + "/sub/a.txt", "a");
    fx::put(t.path() + "/sub/b.txt", "b");
    const QString sub = QDir::cleanPath(t.path() + "/sub");
    FilePane p;
    p.setPath(t.path());
    p.setPath(sub);
    CHECK(waitFor([&] { return p.visibleNames().size() == 2; }));
    p.selectPaths({sub + "/b.txt"});
    CHECK(waitFor([&] { return p.selectedPaths() == QStringList{sub + "/b.txt"}; }));
    // A file the watcher may not have reported yet: refresh must show it.
    fx::put(t.path() + "/sub/c.txt", "c");
    p.refresh();
    CHECK_EQ(p.path(), sub);
    CHECK(waitFor([&] { return p.visibleNames().size() == 3; }));
    CHECK(waitFor([&] { return p.selectedPaths() == QStringList{sub + "/b.txt"}; }));
    p.back();
    CHECK_EQ(p.path(), QDir::cleanPath(t.path()));  // refresh added no history entry
}

#ifndef Q_OS_WIN
TEST(pane_hidden_items_toggle) {
    QTemporaryDir t;
    fx::put(t.path() + "/.secret", "s");
    fx::put(t.path() + "/plain", "p");
    FilePane p;
    p.setPath(t.path());
    CHECK(waitFor([&] { return p.visibleNames() == QStringList{"plain"}; }));
    p.setShowHidden(true);
    CHECK(waitFor([&] { return p.visibleNames().size() == 2; }));
}
#endif

TEST(proxy_shows_explorer_sizes_and_readable_types) {
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    QTemporaryDir t;
    QDir(t.path()).mkdir("folder");
    fx::put(t.path() + "/notes.txt", QByteArray(1812, 'x'));
    fx::put(t.path() + "/capture.pcapzz", "x");
    FilePane p;
    p.setPath(t.path());
    CHECK(waitFor([&] { return p.visibleNames().size() == 3; }));
    const QModelIndex root = p.detailsView()->rootIndex();
    QHash<QString, QPair<QString, QString>> row;  // name -> (type, size)
    for (int r = 0; r < 3; ++r) {
        const auto *m = p.proxy();
        row[m->index(r, 0, root).data().toString()] = {m->index(r, 2, root).data().toString(),
                                                       m->index(r, 1, root).data().toString()};
    }
    CHECK_EQ(row["notes.txt"].second, QString("2 KB"));  // whole KB, rounded up
    CHECK_EQ(row["folder"].second, QString());           // folders have no size
    CHECK(!row["notes.txt"].first.contains('/'));          // a description, not text/plain
    CHECK(!row["notes.txt"].first.isEmpty());
#ifndef Q_OS_WIN
    CHECK_EQ(row["folder"].first, QString("File folder"));
    // From the curated table, so it is the same on Linux and macOS -- the
    // Mac's built-in MIME database has no descriptions to fall back on.
    CHECK_EQ(row["notes.txt"].first, QString("Text Document"));
    CHECK_EQ(row["capture.pcapzz"].first, QString("PCAPZZ File"));
#endif
    QLocale::setDefault(QLocale::c());
}

TEST(properties_counts_a_folder_live) {
    QTemporaryDir t;
    fx::put(t.path() + "/proj/a.txt", QByteArray(1000, 'a'));
    fx::put(t.path() + "/proj/sub/b.txt", QByteArray(2000, 'b'));
    PropertiesDialog d({t.path() + "/proj"});
    CHECK(d.scanner() != nullptr);
    CHECK(waitFor([&] { return d.scanner()->totals().done; }));
    QCoreApplication::processEvents();
    CHECK_EQ(d.containsText(), QString("2 Files, 1 Folders"));
    CHECK(d.sizeText().startsWith("2.92 KB"));
#ifndef Q_OS_WIN
    CHECK_EQ(d.typeText(), QString("File folder"));
#endif
}

TEST(properties_multi_selection_summarises) {
    QTemporaryDir t;
    fx::put(t.path() + "/a.txt", "a");
    fx::put(t.path() + "/b.txt", "bb");
    PropertiesDialog d({t.path() + "/a.txt", t.path() + "/b.txt"});
    CHECK(waitFor([&] { return d.scanner()->totals().done; }));
    CHECK(d.typeText().startsWith("All of type"));
    CHECK(d.sizeText().startsWith("3 bytes"));
}

TEST(properties_rename_applies) {
    QTemporaryDir t;
    fx::put(t.path() + "/old.txt", "o");
    PropertiesDialog d({t.path() + "/old.txt"});
    d.setNameForTest("new.txt");
    CHECK(d.applyChanges());
    CHECK(fx::exists(t.path() + "/new.txt"));
    CHECK(!fx::exists(t.path() + "/old.txt"));
}

#ifndef Q_OS_WIN
TEST(properties_permissions_grid_chmods) {
    QTemporaryDir t;
    fx::put(t.path() + "/key", "k");
    QFile::setPermissions(t.path() + "/key", QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                                 QFileDevice::ReadGroup | QFileDevice::ReadOther);
    PropertiesDialog d({t.path() + "/key"});
    CHECK(d.modeText().contains("644"));
    d.setPermissionBitForTest(1, 0, false);  // group read off
    d.setPermissionBitForTest(2, 0, false);  // others read off
    CHECK(d.modeText().contains("-rw-------"));
    CHECK(d.applyChanges());
    CHECK_EQ(int(QFileInfo(t.path() + "/key").permissions() &
                 (QFileDevice::ReadGroup | QFileDevice::ReadOther)),
             0);
}
#endif

TEST(proxy_uses_explorer_column_names) {
    FilePane p;
    const QAbstractItemModel *m = p.proxy();
    CHECK_EQ(m->headerData(3, Qt::Horizontal).toString(), QString("Date modified"));
    CHECK_EQ(m->headerData(2, Qt::Horizontal).toString(), QString("Type"));
}

TEST(drop_rules_follow_the_platform) {
    QList<VolumeInfo> vols;
    VolumeInfo root;
    root.root = "/";
    VolumeInfo usb;
    usb.root = "/media/usb";
    vols << root << usb;

    const QStringList src{"/home/me/a.txt"};
    CHECK_EQ(int(chooseDropAction(src, "/home/me/dir", Qt::NoModifier, vols)),
             int(Qt::MoveAction));
    CHECK_EQ(int(chooseDropAction(src, "/media/usb/x", Qt::NoModifier, vols)),
             int(Qt::CopyAction));
#ifdef Q_OS_MACOS
    CHECK_EQ(int(chooseDropAction(src, "/home/me/dir", Qt::AltModifier, vols)),
             int(Qt::CopyAction));
    CHECK_EQ(int(chooseDropAction(src, "/media/usb/x", Qt::ControlModifier, vols)),
             int(Qt::MoveAction));
#else
    CHECK_EQ(int(chooseDropAction(src, "/home/me/dir", Qt::ControlModifier, vols)),
             int(Qt::CopyAction));
    CHECK_EQ(int(chooseDropAction(src, "/media/usb/x", Qt::ShiftModifier, vols)),
             int(Qt::MoveAction));
#endif
    // Unknown volume on either end: keep the original.
    CHECK_EQ(int(chooseDropAction(src, "/home/me/dir", Qt::NoModifier, {})),
             int(Qt::CopyAction));
}

TEST(drop_onto_own_folder_or_into_itself_is_ignored) {
    CHECK(isPointlessDrop({"/a/x.txt", "/a/y.txt"}, "/a"));
    CHECK(isPointlessDrop({"/a/dir"}, "/a/dir"));
    CHECK(isPointlessDrop({"/a/dir"}, "/a/dir/sub"));
    CHECK(!isPointlessDrop({"/a/x.txt"}, "/b"));
    CHECK(!isPointlessDrop({"/a/dir"}, "/a/dir2"));  // a prefix, not a parent
}

TEST(navpane_lists_volumes) {
    NavPane n;
    VolumeInfo a;
    a.root = "/";
    a.name = "File System";
    VolumeInfo b;
    b.root = "/Volumes/USB";
    b.name = "USB";
    n.setVolumes({a, b});
    CHECK_EQ(n.volumeRoots(), (QStringList{"/", "/Volumes/USB"}));
    n.setVolumes({a});
    CHECK_EQ(n.volumeRoots(), QStringList{"/"});
}

TEST(window_paste_copies_and_a_cut_moves) {
    QTemporaryDir cfg, t;
    qputenv("ANYFILEQT_CONFIG_DIR", cfg.path().toLocal8Bit());
    fx::put(t.path() + "/src/keep.txt", "K");
    fx::put(t.path() + "/src/move.txt", "M");
    QDir().mkpath(t.path() + "/dst");

    Settings s;
    MainWindow w(&s);
    w.operationsPanel()->setAutoAnswer(ConflictChoice::Skip);
    bool idle = false;
    QObject::connect(w.queue(), &OperationQueue::idle, [&] { idle = true; });

    std::unique_ptr<QMimeData> copy(makeClipboardData({t.path() + "/src/keep.txt"}, false));
    CHECK(w.pasteInto(copy.get(), t.path() + "/dst") != nullptr);
    std::unique_ptr<QMimeData> cut(makeClipboardData({t.path() + "/src/move.txt"}, true));
    CHECK(w.pasteInto(cut.get(), t.path() + "/dst") != nullptr);
    CHECK(waitFor([&] { return idle && !w.queue()->isBusy(); }));

    CHECK_EQ(fx::get(t.path() + "/dst/keep.txt"), QByteArray("K"));
    CHECK(fx::exists(t.path() + "/src/keep.txt"));
    CHECK_EQ(fx::get(t.path() + "/dst/move.txt"), QByteArray("M"));
    CHECK(!fx::exists(t.path() + "/src/move.txt"));
}

TEST(window_conflict_is_answered_through_the_panel) {
    QTemporaryDir cfg, t;
    qputenv("ANYFILEQT_CONFIG_DIR", cfg.path().toLocal8Bit());
    fx::put(t.path() + "/src/a.txt", "new");
    fx::put(t.path() + "/dst/a.txt", "old");

    Settings s;
    MainWindow w(&s);
    w.operationsPanel()->setAutoAnswer(ConflictChoice::KeepBoth);
    bool idle = false;
    QObject::connect(w.queue(), &OperationQueue::idle, [&] { idle = true; });
    std::unique_ptr<QMimeData> copy(makeClipboardData({t.path() + "/src/a.txt"}, false));
    w.pasteInto(copy.get(), t.path() + "/dst");
    CHECK(waitFor([&] { return idle; }));
    CHECK_EQ(fx::get(t.path() + "/dst/a.txt"), QByteArray("old"));
    CHECK_EQ(fx::get(t.path() + "/dst/a (2).txt"), QByteArray("new"));
}

TEST(window_settings_restore_paths_and_layout) {
    QTemporaryDir cfg, t;
    qputenv("ANYFILEQT_CONFIG_DIR", cfg.path().toLocal8Bit());
    QDir().mkpath(t.path() + "/left");
    QDir().mkpath(t.path() + "/right");
    {
        Settings s;
        MainWindow w(&s);
        w.pane(0)->setPath(t.path() + "/left");
        w.pane(1)->setPath(t.path() + "/right");
        w.pane(1)->setViewMode(FilePane::ViewMode::Icons);
        w.setDualPane(false);
        w.saveSettings();
    }
    CHECK(QFileInfo(Settings::configPath()).exists());
    Settings s;
    MainWindow w(&s);
    CHECK_EQ(w.pane(0)->path(), QDir::cleanPath(t.path() + "/left"));
    CHECK_EQ(w.pane(1)->path(), QDir::cleanPath(t.path() + "/right"));
    CHECK(w.pane(1)->viewMode() == FilePane::ViewMode::Icons);
    CHECK(!w.isDualPane());
}

TEST(window_restores_home_when_saved_folder_is_gone) {
    QTemporaryDir cfg;
    qputenv("ANYFILEQT_CONFIG_DIR", cfg.path().toLocal8Bit());
    {
        QSettings raw(Settings::configPath(), QSettings::IniFormat);
        raw.setValue("pane0/path", "/definitely/not/here");
    }
    Settings s;
    MainWindow w(&s);
    CHECK_EQ(w.pane(0)->path(), QDir::cleanPath(QDir::homePath()));
}

int main(int argc, char **argv) {
    QTemporaryDir cfg;
    qputenv("ANYFILEQT_CONFIG_DIR", cfg.path().toLocal8Bit());
    QApplication app(argc, argv);
    return harness::run(argc > 1 ? argv[1] : nullptr);
}
