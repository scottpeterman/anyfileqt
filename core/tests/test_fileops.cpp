// core/tests/test_fileops.cpp
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <optional>
#include <thread>

#include "anyfile/fileop.h"
#include "anyfile/operationqueue.h"
#include "anyfile/volumes.h"
#include "fixtures.h"
#include "harness.h"

using namespace anyfile;

namespace {

struct Ran {
    JobResult r;
    int conflicts = 0;
};

// Runs a job on this thread. When answer is set, every conflict is answered
// with it; otherwise nothing is connected and the job's default applies.
Ran runJob(OpKind kind, const QStringList &src, const QString &dst,
           std::optional<ConflictChoice> answer = std::nullopt, bool applyAll = false) {
    FileJob job(kind, src, dst);
    Ran out;
    if (answer) {
        QObject::connect(&job, &FileJob::conflict, [&](const ConflictInfo &) {
            ++out.conflicts;
            job.answerConflict(*answer, applyAll);
        });
    }
    job.run();
    out.r = job.result();
    return out;
}

bool noPartials(const QString &dir) {
    for (const QString &n : fx::names(dir))
        if (n.endsWith(".anyfile-part")) return false;
    return true;
}

void setMtime(const QString &path, const QDateTime &when) {
    QFile f(path);
    if (!f.open(QIODevice::ReadWrite)) return;
    f.setFileTime(when, QFileDevice::FileModificationTime);
}

}  // namespace

TEST(copy_file_keeps_content_and_mtime) {
    QTemporaryDir t;
    const QString src = t.path() + "/src/report.txt";
    fx::put(src, "hello");
    const QDateTime old(QDate(2020, 1, 2), QTime(3, 4, 5));
    setMtime(src, old);
    QDir().mkpath(t.path() + "/dst");

    const Ran r = runJob(OpKind::Copy, {src}, t.path() + "/dst");
    CHECK_EQ(int(r.r.errors.size()), 0);
    CHECK_EQ(fx::get(t.path() + "/dst/report.txt"), QByteArray("hello"));
    CHECK_EQ(QFileInfo(t.path() + "/dst/report.txt").lastModified().toSecsSinceEpoch(),
             old.toSecsSinceEpoch());
    CHECK(fx::exists(src));
    CHECK(noPartials(t.path() + "/dst"));
    CHECK_EQ(r.r.created, QStringList{t.path() + "/dst/report.txt"});
}

TEST(copy_tree_recursive) {
    QTemporaryDir t;
    const QString s = t.path() + "/src/tree";
    fx::put(s + "/a.txt", "A");
    fx::put(s + "/sub/b.bin", fx::pattern(300 * 1024));
    QDir().mkpath(s + "/empty");
    fx::put(s + "/.hidden", "h");
    QDir().mkpath(t.path() + "/dst");

    FileJob job(OpKind::Copy, {s}, t.path() + "/dst");
    job.setChunkSize(64 * 1024);  // several chunks for b.bin
    int progressEvents = 0;
    QObject::connect(&job, &FileJob::progress, [&](const JobProgress &) { ++progressEvents; });
    job.run();
    const JobResult &r = job.result();

    CHECK_EQ(int(r.errors.size()), 0);
    CHECK_EQ(r.itemsDone, 6);  // tree, .hidden, a.txt, empty, sub, sub/b.bin
    CHECK_EQ(fx::get(t.path() + "/dst/tree/a.txt"), QByteArray("A"));
    CHECK_EQ(fx::get(t.path() + "/dst/tree/.hidden"), QByteArray("h"));
    CHECK_EQ(fx::get(t.path() + "/dst/tree/sub/b.bin"), fx::pattern(300 * 1024));
    CHECK(QFileInfo(t.path() + "/dst/tree/empty").isDir());
    CHECK(progressEvents >= 2);  // at least the scan start and the final Done
}

TEST(copy_into_same_folder_names_like_explorer) {
    QTemporaryDir t;
    const QString d = t.path();
    fx::put(d + "/report.txt", "1");
    runJob(OpKind::Copy, {d + "/report.txt"}, d);
    runJob(OpKind::Copy, {d + "/report.txt"}, d);
    CHECK_EQ(fx::names(d),
             (QStringList{"report - Copy (2).txt", "report - Copy.txt", "report.txt"}));
}

