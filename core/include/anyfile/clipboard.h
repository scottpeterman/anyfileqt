// core/include/anyfile/clipboard.h
//
// Cut, copy and paste of files, in the formats the other file managers on
// each platform read and write -- so Ctrl+X here and Ctrl+V in Explorer, or
// the reverse, moves the file instead of copying it.
//
//   every platform   text/uri-list (the files), text/plain (their paths, so a
//                    paste into a terminal or an editor gets something useful)
//   Windows          "Preferred DropEffect": a DWORD, 2 for a cut, 5 for a copy
//   KDE / Dolphin    application/x-kde-cutselection: "1" for a cut
//   GNOME / Nautilus x-special/gnome-copied-files: "cut\n<url>\n..." or "copy\n..."
//   macOS            Finder has no cut. Only this application's own marker
//                    carries it, so a cut here pastes as a move here and as a
//                    copy in Finder -- which is what Finder would do anyway.
//
// All of them are written on every platform; it costs nothing and a Linux
// desktop may run either DE's file manager.
//
// The Windows format goes through Qt's application/x-qt-windows-mime;value=
// scheme, which registers and reads arbitrary clipboard formats by name.
// NOT VERIFIED ON WINDOWS.
#pragma once

#include <QStringList>

class QMimeData;

namespace anyfile {

struct ClipboardFiles {
    QStringList paths;  // absolute, local files only
    bool cut = false;
};

// The caller owns the result; QClipboard::setMimeData takes ownership.
QMimeData *makeClipboardData(const QStringList &paths, bool cut);

ClipboardFiles readClipboardData(const QMimeData *data);

// True when data is a cut that this application wrote. After the paste of a
// cut completes, the clipboard is cleared, as Explorer does -- but only if it
// still holds that cut and not something copied since.
bool isOurCut(const QMimeData *data);

}  // namespace anyfile
