// core/src/naming.cpp
#include "anyfile/naming.h"

#include <QDir>
#include <QFileInfo>

namespace anyfile {

NameParts splitName(const QString &fileName, bool isDir) {
    // Folders have no extension in Explorer's eyes: "v1.2 - Copy", not
    // "v1 - Copy.2".
    if (isDir) return {fileName, {}};
    const int dot = fileName.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0) return {fileName, {}};  // no dot, or .bashrc
    return {fileName.left(dot), fileName.mid(dot)};
}

bool pathExists(const QString &path) {
    const QFileInfo fi(path);
    return fi.exists() || fi.isSymLink();
}

static QString join(const QString &dir, const QString &name) {
    return QDir(dir).filePath(name);
}

QString copyName(const QString &dir, const QString &fileName, bool isDir) {
    const NameParts p = splitName(fileName, isDir);
    QString candidate = p.base + QStringLiteral(" - Copy") + p.ext;
    for (int n = 2; pathExists(join(dir, candidate)); ++n)
        candidate = p.base + QStringLiteral(" - Copy (%1)").arg(n) + p.ext;
    return candidate;
}

QString keepBothName(const QString &dir, const QString &fileName, bool isDir) {
    const NameParts p = splitName(fileName, isDir);
    QString candidate;
    int n = 2;
    do {
        candidate = p.base + QStringLiteral(" (%1)").arg(n++) + p.ext;
    } while (pathExists(join(dir, candidate)));
    return candidate;
}

QString freeName(const QString &dir, const QString &fileName, bool isDir) {
    if (!pathExists(join(dir, fileName))) return fileName;
    return keepBothName(dir, fileName, isDir);
}

int naturalCompare(const QString &a, const QString &b) {
    const qsizetype na = a.size(), nb = b.size();
    qsizetype i = 0, j = 0;
    while (i < na && j < nb) {
        const bool da = a.at(i).isDigit(), db = b.at(j).isDigit();
        if (da && db) {
            // Compare digit runs by value without converting: skip leading
            // zeros, then the longer run is the bigger number, then digit by
            // digit. No overflow on a 40-digit serial number.
            qsizetype si = i, sj = j;
            while (si < na && a.at(si) == QLatin1Char('0')) ++si;
            while (sj < nb && b.at(sj) == QLatin1Char('0')) ++sj;
            qsizetype ei = si, ej = sj;
            while (ei < na && a.at(ei).isDigit()) ++ei;
            while (ej < nb && b.at(ej).isDigit()) ++ej;
            const qsizetype la = ei - si, lb = ej - sj;
            if (la != lb) return la < lb ? -1 : 1;
            for (qsizetype k = 0; k < la; ++k) {
                const int d = a.at(si + k).unicode() - b.at(sj + k).unicode();
                if (d != 0) return d;
            }
            // Equal value: fewer leading zeros first (1 before 01).
            const qsizetype za = si - i, zb = sj - j;
            if (za != zb) return za < zb ? -1 : 1;
            i = ei;
            j = ej;
            continue;
        }
        // A non-digit run on each side, compared as a whole so the locale
        // collation sees words rather than single characters.
        qsizetype ei = i, ej = j;
        while (ei < na && !a.at(ei).isDigit()) ++ei;
        while (ej < nb && !b.at(ej).isDigit()) ++ej;
        if (ei == i || ej == j) {
            // Digit on one side, text on the other: digits sort first, as in
            // Explorer ("1abc" before "abc").
            return ei == i ? -1 : 1;
        }
        const QString ca = a.mid(i, ei - i).toCaseFolded();
        const QString cb = b.mid(j, ej - j).toCaseFolded();
        int c = QString::localeAwareCompare(ca, cb);
        if (c == 0) c = ca.compare(cb);
        if (c != 0) return c;
        i = ei;
        j = ej;
    }
    if (i < na) return 1;
    if (j < nb) return -1;
    // Equal ignoring case: a stable, case-sensitive tiebreak so the order
    // never depends on the order the files were listed in.
    return a.compare(b);
}

QString trashDisplayName() {
#ifdef Q_OS_WIN
    return QStringLiteral("Recycle Bin");
#else
    return QStringLiteral("Trash");
#endif
}

}  // namespace anyfile
