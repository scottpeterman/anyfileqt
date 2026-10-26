// core/src/clipboard.cpp
#include "anyfile/clipboard.h"

#include <QDir>
#include <QMimeData>
#include <QUrl>
#include <QtEndian>

namespace anyfile {

namespace {

const QString kOurCut = QStringLiteral("application/x-anyfileqt-cut");
const QString kKdeCut = QStringLiteral("application/x-kde-cutselection");
const QString kGnome = QStringLiteral("x-special/gnome-copied-files");
const QString kWinEffect =
    QStringLiteral("application/x-qt-windows-mime;value=\"Preferred DropEffect\"");

// DROPEFFECT_COPY | DROPEFFECT_LINK is what Explorer itself writes for a copy;
// DROPEFFECT_MOVE alone is a cut.
constexpr quint32 kDropEffectCopy = 5;
constexpr quint32 kDropEffectMove = 2;

}  // namespace

QMimeData *makeClipboardData(const QStringList &paths, bool cut) {
    auto *md = new QMimeData;
    QList<QUrl> urls;
    QStringList native;
    QByteArray gnome = cut ? "cut" : "copy";
    for (const QString &p : paths) {
        const QUrl u = QUrl::fromLocalFile(p);
        urls.append(u);
        native.append(QDir::toNativeSeparators(p));
        gnome += '\n' + u.toEncoded();
    }
    md->setUrls(urls);
    md->setText(native.join(QLatin1Char('\n')));
    md->setData(kGnome, gnome);
    md->setData(kKdeCut, cut ? "1" : "0");
    md->setData(kOurCut, cut ? "1" : "0");

    QByteArray effect(4, '\0');
    qToLittleEndian<quint32>(cut ? kDropEffectMove : kDropEffectCopy, effect.data());
    md->setData(kWinEffect, effect);
    return md;
}

ClipboardFiles readClipboardData(const QMimeData *data) {
    ClipboardFiles out;
    if (!data) return out;

    QList<QUrl> urls = data->urls();
    QByteArray gnome;
    if (data->hasFormat(kGnome)) gnome = data->data(kGnome);

    // Nautilus on some versions offers only its own format, with no
    // text/uri-list beside it.
    if (urls.isEmpty() && !gnome.isEmpty()) {
        const QList<QByteArray> lines = gnome.split('\n');
        for (qsizetype i = 1; i < lines.size(); ++i) {
            const QByteArray l = lines.at(i).trimmed();
            if (!l.isEmpty()) urls.append(QUrl::fromEncoded(l));
        }
    }
    for (const QUrl &u : urls) {
        if (u.isLocalFile()) out.paths.append(QDir::cleanPath(u.toLocalFile()));
    }

    if (data->hasFormat(kOurCut)) {
        out.cut = data->data(kOurCut) == "1";
    } else if (data->hasFormat(kKdeCut)) {
        out.cut = data->data(kKdeCut) == "1";
    } else if (!gnome.isEmpty()) {
        out.cut = gnome.startsWith("cut\n") || gnome == "cut";
    } else if (data->hasFormat(kWinEffect)) {
        const QByteArray e = data->data(kWinEffect);
        if (e.size() >= 4) out.cut = qFromLittleEndian<quint32>(e.constData()) == kDropEffectMove;
    }
    return out;
}

bool isOurCut(const QMimeData *data) {
    return data && data->hasFormat(kOurCut) && data->data(kOurCut) == "1";
}

}  // namespace anyfile
