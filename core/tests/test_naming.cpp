// core/tests/test_naming.cpp
#include <QTemporaryDir>

#include "anyfile/naming.h"
#include "fixtures.h"
#include "harness.h"

using namespace anyfile;

TEST(naming_split_at_last_dot) {
    CHECK_EQ(splitName("report.txt", false).base, QString("report"));
    CHECK_EQ(splitName("report.txt", false).ext, QString(".txt"));
    CHECK_EQ(splitName("archive.tar.gz", false).base, QString("archive.tar"));
    CHECK_EQ(splitName("archive.tar.gz", false).ext, QString(".gz"));
    CHECK_EQ(splitName("Makefile", false).ext, QString());
}

TEST(naming_dotfile_is_all_base) {
    CHECK_EQ(splitName(".bashrc", false).base, QString(".bashrc"));
    CHECK_EQ(splitName(".bashrc", false).ext, QString());
    CHECK_EQ(splitName(".config.json", false).ext, QString(".json"));
}

TEST(naming_folders_have_no_extension) {
    CHECK_EQ(splitName("v1.2", true).base, QString("v1.2"));
    CHECK_EQ(splitName("v1.2", true).ext, QString());
}

TEST(naming_copy_name_sequence) {
    QTemporaryDir t;
    const QString d = t.path();
    CHECK_EQ(copyName(d, "report.txt", false), QString("report - Copy.txt"));
    fx::put(d + "/report - Copy.txt", "x");
    CHECK_EQ(copyName(d, "report.txt", false), QString("report - Copy (2).txt"));
    fx::put(d + "/report - Copy (2).txt", "x");
    CHECK_EQ(copyName(d, "report.txt", false), QString("report - Copy (3).txt"));
    CHECK_EQ(copyName(d, "v1.2", true), QString("v1.2 - Copy"));
    CHECK_EQ(copyName(d, ".bashrc", false), QString(".bashrc - Copy"));
}

TEST(naming_keep_both_and_new_folder) {
    QTemporaryDir t;
    const QString d = t.path();
    CHECK_EQ(keepBothName(d, "a.txt", false), QString("a (2).txt"));
    fx::put(d + "/a (2).txt", "x");
    CHECK_EQ(keepBothName(d, "a.txt", false), QString("a (3).txt"));
    CHECK_EQ(freeName(d, "New folder", true), QString("New folder"));
    QDir(d).mkdir("New folder");
    CHECK_EQ(freeName(d, "New folder", true), QString("New folder (2)"));
}

#ifndef Q_OS_WIN
TEST(naming_dangling_symlink_counts_as_taken) {
    QTemporaryDir t;
    const QString d = t.path();
    QFile::link(d + "/nowhere", d + "/a.txt");
    CHECK(pathExists(d + "/a.txt"));
    CHECK_EQ(freeName(d, "a.txt", false), QString("a (2).txt"));
}
#endif

TEST(naming_natural_compare) {
    CHECK(naturalCompare("file2.txt", "file10.txt") < 0);
    CHECK(naturalCompare("file10.txt", "file2.txt") > 0);
    CHECK(naturalCompare("File2", "file10") < 0);  // case-insensitive
    CHECK(naturalCompare("a", "B") < 0);
    CHECK(naturalCompare("img1", "img01") < 0);    // same value, fewer zeros first
    CHECK(naturalCompare("x99999999999999999999999", "x100000000000000000000000") < 0);
    CHECK(naturalCompare("1abc", "abc") < 0);
    CHECK(naturalCompare("abc", "abc") == 0);
    CHECK(naturalCompare("abc", "abcd") < 0);
}
