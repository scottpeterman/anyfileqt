#!/usr/bin/env bash
# scripts/bundle-linux.sh
#
# Packages anyfileqt as a relocatable directory, then a tarball.
#
#   ./scripts/bundle-linux.sh                         # -> dist/anyfile-<ver>-linux-<arch>.tar.gz
#   ./scripts/bundle-linux.sh --qt ~/Qt/6.10.3/gcc_64
#   ./scripts/bundle-linux.sh --no-tar                # leave the tree, skip the archive
#
# anyeditqt's bundle-linux.sh with the grammar corpus taken out; every guard in
# it is here for the same reason it is there.
#
# WHICH Qt, echoed before the build. A machine with a distribution Qt and an
# installed one lets find_package reach whichever comes first, and this script
# must deploy the libraries THAT Qt resolves to. Resolution order:
#
#   --qt <prefix>, $CMAKE_PREFIX_PATH, $Qt6_DIR, the newest ~/Qt/6.*/gcc_64,
#   then whatever find_package finds on its own.
#
# Layout:
#
#   anyfile/
#     bin/anyfile         launcher; bin/anyfile.bin is the binary
#     lib/                Qt and ICU
#     plugins/            platform plugin and friends
#     Resources/          LICENSE, THIRD_PARTY_NOTICES.md, licenses/
#     install.sh          per-user install: ~/.local/bin and a .desktop entry
#
# A LAUNCHER, NOT AN RPATH: a link-time rpath has to be right for a layout that
# does not exist at configure time, and a wrong one silently loads the HOST's
# Qt -- fine on the build machine, broken everywhere else.

set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT}"

MAKE_TAR=1
BUILD_DIR="build-linux"
QTPREFIX="${CMAKE_PREFIX_PATH:-}"
TARGET="anyfile"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --qt) QTPREFIX="$2"; shift 2 ;;
        --qt=*) QTPREFIX="${1#*=}"; shift ;;
        --no-tar) MAKE_TAR=0; shift ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        -h|--help) sed -n '3,8p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

die() { echo "error: $*" >&2; exit 1; }
say() { echo "==> $*"; }

# --- which Qt ---------------------------------------------------------------

if [[ -z "${QTPREFIX}" && -n "${Qt6_DIR:-}" ]]; then
    QTPREFIX="$(cd "${Qt6_DIR}/../../.." && pwd)"
fi
# sort -V, not sort: text order puts 6.9.1 above 6.10.3.
if [[ -z "${QTPREFIX}" ]]; then
    for base in "${HOME}/Qt" /opt/Qt; do
        [[ -d "${base}" ]] || continue
        for ver in $(ls -1 "${base}" 2>/dev/null | grep -E '^6\.[0-9]+' | sort -Vr); do
            for abi in gcc_64 gcc_arm64; do
                if [[ -d "${base}/${ver}/${abi}/lib/cmake/Qt6" ]]; then
                    QTPREFIX="${base}/${ver}/${abi}"
                    break 3
                fi
            done
        done
    done
    [[ -n "${QTPREFIX}" ]] && say "found Qt at ${QTPREFIX} (pass --qt to choose a different one)"
fi
if [[ -n "${QTPREFIX}" ]]; then
    [[ -d "${QTPREFIX}/lib/cmake/Qt6" ]] \
        || die "${QTPREFIX} has no lib/cmake/Qt6 in it. The prefix is the directory
     holding bin/, lib/ and include/ -- e.g. ~/Qt/6.10.3/gcc_64."
fi

VERSION="$(sed -n 's/^project(anyfileqt VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
[[ -n "${VERSION}" ]] || die "could not read the version out of CMakeLists.txt"
ARCH="$(uname -m)"
STAGE="dist/anyfile"

# --- build -----------------------------------------------------------------

