// core/tests/test_clipboard.cpp
#include <QDir>
#include <QMimeData>
#include <QUrl>
#include <QtEndian>

#include <memory>

#include "anyfile/clipboard.h"
#include "harness.h"

using namespace anyfile;

TEST(clipboard_round_trip_cut_and_copy) {
    const QStringList paths{"/tmp/a b.txt", "/tmp/dir"};
    std::unique_ptr<QMimeData> cut(makeClipboardData(paths, true));
    ClipboardFiles f = readClipboardData(cut.get());
    CHECK(f.cut);
    CHECK_EQ(f.paths, paths);
    CHECK(isOurCut(cut.get()));

    std::unique_ptr<QMimeData> copy(makeClipboardData(paths, false));
    f = readClipboardData(copy.get());
    CHECK(!f.cut);
    CHECK_EQ(f.paths, paths);
    CHECK(!isOurCut(copy.get()));
}

TEST(clipboard_writes_every_platforms_cut_marker) {
    std::unique_ptr<QMimeData> md(makeClipboardData({"/tmp/x"}, true));
    CHECK_EQ(md->data("application/x-kde-cutselection"), QByteArray("1"));
    CHECK(md->data("x-special/gnome-copied-files").startsWith("cut\nfile:///tmp/x"));
    const QByteArray e =
        md->data("application/x-qt-windows-mime;value=\"Preferred DropEffect\"");
    CHECK_EQ(int(e.size()), 4);
    CHECK_EQ(qFromLittleEndian<quint32>(e.constData()), quint32(2));
    // Plain text is for pasting into a terminal or an editor, so it carries
    // the platform's separators: \tmp\x on Windows.
    CHECK_EQ(md->text(), QDir::toNativeSeparators("/tmp/x"));
}

TEST(clipboard_reads_nautilus_only_format) {
    QMimeData md;
    md.setData("x-special/gnome-copied-files", "cut\nfile:///tmp/one\nfile:///tmp/two%20x");
    const ClipboardFiles f = readClipboardData(&md);
    CHECK(f.cut);
    CHECK_EQ(f.paths, (QStringList{"/tmp/one", "/tmp/two x"}));
}

TEST(clipboard_reads_dolphin_cut) {
    QMimeData md;
    md.setUrls({QUrl::fromLocalFile("/tmp/k")});
    md.setData("application/x-kde-cutselection", "1");
    CHECK(readClipboardData(&md).cut);
}

TEST(clipboard_reads_explorer_drop_effect) {
    QMimeData md;
    md.setUrls({QUrl::fromLocalFile("/tmp/w")});
    QByteArray e(4, '\0');
    qToLittleEndian<quint32>(2, e.data());
    md.setData("application/x-qt-windows-mime;value=\"Preferred DropEffect\"", e);
    CHECK(readClipboardData(&md).cut);
    qToLittleEndian<quint32>(5, e.data());
    md.setData("application/x-qt-windows-mime;value=\"Preferred DropEffect\"", e);
    CHECK(!readClipboardData(&md).cut);
}

TEST(clipboard_ignores_remote_urls) {
    QMimeData md;
    md.setUrls({QUrl("https://example.com/x"), QUrl::fromLocalFile("/tmp/y")});
    CHECK_EQ(readClipboardData(&md).paths, QStringList{"/tmp/y"});
}
