# README_Claude_Qt_Sandbox.md

Standing anyfileqt up in a Claude sandbox from cold, so changes get **built and tested** there rather than reasoned about from reading the source.

Shorter than anyeditqt's, because there is less to set up: no FetchContent, no vendored libraries, no corpus. Qt and CMake are the only dependencies.

Every command below was run in the sandbox and every number comes from that run. The sandbox was Ubuntu 24.04, CMake 3.28.3 and the distribution Qt 6.4.2.

## 1. Toolchain

```bash
apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y cmake qt6-base-dev gdb
```

`qt6-base-dev` brings QtCore, QtGui and QtWidgets plus the offscreen platform plugin, which is what makes every test runnable with no display.

A 403 from a third-party apt repo (docker, nodesource) can be ignored; the Ubuntu archives are what matter.

## 2. Configure, build, test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DANYFILE_BUILD_APP=ON -DANYFILE_BUILD_TESTS=ON
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

Pass every option explicitly: `option()` honours an existing cache entry, so a changed default never reaches an existing build directory.

The suites run directly too, and print a case count that ctest hides:

```bash
./build/core/anyfile_core_tests                              # 40 cases
QT_QPA_PLATFORM=offscreen ./build/app/anyfile_app_tests      # 22 cases
./build/core/anyfile_core_tests natural                      # only cases whose name contains "natural"
```

Notes on the suites:

- **Core tests never touch the real Trash.** `test_main.cpp` points `XDG_DATA_HOME` at a temporary directory before anything runs.
- **App tests never touch `~/.anyfileqt`.** They point `ANYFILEQT_CONFIG_DIR` at a temporary directory.

### Sanitizers

Worth running after any change to `fileop.cpp`, the queue or the panes, because the threading is where the bugs will be:

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DANYFILE_BUILD_TESTS=ON -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan -j$(nproc)
ASAN_OPTIONS=detect_leaks=0 ./build-asan/core/anyfile_core_tests
ASAN_OPTIONS=detect_leaks=0 QT_QPA_PLATFORM=offscreen ./build-asan/app/anyfile_app_tests
```

Both suites are clean under ASan and UBSan as of 0.1.0. Leak detection is off because Qt's own one-time allocations at exit swamp it.

## 3. Looking at the window with no display

Write a throwaway harness under the scratchpad, never in the repo. Link it against the built static libraries, construct a `MainWindow`, let the file system models load, and save `grab()`:

```bash
g++ -std=c++17 -fPIC -I. -Icore/include shot.cpp build/app/libanyfile_app.a build/core/libanyfile_core.a $(pkg-config --cflags --libs Qt6Widgets Qt6Gui Qt6Core) -o shot
QT_QPA_PLATFORM=offscreen ./shot <fixture-dir> <config-dir> shot.png
```

The harness must run the event loop for a second or two before `grab()`. `QFileSystemModel` loads on its own thread, and a grab taken straight after `setPath()` shows empty panes.

What offscreen gets wrong, so it is not chased:

- **No file icons.** There is no icon theme and no platform icon provider. The icon view therefore draws text with gaps where icons go. On macOS and Windows the native icons appear.
- **Toolbar buttons without icons show text only.** Cut, Copy, Paste and Rename have no `QStyle` standard icon, only a theme name.
- **Noise on stderr.** `QStandardPaths: XDG_RUNTIME_DIR not set` and `This plugin does not support propagateSizeHints()` can be ignored.

## 4. Packaging

```bash
./scripts/bundle-linux.sh
```

In the sandbox this takes about 38 s on two cores and writes a 25 MB tarball. It ends by running `anyfile --check` from the staged tree and asserting that every Qt library resolves inside it.

The mac and Windows scripts cannot run here; they are anyeditqt's, adapted.

## 5. Things that bit, and stay fixed

- **`QCollator` numeric mode is ignored in the C locale.** That is what a process gets with `LANG` unset, so file10 sorted before file2 in the tests and nowhere else. The sort now uses `naturalCompare()` in `core/naming.cpp`, and a test pins the order.
- **`GROUPS` is a bash builtin.** An array assigned to it is silently discarded. The Linux bundle script uses `PLUGIN_GROUPS`.
- **`QFileSystemModel::index(path)` can emit `rowsInserted` from inside the call.** It creates the node for a path it has not loaded yet. `FilePane::retrySelect()` was iterating its pending set while calling it, re-entered through `rowsInserted`, modified the set underneath itself, and hit Qt's `!isUnused()` hash assertion. It now has a re-entrancy guard and iterates a copy. `pane_refresh_keeps_folder_selection_and_history` is the test that found it.
- **A test that pauses a job before it starts never reaches the copy loop.** The job checkpoints per top-level item, so it blocks before opening the part file. The cancel-mid-file test pauses from inside the progress callback after the first chunk instead, with `setProgressInterval(0)` so every chunk reports.
