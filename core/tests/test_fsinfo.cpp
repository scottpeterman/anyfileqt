// anyfileqt/core/tests/test_fsinfo.cpp
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLocale>
#include <QTemporaryDir>

#include "anyfile/fsinfo.h"
#include "fixtures.h"
#include "harness.h"

using namespace anyfile;

namespace {
// Group separators and decimal points come from the locale; pin one so the
// expected strings mean something.
struct UsLocale {
    QLocale saved;
    UsLocale() { QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates)); }
    ~UsLocale() { QLocale::setDefault(saved); }
};
}  // namespace

TEST(fsinfo_human_size_matches_explorer) {
    UsLocale us;
    CHECK_EQ(humanSize(0), QString("0 bytes"));
    CHECK_EQ(humanSize(1023), QString("1,023 bytes"));
    CHECK_EQ(humanSize(1024), QString("1.00 KB"));
    CHECK_EQ(humanSize(1812), QString("1.76 KB"));  // 1.769, truncated
    CHECK_EQ(humanSize(18120), QString("17.6 KB"));
    CHECK_EQ(humanSize(181200), QString("176 KB"));
    CHECK_EQ(humanSize(1258291), QString("1.19 MB"));  // 1.1999998 MB: truncated, like Explorer
    CHECK_EQ(humanSize(1258292), QString("1.20 MB"));
    CHECK_EQ(humanSize(qint64(926.4 * 1024 * 1024 * 1024)), QString("926 GB"));
}

TEST(fsinfo_column_size_is_whole_kb_rounded_up) {
    UsLocale us;
    CHECK_EQ(columnSize(0), QString("0 KB"));
    CHECK_EQ(columnSize(1), QString("1 KB"));
    CHECK_EQ(columnSize(1024), QString("1 KB"));
    CHECK_EQ(columnSize(1025), QString("2 KB"));
    CHECK_EQ(columnSize(5000000), QString("4,883 KB"));
}

TEST(fsinfo_size_with_bytes) {
    UsLocale us;
    CHECK_EQ(sizeWithBytes(1812), QString("1.76 KB (1,812 bytes)"));
    CHECK_EQ(sizeWithBytes(222), QString("222 bytes"));
}

TEST(fsinfo_mode_strings) {
    const QFileDevice::Permissions p = QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                       QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                                       QFileDevice::ExeGroup;
    CHECK_EQ(modeString(p, true, false), QString("drwxr-x---"));
    CHECK_EQ(modeOctal(p), QString("750"));
    CHECK_EQ(modeOctal(QFileDevice::ReadOwner | QFileDevice::WriteOwner), QString("600"));
    CHECK_EQ(modeString(QFileDevice::ReadOwner, false, true), QString("lr--------"));
}

TEST(fsinfo_scanner_counts_a_tree) {
    QTemporaryDir t;
    fx::put(t.path() + "/root/a.txt", QByteArray(1000, 'a'));
    fx::put(t.path() + "/root/sub/b.txt", QByteArray(2000, 'b'));
    fx::put(t.path() + "/root/sub/deeper/c.txt", QByteArray(3000, 'c'));
    QDir().mkpath(t.path() + "/root/empty");
#ifndef Q_OS_WIN
    QFile::link("/", t.path() + "/root/link-to-root");  // must not be followed
#endif

    SizeScanner s;
    bool done = false;
    QObject::connect(&s, &SizeScanner::updated, [&](const SizeTotals &x) { done = x.done; });
    s.start({t.path() + "/root"});
    QElapsedTimer w;
    w.start();
    while (!done && w.elapsed() < 5000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

    CHECK(done);
    CHECK_EQ(s.totals().bytes, qint64(6000));
    CHECK_EQ(s.totals().folders, 3);  // sub, deeper, empty -- not root itself
#ifndef Q_OS_WIN
    CHECK_EQ(s.totals().files, 4);    // three files and the link
    CHECK(s.totals().onDisk >= 6000);
#else
    CHECK_EQ(s.totals().files, 3);
#endif
}

TEST(fsinfo_scanner_counts_selected_folders_when_several) {
    QTemporaryDir t;
    fx::put(t.path() + "/one/a.txt", "a");
    fx::put(t.path() + "/two/b.txt", "bb");
    fx::put(t.path() + "/c.txt", "ccc");
    SizeScanner s;
    bool done = false;
    QObject::connect(&s, &SizeScanner::updated, [&](const SizeTotals &x) { done = x.done; });
    s.start({t.path() + "/one", t.path() + "/two", t.path() + "/c.txt"});
    QElapsedTimer w;
    w.start();
    while (!done && w.elapsed() < 5000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    CHECK_EQ(s.totals().folders, 2);
    CHECK_EQ(s.totals().files, 3);
    CHECK_EQ(s.totals().bytes, qint64(6));
}

TEST(fsinfo_scanner_deleted_mid_count_is_safe) {
    QTemporaryDir t;
    for (int i = 0; i < 200; ++i) fx::put(t.path() + QStringLiteral("/d/f%1").arg(i), "x");
    {
        SizeScanner s;
        s.start({t.path() + "/d"});
    }  // destroyed while the worker may still be running
    QElapsedTimer w;
    w.start();
    while (w.elapsed() < 300) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    CHECK(true);  // reaching here without a crash under ASan is the test
}
