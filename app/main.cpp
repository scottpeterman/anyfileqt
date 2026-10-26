// app/main.cpp
//
//   anyfile [folder]          open, with the left pane on folder if given
//   anyfile --check           print what a packaged build needs to find, exit
#ifdef _WIN32
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <cstdio>
#include <iostream>
#endif

#include <QApplication>
#include <QDir>
#include <QFileInfo>

#include "anyfile/volumes.h"
#include "app/filepane.h"
#include "app/mainwindow.h"
#include "app/settings.h"

namespace {

#ifdef _WIN32
// anyeditqt's console handling, unchanged -- see the long note there. The exe
// is /SUBSYSTEM:WINDOWS so a double-click does not drag a console window
// along, which leaves stdout bound to nothing. `anyfile --check` must still
// print, because bundle-windows.bat greps it. The standard handles are read
// BEFORE AttachConsole, which resets them: reopening CONOUT$ after attaching
// would send output to the console even when the caller redirected it to a
// file, and the smoke test would see its own output and still fail.
bool bindStandardStream(DWORD which, FILE *stream, const char *device, const char *mode) {
    const HANDLE h = GetStdHandle(which);
    if (h && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN) {
        const int flags = (mode[0] == 'r') ? (_O_RDONLY | _O_TEXT) : _O_TEXT;
        const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(h), flags);
        if (fd == -1) return false;
        if (_dup2(fd, _fileno(stream)) != 0) return false;
        setvbuf(stream, nullptr, _IONBF, 0);
        return true;
    }
    FILE *f = nullptr;
    return freopen_s(&f, device, mode, stream) == 0;
}

void attachParentConsole() {
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    const HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    const auto usable = [](HANDLE h) {
        return h && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN;
    };
    if (!usable(out) && !usable(err)) {
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
    }
    bindStandardStream(STD_OUTPUT_HANDLE, stdout, "CONOUT$", "w");
    bindStandardStream(STD_ERROR_HANDLE, stderr, "CONOUT$", "w");
    std::ios::sync_with_stdio(true);
}
#endif

}  // namespace

int main(int argc, char **argv) {
#ifdef _WIN32
    attachParentConsole();
#endif
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("anyfile"));
    QCoreApplication::setOrganizationName(QStringLiteral("anyfileqt"));
    QCoreApplication::setApplicationVersion(QStringLiteral(ANYFILE_VERSION));
    QApplication::setApplicationDisplayName(QStringLiteral("AnyFile"));

    // --check: no window, no event loop, nothing written to the config
    // directory. It proves a STAGED tree starts, loads its own Qt and its
    // platform plugin, and can enumerate volumes -- the parts packaging can
    // break. The bundle scripts grep for "volumes:".
    if (app.arguments().contains(QStringLiteral("--check"))) {
        const QList<anyfile::VolumeInfo> v = anyfile::scanVolumes();
        printf("anyfile: %s\nqt: %s\nplatform: %s\nsettings: %s\nvolumes: %d\n",
               ANYFILE_VERSION, qVersion(), qPrintable(QGuiApplication::platformName()),
               qPrintable(anyfile::Settings::configPath()), int(v.size()));
        for (const anyfile::VolumeInfo &vi : v)
            printf("    %s  %s  %s\n", qPrintable(vi.root), qPrintable(vi.fsType),
                   qPrintable(vi.name));
        return 0;
    }

    anyfile::Settings settings;
    anyfile::MainWindow w(&settings);

    // A folder on the command line opens in the left pane, so "Open with
    // AnyFile" on a folder and `anyfile .` both do the obvious thing.
    const QStringList args = app.arguments().mid(1);
    for (const QString &a : args) {
        if (a.startsWith(QLatin1Char('-'))) continue;
        const QFileInfo fi(a);
        const QString dir = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
        if (QFileInfo(dir).isDir()) {
            w.pane(0)->setPath(dir);
            if (!fi.isDir()) w.pane(0)->selectPaths({fi.absoluteFilePath()});
        }
        break;
    }

    w.show();
    return app.exec();
}