TEST(conflict_skip_replace_keep_both) {
    QTemporaryDir t;
    const QString src = t.path() + "/src/a.txt";
    const QString dst = t.path() + "/dst";
    fx::put(src, "new");
    fx::put(dst + "/a.txt", "old");

    Ran r = runJob(OpKind::Copy, {src}, dst, ConflictChoice::Skip);
    CHECK_EQ(r.conflicts, 1);
    CHECK_EQ(fx::get(dst + "/a.txt"), QByteArray("old"));

    r = runJob(OpKind::Copy, {src}, dst, ConflictChoice::KeepBoth);
    CHECK_EQ(fx::get(dst + "/a.txt"), QByteArray("old"));
    CHECK_EQ(fx::get(dst + "/a (2).txt"), QByteArray("new"));
    CHECK_EQ(r.r.created, QStringList{dst + "/a (2).txt"});

    r = runJob(OpKind::Copy, {src}, dst, ConflictChoice::Replace);
    CHECK_EQ(fx::get(dst + "/a.txt"), QByteArray("new"));
    CHECK(noPartials(dst));
}

TEST(conflict_apply_to_all_asks_once) {
    QTemporaryDir t;
    const QString s = t.path() + "/src", d = t.path() + "/dst";
    for (const char *n : {"1.txt", "2.txt", "3.txt"}) {
        fx::put(s + "/" + n, "new");
        fx::put(d + "/" + n, "old");
    }
    const Ran r = runJob(OpKind::Copy, {s + "/1.txt", s + "/2.txt", s + "/3.txt"}, d,
                         ConflictChoice::Replace, true);
    CHECK_EQ(r.conflicts, 1);
    CHECK_EQ(fx::get(d + "/3.txt"), QByteArray("new"));
}

TEST(conflict_with_nothing_connected_skips_rather_than_hangs) {
    QTemporaryDir t;
    fx::put(t.path() + "/src/a.txt", "new");
    fx::put(t.path() + "/dst/a.txt", "old");
    const Ran r = runJob(OpKind::Copy, {t.path() + "/src/a.txt"}, t.path() + "/dst");
    CHECK_EQ(fx::get(t.path() + "/dst/a.txt"), QByteArray("old"));
    CHECK(!r.r.cancelled);
}

TEST(conflict_cancel_stops_the_job) {
    QTemporaryDir t;
    const QString s = t.path() + "/src", d = t.path() + "/dst";
    fx::put(s + "/1.txt", "new");
    fx::put(s + "/2.txt", "new");
    fx::put(d + "/1.txt", "old");
    const Ran r = runJob(OpKind::Copy, {s + "/1.txt", s + "/2.txt"}, d, ConflictChoice::Cancel);
    CHECK(r.r.cancelled);
    CHECK(!fx::exists(d + "/2.txt"));
}

TEST(folder_onto_folder_merges) {
    QTemporaryDir t;
    const QString s = t.path() + "/src/proj", d = t.path() + "/dst";
    fx::put(s + "/keep.txt", "src");
    fx::put(s + "/clash.txt", "src");
    fx::put(d + "/proj/existing.txt", "dst");
    fx::put(d + "/proj/clash.txt", "dst");

    const Ran r = runJob(OpKind::Copy, {s}, d, ConflictChoice::Skip);
    CHECK_EQ(r.conflicts, 1);  // only the file that collides
    CHECK_EQ(fx::names(d + "/proj"), (QStringList{"clash.txt", "existing.txt", "keep.txt"}));
    CHECK_EQ(fx::get(d + "/proj/clash.txt"), QByteArray("dst"));
    CHECK(r.r.created.isEmpty());  // merged into an existing folder: nothing new at the top
}

TEST(move_on_one_volume_is_a_rename) {
    QTemporaryDir t;
    const QString s = t.path() + "/src/tree";
    fx::put(s + "/a.txt", "A");
    QDir().mkpath(t.path() + "/dst");
    const Ran r = runJob(OpKind::Move, {s}, t.path() + "/dst");
    CHECK_EQ(int(r.r.errors.size()), 0);
    CHECK(!fx::exists(s));
    CHECK_EQ(fx::get(t.path() + "/dst/tree/a.txt"), QByteArray("A"));
    CHECK_EQ(r.r.created, QStringList{t.path() + "/dst/tree"});
}

