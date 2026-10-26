// app/dropaction.cpp
#include "app/dropaction.h"

#include <QDir>
#include <QFileInfo>

namespace anyfile {

namespace {
constexpr Qt::CaseSensitivity kPathCase =
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    Qt::CaseInsensitive;
#else
    Qt::CaseSensitive;
#endif

QString clean(const QString &p) { return QDir::cleanPath(QDir::fromNativeSeparators(p)); }
}  // namespace

Qt::DropAction chooseDropAction(const QStringList &sources, const QString &destDir,
                                Qt::KeyboardModifiers mods, const QList<VolumeInfo> &volumes) {
#ifdef Q_OS_MACOS
    if (mods & Qt::AltModifier) return Qt::CopyAction;
    if (mods & Qt::ControlModifier) return Qt::MoveAction;
#else
    if (mods & Qt::ControlModifier) return Qt::CopyAction;
    if (mods & Qt::ShiftModifier) return Qt::MoveAction;
#endif
    const VolumeInfo *dv = volumeFor(volumes, destDir);
    if (!dv) return Qt::CopyAction;
    for (const QString &s : sources) {
        const VolumeInfo *sv = volumeFor(volumes, s);
        if (!sv || sv->root.compare(dv->root, kPathCase) != 0) return Qt::CopyAction;
    }
    return Qt::MoveAction;
}

bool isPointlessDrop(const QStringList &sources, const QString &destDir) {
    const QString d = clean(destDir);
    bool allAlreadyThere = true;
    for (const QString &raw : sources) {
        const QString s = clean(raw);
        if (d.compare(s, kPathCase) == 0 || d.startsWith(s + QLatin1Char('/'), kPathCase))
            return true;
        // path(), not absolutePath(): lexical, so it neither touches the file
        // system nor depends on the cwd. absolutePath() turned the rootless
        // "/a/x.txt" into "C:/a" on Windows and the comparison missed.
        if (clean(QFileInfo(s).path()).compare(d, kPathCase) != 0)
            allAlreadyThere = false;
    }
    return allAlreadyThere;
}

}  // namespace anyfile
