// app/dropaction.h
//
// Whether a drop copies or moves, decided the way each platform's own file
// manager decides it:
//
//   Windows, Linux   Ctrl copies, Shift moves. No key: same volume moves,
//                    another volume copies.
//   macOS            Option copies, Command moves. No key: same as above.
//                    (Qt calls Command "Control" on macOS and Option "Alt".)
//
// "Same volume" comes from the volume list VolumeMonitor already scanned on a
// worker, by path prefix -- not from a QStorageInfo constructed here, which
// would statfs() both ends on the UI thread at the moment of the drop. A
// source or destination that is on no known volume counts as different, so
// the answer in doubt is the one that keeps the original: copy.
#pragma once

#include <QList>
#include <QStringList>
#include <Qt>

#include "anyfile/volumes.h"

namespace anyfile {

Qt::DropAction chooseDropAction(const QStringList &sources, const QString &destDir,
                                Qt::KeyboardModifiers mods, const QList<VolumeInfo> &volumes);

// True when dropping sources onto destDir would do nothing or make no sense:
// every source is already in destDir, or destDir is one of the sources or
// inside one.
bool isPointlessDrop(const QStringList &sources, const QString &destDir);

}  // namespace anyfile