# A CMake cache records the absolute path it was created at and cannot be
# relocated; one that travelled with the tree fails every cmake call naming
# somebody else's home. Wipe it rather than diagnose it.
if [[ -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    CACHED_SRC="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "${BUILD_DIR}/CMakeCache.txt")"
    HERE_SRC="$(cd "${ROOT}" && pwd -P)"
    if [[ -n "${CACHED_SRC}" && "${CACHED_SRC}" != "${ROOT}" && "${CACHED_SRC}" != "${HERE_SRC}" ]]; then
        say "${BUILD_DIR} was configured under ${CACHED_SRC}; a CMake cache is not relocatable, wiping it"
        rm -rf "${BUILD_DIR}"
    fi
fi
# A cached Qt6_DIR beats -DCMAKE_PREFIX_PATH and says nothing about it.
if [[ -n "${QTPREFIX}" && -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    CACHED="$(sed -n 's/^Qt6_DIR:PATH=//p' "${BUILD_DIR}/CMakeCache.txt")"
    if [[ -n "${CACHED}" && "${CACHED}" != "${QTPREFIX}"/* ]]; then
        say "${BUILD_DIR} was configured against ${CACHED}; wiping it"
        rm -rf "${BUILD_DIR}"
    fi
fi

say "building ${VERSION} into ${BUILD_DIR}"
# Every option passed explicitly: option() honours an existing cache entry, so
# a default changed in CMakeLists.txt never reaches an existing build directory.
CMAKE_ARGS=(-DCMAKE_BUILD_TYPE=Release -DANYFILE_BUILD_APP=ON -DANYFILE_BUILD_TESTS=OFF)
[[ -n "${QTPREFIX}" ]] && CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=${QTPREFIX}")
cmake -S . -B "${BUILD_DIR}" "${CMAKE_ARGS[@]}" | grep -E '^-- (Qt [0-9]|  (prefix|QtCore):)' || {
    cmake -S . -B "${BUILD_DIR}" "${CMAKE_ARGS[@]}"
    die "configure failed"
}
cmake --build "${BUILD_DIR}" --target "${TARGET}" -j"$(nproc)"

BIN="${BUILD_DIR}/app/${TARGET}"
[[ -x "${BIN}" ]] || die "no ${BIN} after the build"

# --- stage -----------------------------------------------------------------

say "staging into ${STAGE}"
rm -rf "${STAGE}"
mkdir -p "${STAGE}/bin" "${STAGE}/lib" "${STAGE}/plugins" "${STAGE}/Resources"
cp "${BIN}" "${STAGE}/bin/${TARGET}.bin"

# Licence texts are a distribution obligation: GPLv3 s4 for the application,
# LGPLv3 s4d for Qt.
cp LICENSE "${STAGE}/Resources/LICENSE"
cp THIRD_PARTY_NOTICES.md "${STAGE}/Resources/THIRD_PARTY_NOTICES.md"
mkdir -p "${STAGE}/Resources/licenses"
cp -R licenses/. "${STAGE}/Resources/licenses/"

say "copying libraries"
ldd "${BIN}" | awk '/=> \//{print $3}' | while read -r lib; do
    case "$(basename "${lib}")" in
        libQt6*|libicu*) cp -Lu "${lib}" "${STAGE}/lib/" ;;
    esac
done

# Plugins from the Qt the binary ACTUALLY resolves to, not qmake on PATH.
QT_CORE_SO="$(ldd "${BIN}" | awk '/libQt6Core/{print $3}')"
[[ -n "${QT_CORE_SO}" ]] || die "the binary does not link libQt6Core"
QT_LIBDIR="$(dirname "${QT_CORE_SO}")"
say "linked against ${QT_CORE_SO}"
QT_PLUGIN_SRC=""
for cand in "${QT_LIBDIR}/qt6/plugins" "${QT_LIBDIR}/../plugins" "${QT_LIBDIR}/qt/plugins"; do
    if [[ -d "${cand}/platforms" ]]; then QT_PLUGIN_SRC="$(cd "${cand}" && pwd)"; break; fi
done
[[ -n "${QT_PLUGIN_SRC}" ]] || die "could not find the plugin directory for ${QT_CORE_SO}"
if [[ -n "${QTPREFIX}" && "${QT_PLUGIN_SRC}" != "$(cd "${QTPREFIX}" && pwd)"/* ]]; then
    die "asked for Qt at ${QTPREFIX} but the binary links ${QT_CORE_SO}; wipe ${BUILD_DIR} and retry"
fi
say "plugins from ${QT_PLUGIN_SRC}"

# PLUGIN_GROUPS, not GROUPS: bash owns $GROUPS and silently drops assignments
# to it, which leaves the loop below copying nothing.
#
# iconengines: without the SVG icon engine a themed icon set renders as blank
# squares. platformthemes: native file dialogs and the desktop's icon theme.
PLUGIN_GROUPS=(platforms platformthemes imageformats iconengines tls xcbglintegrations wayland-shell-integration wayland-decoration-client wayland-graphics-integration-client)
for group in "${PLUGIN_GROUPS[@]}"; do
    [[ -d "${QT_PLUGIN_SRC}/${group}" ]] || continue
    mkdir -p "${STAGE}/plugins/${group}"
    cp -Lu "${QT_PLUGIN_SRC}/${group}"/*.so "${STAGE}/plugins/${group}/" 2>/dev/null || true
done
[[ -f "${STAGE}/plugins/platforms/libqxcb.so" ]] || die "libqxcb.so did not get copied; the bundle would not start"
[[ -f "${STAGE}/plugins/platforms/libqoffscreen.so" ]] || die "libqoffscreen.so did not get copied; the verify step cannot run"

# The plugins' own Qt dependencies, resolved against the ORIGINAL plugins: an
# installer Qt's plugins carry RUNPATH $ORIGIN/../../lib, which from the stage
# resolves to the stage itself and asks cp to copy a file onto itself.
for group in "${PLUGIN_GROUPS[@]}"; do
    [[ -d "${QT_PLUGIN_SRC}/${group}" ]] || continue
    for plugin in "${QT_PLUGIN_SRC}/${group}"/*.so; do
        [[ -f "${plugin}" ]] || continue
        ldd "${plugin}" 2>/dev/null | awk '/=> \//{print $3}' | while read -r lib; do
            case "$(basename "${lib}")" in libQt6*|libicu*) ;; *) continue ;; esac
            [[ "$(readlink -f "${lib}")" == "$(readlink -f "${STAGE}")"/* ]] && continue
            cp -Lu "${lib}" "${STAGE}/lib/"
        done
    done
done

# --- icon and desktop entry ----------------------------------------------------

if compgen -G "assets/anyfile-*.png" >/dev/null; then
    for png in assets/anyfile-*.png; do
        sz="$(basename "${png}" .png)"; sz="${sz#anyfile-}"
        mkdir -p "${STAGE}/share/icons/hicolor/${sz}x${sz}/apps"
        cp "${png}" "${STAGE}/share/icons/hicolor/${sz}x${sz}/apps/anyfile.png"
    done
fi
mkdir -p "${STAGE}/share/applications"
cat > "${STAGE}/share/applications/anyfile.desktop" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=AnyFile
GenericName=File Manager
Comment=A Windows-styled dual-pane file manager
Exec=anyfile %U
Icon=anyfile
Terminal=false
Categories=System;FileTools;FileManager;Utility;
MimeType=inode/directory;
Keywords=files;folders;explorer;manager;
StartupWMClass=anyfile
DESKTOP

# --- launcher and installer ------------------------------------------------------

cat > "${STAGE}/bin/${TARGET}" <<LAUNCH
#!/usr/bin/env bash
# Resolves through symlinks, so this still works from a link in ~/.local/bin.
HERE="\$(cd "\$(dirname "\$(readlink -f "\${BASH_SOURCE[0]}")")" && pwd)"
export LD_LIBRARY_PATH="\${HERE}/../lib\${LD_LIBRARY_PATH:+:\${LD_LIBRARY_PATH}}"
export QT_PLUGIN_PATH="\${HERE}/../plugins"
exec "\${HERE}/${TARGET}.bin" "\$@"
LAUNCH
chmod +x "${STAGE}/bin/${TARGET}"

# Per-user, never system-wide. The .desktop entry gets an absolute Exec at
# install time because the tarball can be unpacked anywhere. It does NOT
# make AnyFile the default for folders; xdg-mime does that if wanted.
cat > "${STAGE}/install.sh" <<'INSTALL'
#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd)"
APPS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
ICONS="${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor"
mkdir -p "${APPS}" "${HOME}/.local/bin"
ln -sf "${HERE}/bin/anyfile" "${HOME}/.local/bin/anyfile"
if [[ -d "${HERE}/share/icons/hicolor" ]]; then
    (cd "${HERE}/share/icons/hicolor" && find . -name anyfile.png) | while read -r rel; do
        mkdir -p "${ICONS}/$(dirname "${rel}")"
        cp "${HERE}/share/icons/hicolor/${rel}" "${ICONS}/${rel}"
    done
fi
sed "s|^Exec=anyfile |Exec=${HERE}/bin/anyfile |" "${HERE}/share/applications/anyfile.desktop" > "${APPS}/anyfile.desktop"
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "${APPS}" || true
echo "installed ${HOME}/.local/bin/anyfile and ${APPS}/anyfile.desktop"
echo "to make it the default for folders:  xdg-mime default anyfile.desktop inode/directory"
INSTALL
chmod +x "${STAGE}/install.sh"

# --- verify ----------------------------------------------------------------

say "verifying"
for f in LICENSE THIRD_PARTY_NOTICES.md licenses/GPL-3.0.txt licenses/LGPL-3.0.txt; do
    [[ -s "${STAGE}/Resources/${f}" ]] || die "Resources/${f} missing; it must ship with the binary"
done
LIBS="$(ls "${STAGE}/lib" | wc -l)"
[[ "${LIBS}" -ge 3 ]] || die "only ${LIBS} libraries staged; expected Qt Core, Gui and Widgets at least"
echo "    ${LIBS} libraries"

say "smoke test: anyfile --check from the staged tree"
CHECK="$(QT_QPA_PLATFORM=offscreen timeout 30 "${STAGE}/bin/${TARGET}" --check 2>&1 || true)"
grep -vE "OpenType support missing|XDG_RUNTIME_DIR" <<<"${CHECK}" | sed 's/^/    /'
grep -qi "could not load the Qt platform plugin" <<<"${CHECK}" \
    && die "the staged tree could not load its platform plugin"
grep -q "^volumes: [1-9]" <<<"${CHECK}" \
    || die "the staged tree started but did not report any volumes; see above"

# It ran; now prove it ran on the SHIPPED Qt and not the host's.
HOSTQT="$(LD_LIBRARY_PATH="${PWD}/${STAGE}/lib" ldd "${STAGE}/bin/${TARGET}.bin" \
          | awk '/libQt6.*=> \//{print $3}' | grep -cv "^${PWD}/${STAGE}/lib" || true)"
[[ "${HOSTQT}" -eq 0 ]] || die "${HOSTQT} Qt libraries still resolve outside the bundle"
say "the staged tree starts and every Qt library resolves inside it"

# --- archive ---------------------------------------------------------------

if [[ "${MAKE_TAR}" -eq 1 ]]; then
    TARBALL="dist/anyfile-${VERSION}-linux-${ARCH}.tar.gz"
    say "writing ${TARBALL}"
    tar -czf "${TARBALL}" -C dist anyfile
    ls -lh "${TARBALL}"
    # Bare filename inside, so sha256sum -c works wherever it is downloaded to.
    ( cd dist && sha256sum "$(basename "${TARBALL}")" > "$(basename "${TARBALL}").sha256" )
    cat "${TARBALL}.sha256"
fi

echo
echo "run it with:"
echo "      ${STAGE}/bin/${TARGET}"
