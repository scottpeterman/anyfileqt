// anyfileqt/app/filetypes.cpp
#include "app/filetypes.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMimeDatabase>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#include <string>
#endif

namespace anyfile {

namespace {

// The common names, so the column reads like Explorer's on every platform
// rather than depending on what the MIME database happens to carry.
const QHash<QString, QString> &knownNames() {
    static const QHash<QString, QString> names = {
        {QStringLiteral("txt"), QStringLiteral("Text Document")},
        {QStringLiteral("log"), QStringLiteral("Text Document")},
        {QStringLiteral("md"), QStringLiteral("Markdown Document")},
        {QStringLiteral("pdf"), QStringLiteral("PDF Document")},
        {QStringLiteral("csv"), QStringLiteral("CSV File")},
        {QStringLiteral("json"), QStringLiteral("JSON File")},
        {QStringLiteral("yaml"), QStringLiteral("YAML File")},
        {QStringLiteral("yml"), QStringLiteral("YAML File")},
        {QStringLiteral("xml"), QStringLiteral("XML Document")},
        {QStringLiteral("html"), QStringLiteral("HTML Document")},
        {QStringLiteral("htm"), QStringLiteral("HTML Document")},
        {QStringLiteral("zip"), QStringLiteral("ZIP Archive")},
        {QStringLiteral("gz"), QStringLiteral("GZ Archive")},
        {QStringLiteral("tgz"), QStringLiteral("TGZ Archive")},
        {QStringLiteral("tar.gz"), QStringLiteral("TAR.GZ Archive")},
        {QStringLiteral("tar"), QStringLiteral("TAR Archive")},
        {QStringLiteral("7z"), QStringLiteral("7Z Archive")},
        {QStringLiteral("dmg"), QStringLiteral("Disk Image")},
        {QStringLiteral("iso"), QStringLiteral("Disk Image")},
        {QStringLiteral("sh"), QStringLiteral("Shell Script")},
        {QStringLiteral("py"), QStringLiteral("Python File")},
        {QStringLiteral("go"), QStringLiteral("Go Source File")},
        {QStringLiteral("c"), QStringLiteral("C Source File")},
        {QStringLiteral("h"), QStringLiteral("C/C++ Header")},
        {QStringLiteral("cpp"), QStringLiteral("C++ Source File")},
        {QStringLiteral("hpp"), QStringLiteral("C++ Header")},
        {QStringLiteral("js"), QStringLiteral("JavaScript File")},
        {QStringLiteral("ts"), QStringLiteral("TypeScript File")},
        {QStringLiteral("conf"), QStringLiteral("Configuration File")},
        {QStringLiteral("cfg"), QStringLiteral("Configuration File")},
        {QStringLiteral("ini"), QStringLiteral("Configuration Settings")},
        {QStringLiteral("pub"), QStringLiteral("Public Key")},
        {QStringLiteral("pem"), QStringLiteral("PEM Certificate")},
        {QStringLiteral("crt"), QStringLiteral("Certificate")},
        {QStringLiteral("pcap"), QStringLiteral("PCAP File")},
        {QStringLiteral("pcapng"), QStringLiteral("PCAPNG File")},
    };
    return names;
}

// A description is usable only if it is a description: Qt's built-in MIME
// database on macOS (and on Windows) ships without them, and comment() then
// returns the MIME name itself -- "text/plain" -- which is what reached the
// Type column on the Mac.
bool usableComment(const QString &comment, const QString &mimeName) {
    return !comment.isEmpty() && comment != mimeName && !comment.contains(QLatin1Char('/'));
}

#ifdef Q_OS_WIN
// Explorer's own wording, read from the same registry entries Explorer uses,
// so it follows whatever the machine has installed ("Text Document", or the
// name an editor registered for .txt). QFileIconProvider::type() does NOT do
// this in Qt 6: it goes to Qt's built-in MIME database, which on Windows has
// no descriptions, and the Type column showed "text/plain".
//
// SHGFI_USEFILEATTRIBUTES: the name alone decides, nothing is opened or
// stat'ed, so a slow network share costs nothing per row. Empty on failure,
// and the caller falls back to the table below rather than a MIME name.
QString shellTypeName(const QString &name, bool isDir) {
    SHFILEINFOW sfi{};
    const std::wstring w = name.toStdWString();
    const DWORD attrs = isDir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    if (!::SHGetFileInfoW(w.c_str(), attrs, &sfi, sizeof(sfi),
                          SHGFI_TYPENAME | SHGFI_USEFILEATTRIBUTES))
        return {};
    return QString::fromWCharArray(sfi.szTypeName).trimmed();
}
#endif

}  // namespace

QString friendlyType(const QFileInfo &fi) {
    if (fi.isDir()) {
#ifdef Q_OS_MACOS
        if (fi.isBundle())
            return fi.suffix().compare(QLatin1String("app"), Qt::CaseInsensitive) == 0
                       ? QStringLiteral("Application")
                       : QStringLiteral("Package");
#endif
        if (QDir(fi.absoluteFilePath()).isRoot()) return QStringLiteral("Drive");
#ifdef Q_OS_WIN
        static const QString folder = [] {
            const QString n = shellTypeName(QStringLiteral("folder"), true);
            return n.isEmpty() ? QStringLiteral("File folder") : n;
        }();
        return folder;
#else
        return QStringLiteral("File folder");
#endif
    }

    // Keyed on the COMPLETE suffix, so .tar.gz gets the tar glob rather than
    // the gzip one, and on the whole name when there is no suffix, so
    // Makefile and README are not cached as each other.
    static QHash<QString, QString> cache;
    const QString ext = fi.suffix().toLower();
    const QString key = fi.completeSuffix().isEmpty()
                            ? QLatin1Char('\x01') + fi.fileName().toLower()
                            : fi.completeSuffix().toLower();
    const auto hit = cache.constFind(key);
    if (hit != cache.constEnd()) return *hit;

#ifdef Q_OS_WIN
    // The shell keys on the LAST suffix, as Explorer does, so ask with a name
    // carrying only that: "x.gz" for a.tar.gz, the bare name when there is none.
    const QString shell = shellTypeName(
        ext.isEmpty() ? fi.fileName() : QStringLiteral("x.") + ext, false);
    if (!shell.isEmpty()) {
        cache.insert(key, shell);
        return shell;
    }
#endif

    // Curated names first, by complete suffix (tar.gz) then by last suffix.
    const QHash<QString, QString> &known = knownNames();
    QString name = known.value(fi.completeSuffix().toLower());
    if (name.isEmpty()) name = known.value(ext);

    if (name.isEmpty()) {
        static const QMimeDatabase db;
        const QMimeType mt = db.mimeTypeForFile(fi.fileName(), QMimeDatabase::MatchExtension);
        if (mt.isValid() && !mt.isDefault() && usableComment(mt.comment(), mt.name())) {
            name = mt.comment();
            name[0] = name.at(0).toUpper();
        } else if (ext.isEmpty()) {
            name = QStringLiteral("File");
        } else {
            // Explorer's wording for a type it has no name for: "PCAPZZ File".
            // The MIME family still says what kind of thing it is.
            const QString up = ext.toUpper();
            const QString family = mt.isValid() && !mt.isDefault() ? mt.name().section(QLatin1Char('/'), 0, 0)
                                                                  : QString();
            if (family == QLatin1String("image"))
                name = up + QStringLiteral(" Image");
            else if (family == QLatin1String("video"))
                name = up + QStringLiteral(" Video");
            else if (family == QLatin1String("audio"))
                name = up + QStringLiteral(" Audio");
            else
                name = up + QStringLiteral(" File");
        }
    }
    cache.insert(key, name);
    return name;
}

}  // namespace anyfile
