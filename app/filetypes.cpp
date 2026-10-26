// anyfileqt/app/filetypes.cpp
#include "app/filetypes.h"

#include <QDir>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QHash>
#include <QMimeDatabase>

namespace anyfile {

#ifndef Q_OS_WIN
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

}  // namespace
#endif

QString friendlyType(const QFileInfo &fi) {
#ifdef Q_OS_WIN
    static QFileIconProvider provider;
    return provider.type(fi);
#else
    if (fi.isDir()) {
#ifdef Q_OS_MACOS
        if (fi.isBundle())
            return fi.suffix().compare(QLatin1String("app"), Qt::CaseInsensitive) == 0
                       ? QStringLiteral("Application")
                       : QStringLiteral("Package");
#endif
        if (QDir(fi.absoluteFilePath()).isRoot()) return QStringLiteral("Drive");
        return QStringLiteral("File folder");
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
#endif
}

}  // namespace anyfile
