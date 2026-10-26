#!/usr/bin/env bash
# anyfileqt/scripts/build.sh
#
# Day-to-day build: configure, build, optionally test, then (re)launch.
# macOS and Linux. For a distributable package use bundle-mac.sh or
# bundle-linux.sh instead.
#
#   ./scripts/build.sh                 configure if needed, build, quit any running AnyFile, launch
#   ./scripts/build.sh --test          also run the test suites before launching
#   ./scripts/build.sh --clean         wipe build/ first
#   ./scripts/build.sh --no-run        build only
#   ./scripts/build.sh --release       Release instead of Debug
#   ./scripts/build.sh --qt ~/Qt/6.10.3/macos
#
# Written for the bash 3.2 that macOS ships: no associative arrays, no
# ${var,,}, no mapfile.
#
# WHY IT EXISTS. A failed build leaves build/app/anyfile.app with an
# Info.plist and an EMPTY Contents/MacOS -- CMake makes the bundle skeleton at
# configure time -- so a build whose error scrolled past looks almost like a
# finished one. This script stops at the first failure, prints the compiler
# errors rather than the last screen of make output, and checks that the
# binary actually exists before it says so.

set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT}"

BUILD_DIR="build"
BUILD_TYPE="Debug"
QTPREFIX="${CMAKE_PREFIX_PATH:-}"
CLEAN=0
TEST=0
RUN=1

while [[ $# -gt 0 ]]; do
    case "$1" in
        --qt) QTPREFIX="$2"; shift 2 ;;
        --qt=*) QTPREFIX="${1#*=}"; shift ;;
        --clean) CLEAN=1; shift ;;
        --test) TEST=1; shift ;;
        --no-run) RUN=0; shift ;;
        --release) BUILD_TYPE="Release"; shift ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        -h|--help) sed -n '3,14p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1 (try --help)" >&2; exit 2 ;;
    esac
done

die() { echo "error: $*" >&2; exit 1; }
say() { echo "==> $*"; }

OS="$(uname -s)"
if [[ "${OS}" == "Darwin" ]]; then
    NCPU="$(sysctl -n hw.ncpu)"
    QTABI="macos"
else
    NCPU="$(nproc)"
    QTABI="gcc_64"
fi

# --- which Qt ---------------------------------------------------------------
# --qt, then $CMAKE_PREFIX_PATH, then the newest ~/Qt/6.*/<abi>. sort -V, not
# sort: text order puts 6.9.1 above 6.10.3.
if [[ -z "${QTPREFIX}" ]]; then
    for ver in $(ls -1 "${HOME}/Qt" 2>/dev/null | grep -E '^6\.[0-9]+' | sort -Vr); do
        if [[ -d "${HOME}/Qt/${ver}/${QTABI}/lib/cmake/Qt6" ]]; then
            QTPREFIX="${HOME}/Qt/${ver}/${QTABI}"
            break
        fi
    done
fi
if [[ -n "${QTPREFIX}" ]]; then
    [[ -d "${QTPREFIX}/lib/cmake/Qt6" ]] || die "${QTPREFIX} has no lib/cmake/Qt6 in it.
     The prefix is the folder holding bin/, lib/ and include/, e.g. ~/Qt/6.10.3/macos."
    say "Qt ${QTPREFIX}"
else
    say "no ~/Qt install found; letting CMake find Qt on its own"
fi

# --- build directory ------------------------------------------------------------
if [[ "${CLEAN}" -eq 1 && -d "${BUILD_DIR}" ]]; then
    say "wiping ${BUILD_DIR}"
    rm -rf "${BUILD_DIR}"
