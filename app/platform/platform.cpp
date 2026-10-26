// app/platform/platform.cpp
//
// NOT VERIFIED ON WINDOWS OR macOS. The Linux branch is exercised in the
// sandbox; the other two are written against documented behaviour of
// explorer.exe, wt.exe and open(1). Each says so in the UI by failing visibly
// (the caller reports a false return) rather than silently.
#include "app/platform/platform.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

namespace anyfile::platform {

QString fileManagerName() {
#if defined(Q_OS_WIN)
    return QStringLiteral("Explorer");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("Finder");
#else
    return QStringLiteral("Files");
#endif
}

bool revealInFileManager(const QString &path) {
#if defined(Q_OS_WIN)
    // explorer.exe parses its own command line, and wants /select,"<path>"
    // as one token with the quotes inside it. QProcess's argument quoting
    // would wrap the whole thing in quotes instead, which Explorer ignores
    // and opens Documents. setNativeArguments passes it through untouched.
    QProcess p;
    p.setProgram(QStringLiteral("explorer.exe"));
    p.setNativeArguments(QStringLiteral("/select,\"%1\"").arg(QDir::toNativeSeparators(path)));
    return p.startDetached();
#elif defined(Q_OS_MACOS)
    return QProcess::startDetached(QStringLiteral("/usr/bin/open"), {QStringLiteral("-R"), path});
#else
    // org.freedesktop.FileManager1.ShowItems selects the item in Nautilus,
    // Dolphin, Nemo, Thunar and Caja. Without a session bus or dbus-send,
    // open the containing folder instead.
    const QString dbus = QStandardPaths::findExecutable(QStringLiteral("dbus-send"));
    if (!dbus.isEmpty()) {
        const QString url = QString::fromUtf8(QUrl::fromLocalFile(path).toEncoded());
        if (QProcess::startDetached(
                dbus, {QStringLiteral("--session"),
                       QStringLiteral("--dest=org.freedesktop.FileManager1"),
                       QStringLiteral("--type=method_call"),
                       QStringLiteral("/org/freedesktop/FileManager1"),
                       QStringLiteral("org.freedesktop.FileManager1.ShowItems"),
                       QStringLiteral("array:string:%1").arg(url), QStringLiteral("string:")}))
            return true;
    }
    return QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
}

bool openTerminalAt(const QString &dir) {
#if defined(Q_OS_WIN)
    const QString native = QDir::toNativeSeparators(dir);
    // Windows Terminal when it is installed (wt.exe is an app execution
    // alias), the console host otherwise.
    if (QProcess::startDetached(QStringLiteral("wt.exe"), {QStringLiteral("-d"), native}))
        return true;
    return QProcess::startDetached(QStringLiteral("cmd.exe"), {}, native);
#elif defined(Q_OS_MACOS)
    return QProcess::startDetached(QStringLiteral("/usr/bin/open"),
                                   {QStringLiteral("-a"), QStringLiteral("Terminal"), dir});
#else
    // $TERMINAL first, then the Debian alternative, then the common ones.
    // Every one of these starts in its own working directory, which
    // startDetached sets.
    QStringList candidates;
    const QString env = qEnvironmentVariable("TERMINAL");
    if (!env.isEmpty()) candidates << env;
    candidates << QStringLiteral("x-terminal-emulator") << QStringLiteral("gnome-terminal")
               << QStringLiteral("konsole") << QStringLiteral("xfce4-terminal")
               << QStringLiteral("kitty") << QStringLiteral("alacritty")
               << QStringLiteral("xterm");
    for (const QString &c : candidates) {
        const QString exe = QStandardPaths::findExecutable(c);
        if (!exe.isEmpty() && QProcess::startDetached(exe, {}, dir)) return true;
    }
    return false;
#endif
}

}  // namespace anyfile::platform
