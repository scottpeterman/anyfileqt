// core/include/anyfile/naming.h
//
// The names Explorer gives things, so a file manager that looks like Explorer
// also names like it:
//
//   paste into the same folder     report.txt  -> report - Copy.txt
//                                              -> report - Copy (2).txt
//   "Keep both" on a conflict      report.txt  -> report (2).txt
//   New folder                     New folder  -> New folder (2)
//
// The extension is split at the LAST dot, as Explorer does it, so
// archive.tar.gz becomes "archive.tar - Copy.gz". A leading dot with no other
// dot (.bashrc) is the whole name, not an extension -- otherwise the copy is
// " - Copy.bashrc", which is a hidden file named after nothing.
#pragma once

#include <QString>

namespace anyfile {

struct NameParts {
    QString base;
    QString ext;  // including the dot, or empty
};

NameParts splitName(const QString &fileName, bool isDir);

// True for a path that exists OR is a dangling symlink. QFileInfo::exists()
// follows links, so it says false for a broken one -- and then writing to that
// name replaces the link with no conflict prompt.
bool pathExists(const QString &path);

// "<base> - Copy<ext>", then "<base> - Copy (2)<ext>", ... -- the first that
// does not exist in dir.
QString copyName(const QString &dir, const QString &fileName, bool isDir);

// "<base> (2)<ext>", "<base> (3)<ext>", ... -- the first that does not exist.
QString keepBothName(const QString &dir, const QString &fileName, bool isDir);

// fileName itself if it is free, otherwise keepBothName(). For New folder.
QString freeName(const QString &dir, const QString &fileName, bool isDir);

// "Recycle Bin" on Windows, "Trash" everywhere else.
QString trashDisplayName();

// Explorer's ordering (StrCmpLogicalW): runs of digits compare as numbers, so
// file2 sorts before file10; everything else compares case-insensitively and
// locale-aware. <0, 0, >0 like strcmp.
//
// Not QCollator::setNumericMode(). In the C locale -- which is what a process
// gets with LANG unset, and so what a test run or a service account gets --
// QCollator falls back to a plain string compare and ignores numeric mode,
// and file10 sorts before file2 with no warning.
int naturalCompare(const QString &a, const QString &b);

}  // namespace anyfile