fi
# A cache made under another path (the tree was moved or re-unzipped
# somewhere else) or against another Qt cannot be reused; CMake would either
# refuse or quietly keep the old Qt.
if [[ -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    CACHED_SRC="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "${BUILD_DIR}/CMakeCache.txt")"
    CACHED_QT="$(sed -n 's/^Qt6_DIR:PATH=//p' "${BUILD_DIR}/CMakeCache.txt")"
    if [[ -n "${CACHED_SRC}" && "${CACHED_SRC}" != "${ROOT}" && "${CACHED_SRC}" != "$(pwd -P)" ]]; then
        say "${BUILD_DIR} was configured for ${CACHED_SRC}; wiping it"
        rm -rf "${BUILD_DIR}"
    elif [[ -n "${QTPREFIX}" && -n "${CACHED_QT}" && "${CACHED_QT}" != "${QTPREFIX}"/* ]]; then
        say "${BUILD_DIR} was configured against ${CACHED_QT}; wiping it"
        rm -rf "${BUILD_DIR}"
    fi
fi

mkdir -p "${BUILD_DIR}"
LOG="${BUILD_DIR}/build.log"

# --- configure --------------------------------------------------------------------
CMAKE_ARGS=(-S . -B "${BUILD_DIR}" "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}" -DANYFILE_BUILD_APP=ON -DANYFILE_BUILD_TESTS=ON)
[[ -n "${QTPREFIX}" ]] && CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=${QTPREFIX}")
say "configuring (${BUILD_TYPE})"
if ! cmake "${CMAKE_ARGS[@]}" >"${LOG}" 2>&1; then
    grep -E "CMake Error|error:" -A6 "${LOG}" | head -60 >&2 || tail -40 "${LOG}" >&2
    die "configure failed; the full output is in ${LOG}"
fi
grep -E "^-- (macOS|Qt [0-9])" "${LOG}" | sed 's/^-- /    /' || true

# --- build ------------------------------------------------------------------------
say "building with ${NCPU} jobs"
START=$(date +%s)
if ! cmake --build "${BUILD_DIR}" -j "${NCPU}" >>"${LOG}" 2>&1; then
    echo >&2
    # The first errors are the real ones; later ones are usually fallout.
    if grep -qE "error:" "${LOG}"; then
        grep -E "error:" -A4 "${LOG}" | head -60 >&2
    else
        tail -40 "${LOG}" >&2
    fi
    die "build failed after $(( $(date +%s) - START ))s; the full output is in ${LOG}"
fi
WARN="$(grep -c "warning:" "${LOG}" || true)"
if [[ "${WARN:-0}" -gt 0 ]]; then
    say "built in $(( $(date +%s) - START ))s, ${WARN} warnings (see ${LOG})"
else
    say "built in $(( $(date +%s) - START ))s"
fi

if [[ "${OS}" == "Darwin" ]]; then
    APP="${BUILD_DIR}/app/anyfile.app"
    BIN="${APP}/Contents/MacOS/anyfile"
else
    APP=""
    BIN="${BUILD_DIR}/app/anyfile"
fi
[[ -x "${BIN}" ]] || die "the build reported success but ${BIN} is not there; see ${LOG}"

VERSION="$(QT_QPA_PLATFORM=offscreen "${BIN}" --check 2>/dev/null | sed -n 's/^anyfile: //p' || true)"
say "anyfile ${VERSION:-?} at ${BIN}"

# --- test ---------------------------------------------------------------------------
if [[ "${TEST}" -eq 1 ]]; then
    say "testing"
    ctest --test-dir "${BUILD_DIR}" --output-on-failure || die "tests failed"
fi

# --- run ------------------------------------------------------------------------------
[[ "${RUN}" -eq 1 ]] || exit 0

# Quit the copy that is already running. Otherwise `open` just brings the OLD
# build to the front, which looks exactly like "my change did nothing".
if [[ "${OS}" == "Darwin" ]]; then
    if pgrep -x anyfile >/dev/null 2>&1; then
        say "quitting the running AnyFile"
        osascript -e 'tell application id "org.anyfileqt.anyfile" to quit' >/dev/null 2>&1 || true
        for _ in 1 2 3 4 5 6 7 8 9 10; do
            pgrep -x anyfile >/dev/null 2>&1 || break
            sleep 0.3
        done
        pgrep -x anyfile >/dev/null 2>&1 && pkill -x anyfile || true
    fi
    say "launching"
    # -n: a new instance from THIS bundle path, never a different registered copy.
    open -n "${APP}"
else
    pkill -x anyfile 2>/dev/null || true
    say "launching"
    nohup "${BIN}" >/dev/null 2>&1 &
fi
