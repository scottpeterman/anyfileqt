// app/platform/platform.h
//
// The seam for everything that is one platform's business. The rest of the
// application calls these and does not #ifdef.
//
// What is here today is small and process-launching. What belongs here next,
// each behind a function with a portable fallback:
//
//   Windows   the Explorer shell context menu (IContextMenu over the selection,
//             so 7-Zip, Git, Open with and the cloud-sync verbs appear),
//             IThumbnailCache thumbnails, This PC / Recycle Bin through the
//             shell namespace, drive arrival and removal (WM_DEVICECHANGE)
//   macOS     Quick Look (QLPreviewPanel, an .mm file), Finder tags
//   Linux     freedesktop thumbnails (~/.cache/thumbnails), udisks mount events
#pragma once

#include <QString>

namespace anyfile::platform {

// "Explorer", "Finder", or "Files".
QString fileManagerName();

// Opens the system file manager with path selected.
bool revealInFileManager(const QString &path);

// Opens a terminal whose working directory is dir.
bool openTerminalAt(const QString &dir);

}  // namespace anyfile::platform
