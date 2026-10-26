# Third-party notices

AnyFile is licensed under the GNU General Public License v3.0 (see `LICENSE`).

It links one third-party library.

## Qt 6

- Components: Qt Core, Qt Gui, Qt Widgets, and the platform plugins deployed with them
- Copyright: The Qt Company Ltd. and other contributors
- License: GNU Lesser General Public License v3.0 (`licenses/LGPL-3.0.txt`), which
  incorporates the GNU General Public License v3.0 (`licenses/GPL-3.0.txt`)
- Source: https://download.qt.io/official_releases/qt/ and https://code.qt.io/

Qt is linked dynamically. The Qt libraries shipped beside the application are
unmodified, and you may replace them with your own build of the same Qt version.
The Qt source matching each release is available from the URLs above.

On Linux packages, the ICU libraries are copied along with Qt
(Unicode License V3, https://www.unicode.org/license.txt).
