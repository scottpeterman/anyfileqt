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
// On Windows the shell already provides Explorer's own names ("Text
// Document"), and those are used unchanged.
//
// Cached by extension. UI thread only.
#pragma once

#include <QString>

class QFileInfo;

namespace anyfile {

QString friendlyType(const QFileInfo &fi);

}  // namespace anyfile