TEST(move_with_merge_removes_only_what_was_copied) {
    QTemporaryDir t;
    const QString s = t.path() + "/src/proj", d = t.path() + "/dst";
    fx::put(s + "/moved.txt", "m");
    fx::put(s + "/sub/clash.txt", "src");
    fx::put(d + "/proj/sub/clash.txt", "dst");

    // The rename fast path cannot apply: the destination folder exists.
    const Ran r = runJob(OpKind::Move, {s}, d, ConflictChoice::Skip);
    CHECK_EQ(int(r.r.errors.size()), 0);
    CHECK_EQ(fx::get(d + "/proj/moved.txt"), QByteArray("m"));
    CHECK(!fx::exists(s + "/moved.txt"));
    // The skipped file stays at the source, and so does every folder above it.
    CHECK_EQ(fx::get(s + "/sub/clash.txt"), QByteArray("src"));
    CHECK_EQ(fx::get(d + "/proj/sub/clash.txt"), QByteArray("dst"));
}

TEST(move_into_the_folder_it_is_in_does_nothing) {
    QTemporaryDir t;
    fx::put(t.path() + "/a.txt", "A");
    const Ran r = runJob(OpKind::Move, {t.path() + "/a.txt"}, t.path());
    CHECK_EQ(int(r.r.errors.size()), 0);
    CHECK_EQ(fx::names(t.path()), QStringList{"a.txt"});
}

TEST(copy_folder_into_itself_is_refused) {
    QTemporaryDir t;
    const QString s = t.path() + "/tree";
    fx::put(s + "/sub/a.txt", "A");
    const Ran r = runJob(OpKind::Copy, {s}, s + "/sub");
    CHECK_EQ(int(r.r.errors.size()), 1);
    CHECK_EQ(fx::names(s + "/sub"), QStringList{"a.txt"});
}

TEST(missing_source_is_an_error_not_a_crash) {
    QTemporaryDir t;
    QDir().mkpath(t.path() + "/dst");
    const Ran r = runJob(OpKind::Copy, {t.path() + "/gone.txt"}, t.path() + "/dst");
    CHECK_EQ(int(r.r.errors.size()), 1);
}

TEST(cancel_mid_file_leaves_nothing_behind) {
    QTemporaryDir t;
    const QString src = t.path() + "/src/big.bin";
    const QString dst = t.path() + "/dst";
    fx::put(src, fx::pattern(4 * 1024 * 1024));
    QDir().mkpath(dst);

    FileJob job(OpKind::Copy, {src}, dst);
    job.setChunkSize(64 * 1024);
    job.setProgressInterval(0);
    // Paused from inside the copy, after the first chunk has been written, so
    // the cancel below lands mid-file. Direct connection: runs on the worker.
    QObject::connect(&job, &FileJob::progress, [&](const JobProgress &p) {
        if (p.phase == JobProgress::Working && p.bytesDone > 0) job.setPaused(true);
    });
    std::thread worker([&] { job.run(); });

    QElapsedTimer w;
    w.start();
    while (!fx::exists(dst + "/.~big.bin.anyfile-part") && w.elapsed() < 3000)
        QThread::msleep(10);
    CHECK(fx::exists(dst + "/.~big.bin.anyfile-part"));  // proves it was mid-file

    job.cancel();
    worker.join();
    CHECK(job.result().cancelled);
    CHECK_EQ(fx::names(dst), QStringList());
}

TEST(pause_then_resume_completes) {
    QTemporaryDir t;
    const QString src = t.path() + "/src/big.bin";
    fx::put(src, fx::pattern(1024 * 1024));
    QDir().mkpath(t.path() + "/dst");

    FileJob job(OpKind::Copy, {src}, t.path() + "/dst");
    job.setChunkSize(64 * 1024);
    job.setPaused(true);
    std::thread worker([&] { job.run(); });
    QThread::msleep(150);
    CHECK(!fx::exists(t.path() + "/dst/big.bin"));
    job.setPaused(false);
    worker.join();
    CHECK(!job.result().cancelled);
    CHECK_EQ(fx::get(t.path() + "/dst/big.bin"), fx::pattern(1024 * 1024));
}

