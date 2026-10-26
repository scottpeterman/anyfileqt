// app/conflictdialog.cpp
#include "app/conflictdialog.h"

#include <QCheckBox>
#include <QCommandLinkButton>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

#include "anyfile/fsinfo.h"

namespace anyfile {

namespace {
QString describe(qint64 size, const QDateTime &when) {
    return QObject::tr("%1, modified %2")
        .arg(sizeWithBytes(size),
             QLocale().toString(when, QLocale::ShortFormat));
}
}  // namespace

ConflictDialog::ConflictDialog(const ConflictInfo &info, QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Replace or Skip Files"));
    setModal(true);

    const QFileInfo target(info.target);
    const QString folder = target.dir().dirName().isEmpty()
                               ? QDir::toNativeSeparators(target.absolutePath())
                               : target.dir().dirName();

    auto *lay = new QVBoxLayout(this);
    auto *head = new QLabel(
        tr("The destination already has a file named <b>%1</b> in <b>%2</b>.")
            .arg(target.fileName().toHtmlEscaped(), folder.toHtmlEscaped()),
        this);
    head->setWordWrap(true);
    lay->addWidget(head);

    auto *replace = new QCommandLinkButton(tr("Replace the file in the destination"),
                                           tr("Yours: %1").arg(describe(info.sourceSize,
                                                                       info.sourceModified)),
                                           this);
    auto *skip = new QCommandLinkButton(
        tr("Skip this file"),
        tr("Keep the existing one: %1").arg(describe(info.targetSize, info.targetModified)),
        this);
    auto *both = new QCommandLinkButton(tr("Keep both files"),
                                        tr("The file being copied gets a number added to "
                                           "its name"),
                                        this);
    lay->addWidget(replace);
    lay->addWidget(skip);
    lay->addWidget(both);
    connect(replace, &QCommandLinkButton::clicked, this, [this] { choose(ConflictChoice::Replace); });
    connect(skip, &QCommandLinkButton::clicked, this, [this] { choose(ConflictChoice::Skip); });
    connect(both, &QCommandLinkButton::clicked, this, [this] { choose(ConflictChoice::KeepBoth); });

    m_all = new QCheckBox(tr("Do this for all conflicts"), this);
    lay->addWidget(m_all);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(buttons);

    // Skip is the default: the one choice that cannot lose data.
    skip->setDefault(true);
    skip->setFocus();
}

bool ConflictDialog::applyToAll() const { return m_all->isChecked(); }

void ConflictDialog::choose(ConflictChoice c) {
    m_choice = c;
    accept();
}

}  // namespace anyfile
