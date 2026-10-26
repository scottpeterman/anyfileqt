// anyfileqt/app/filetypes.h
//
// The Type column's wording: "File folder", "Plain text document", "PNG image"
// -- never a MIME name.
//
// On macOS and Linux QFileSystemModel's type() is the raw MIME name
// (text/plain, application/x-...). This uses the MIME database's description
// instead, matched by EXTENSION ONLY: content sniffing would open and read
// every file on screen, which on a network share is a stall per row.
//
// On Windows the name comes from the shell (SHGetFileInfo), so it reads
// exactly as Explorer's does. QFileIconProvider::type() does not do that in
// Qt 6 -- it returns the MIME name there too. When the shell has nothing,
// the same table and fallbacks as the other platforms apply.
//
// Cached by extension. UI thread only.
#pragma once

#include <QString>

class QFileInfo;

namespace anyfile {

QString friendlyType(const QFileInfo &fi);

}  // namespace anyfile