TEST(delete_removes_a_tree) {
    QTemporaryDir t;
    const QString s = t.path() + "/tree";
    fx::put(s + "/a.txt", "A");
    fx::put(s + "/sub/deeper/b.txt", "B");
    const Ran r = runJob(OpKind::Delete, {s}, QString());
    CHECK_EQ(int(r.r.errors.size()), 0);
    CHECK(!fx::exists(s));
    CHECK_EQ(r.r.itemsDone, 5);
}

#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
TEST(trash_moves_into_the_freedesktop_trash) {
    // XDG_DATA_HOME is a scratch directory -- see test_main.cpp -- so this is
    // that trash, not the user's. Temp and XDG_DATA_HOME share /tmp, so Qt
    // uses the home trash rather than a per-volume .Trash-<uid>.
    QTemporaryDir t;
    fx::put(t.path() + "/junk.txt", "J");
    const Ran r = runJob(OpKind::Trash, {t.path() + "/junk.txt"}, QString());
    CHECK_EQ(int(r.r.errors.size()), 0);
    CHECK(!fx::exists(t.path() + "/junk.txt"));
    const QString trash = qEnvironmentVariable("XDG_DATA_HOME") + "/Trash/files";
    CHECK(fx::names(trash).contains("junk.txt"));
}
#endif

#ifndef Q_OS_WIN
TEST(symlink_is_copied_as_a_link_and_stays_relative) {
    QTemporaryDir t;
    const QString s = t.path() + "/src/tree";
    fx::put(s + "/real.txt", "R");
    QFile::link("real.txt", s + "/link.txt");  // relative link text
    QDir().mkpath(t.path() + "/dst");

    const Ran r = runJob(OpKind::Copy, {s}, t.path() + "/dst");
    CHECK_EQ(int(r.r.errors.size()), 0);
    const QFileInfo li(t.path() + "/dst/tree/link.txt");
    CHECK(li.isSymLink());
    // Resolves inside the copy, not back into the source tree.
    CHECK_EQ(QFileInfo(li.symLinkTarget()).canonicalFilePath(),
             QFileInfo(t.path() + "/dst/tree/real.txt").canonicalFilePath());
}
#endif

TEST(queue_runs_jobs_in_order_on_a_worker) {
    QTemporaryDir t;
    fx::put(t.path() + "/src/1.txt", "1");
    fx::put(t.path() + "/src/2.txt", "2");
    QDir().mkpath(t.path() + "/dst");

    OperationQueue q;
    QStringList order;
    bool offThread = true;
    QObject::connect(&q, &OperationQueue::jobFinished,
                     [&](FileJob *j, const JobResult &) {
                         order << QFileInfo(j->sources().first()).fileName();
                     });
    FileJob *a = q.enqueue(OpKind::Copy, {t.path() + "/src/1.txt"}, t.path() + "/dst");
    q.enqueue(OpKind::Copy, {t.path() + "/src/2.txt"}, t.path() + "/dst");
    QObject::connect(a, &FileJob::progress, a,
                     [&](const JobProgress &) {
                         // Delivered to the UI thread, i.e. queued: we are on it.
                         offThread = offThread && QThread::currentThread() ==
                                                      QCoreApplication::instance()->thread();
                     });

    QEventLoop loop;
    QObject::connect(&q, &OperationQueue::idle, &loop, &QEventLoop::quit);
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();

    CHECK_EQ(order, (QStringList{"1.txt", "2.txt"}));
    CHECK(offThread);
    CHECK(!q.isBusy());
    CHECK_EQ(fx::names(t.path() + "/dst"), (QStringList{"1.txt", "2.txt"}));
}

TEST(volumes_scan_finds_the_root_and_maps_paths) {
    const QList<VolumeInfo> v = scanVolumes();
    CHECK(!v.isEmpty());
    const VolumeInfo *home = volumeFor(v, QDir::homePath());
    CHECK(home != nullptr);
    if (home) CHECK(home->bytesTotal > 0);
}
