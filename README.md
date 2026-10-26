# AnyFile

A Windows-styled, dual-pane file manager for Linux, macOS and Windows. C++17 and Qt 6, no other dependencies.

Explorer's layout and habits on every platform: a command bar, a navigation pane with Quick access and This PC, an address bar with back/forward/up, Details and Icons views, Explorer's keyboard shortcuts and naming rules. Two panes side by side, Total Commander style, with an operation queue underneath.

## What works in 0.1.0

- **Browsing**: details and icon views sharing one selection, folders first on every platform, natural sort (file2 before file10), per-folder search, hidden-items toggle, back/forward/up history, typed or relative paths in the address bar with folder completion.
- **File operations on a worker thread**: copy, move, delete to the Trash / Recycle Bin, permanent delete. One queue, run in order, with progress, speed, time left, pause and cancel.
  - Conflicts get Explorer's Replace / Skip / Keep both, with "Do this for all conflicts".
  - Copies are written under a temporary name and renamed into place, so a cancelled or failed copy leaves nothing behind and Replace never destroys the original before the new copy is complete.
  - A move on one volume is a rename, whatever the size of the tree.
  - Modification times and permissions are preserved. On macOS, extended attributes and ACLs are preserved too, including Finder tags and the quarantine flag. On Windows, CopyFileEx is used, which carries attributes and alternate data streams and lets SMB servers copy server-side.
  - Symlinks are copied as links and stay relative.
- **Clipboard interop**: cut/copy/paste of files in Explorer's, Dolphin's and Nautilus's formats, so a cut here pastes as a move there and the other way round.
- **Drag and drop** between panes and with other applications. The platform's rules decide copy versus move:
  - Windows and Linux: Ctrl copies, Shift moves.
  - macOS: Option copies, Command moves.
  - With no key held, a drop on the same volume moves and a drop on another volume copies.
- **Volumes** are scanned off the UI thread, so a dead NFS or SMB mount never freezes the window.
- **Properties** (Alt+Enter, or Cmd+I on macOS): type, location, size and size on disk, folder contents counted live in the background, dates, attributes, rename. On macOS and Linux a Permissions tab shows owner, group and mode, with an editable rwx grid.
- **Explorer's wording**: the Size column is whole KB, the Type column shows readable names rather than MIME types, and every size label uses one formatter.
- **Icons**: the command bar icons are drawn in code, so they look the same on every platform and follow light and dark mode.
- **Extras**: Show in Explorer/Finder/Files, Open in Terminal, Copy as path.

## Not yet

- The Explorer shell context menu (7-Zip, Git, cloud-sync verbs).
- Thumbnails.
- Browsing the Recycle Bin from inside AnyFile.
- Quick Look.
- App icons.

These belong in `app/platform/`; see the note at the top of `app/platform/platform.h`.

## Build

On macOS and Linux, one script configures, builds, quits any running copy and relaunches. If the build fails it stops and prints the compiler errors:

```bash
./scripts/build.sh
```

Add `--test` to run the suites first, `--clean` to start from an empty `build/`, `--no-run` to build only. The full log is in `build/build.log`.

By hand:

Qt 6.2 or newer and CMake 3.21 or newer.

macOS:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=$HOME/Qt/6.10.3/macos
cmake --build build -j
ctest --test-dir build --output-on-failure
open build/app/anyfile.app
```

Linux:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=$HOME/Qt/6.10.3/gcc_64
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/app/anyfile
```

Windows, from an "x64 Native Tools Command Prompt for VS 2022":

```bat
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:\Qt\6.10.3\msvc2022_64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Packaging

Each script builds Release, deploys Qt beside the binary, runs `anyfile --check` from the packaged copy and checks that Qt resolves inside the package:

| Platform | Command | Output |
|---|---|---|
| macOS | `./scripts/bundle-mac.sh --zip`, or `--sign "Developer ID Application: ..." --dmg` | a signed `.app` in a zip or dmg |
| Linux | `./scripts/bundle-linux.sh` | a relocatable tarball with `install.sh` |
| Windows | `scripts\bundle-windows.bat --zip` | an unzip-and-run folder |

## Layout

```
core/            anyfile::core, QtCore only, tested headless
  fileop         the copy/move/trash/delete engine
  operationqueue one worker at a time
  clipboard      the platform clipboard formats
  volumes        the off-thread volume scan
  naming         Explorer's names: "x - Copy", "x (2)", natural order
app/             the Widgets application
  filepane       one pane: address row, views, status line
  fsproxy        folders-first sort, root-only search, drops to the queue
  navpane        Quick access and This PC
  opspanel       the progress strip and the conflict dialog
  mainwindow     actions, menus, command bar, settings
  platform/      everything that is one OS's business
scripts/         bundle-mac.sh, bundle-linux.sh, bundle-windows.bat
```

Settings live in `~/.anyfileqt/settings.ini` on every platform; `ANYFILEQT_CONFIG_DIR` overrides the directory.

## License

GPL-3.0. Qt is used under the LGPL-3.0; see `THIRD_PARTY_NOTICES.md`.
