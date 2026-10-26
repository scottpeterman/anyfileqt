// anyfileqt/app/propertiesdialog.cpp
#include "app/propertiesdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QTabWidget>
#include <QVBoxLayout>

#include "anyfile/naming.h"
#include "app/filetypes.h"

namespace anyfile {

namespace {

QFrame *rule(QWidget *parent) {
    auto *f = new QFrame(parent);
    f->setFrameShape(QFrame::HLine);
    f->setFrameShadow(QFrame::Sunken);
    return f;
}

QLabel *value(const QString &text, QWidget *parent) {
    auto *l = new QLabel(text, parent);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);  // paths and sizes get copied
    l->setWordWrap(true);
    return l;
}

QString when(const QDateTime &dt) {
    if (!dt.isValid()) return QObject::tr("Unknown");
    const QLocale loc;
    return loc.toString(dt.date(), QLocale::LongFormat) + QStringLiteral(", ") +
           loc.toString(dt.time(), QLocale::ShortFormat);
}

QString typeOf(const QFileInfo &fi) {
#ifndef Q_OS_WIN
    if (fi.isSymLink()) return QObject::tr("Symbolic link");
#endif
    return friendlyType(fi);
}

// Rows: owner, group, others. Columns: read, write, execute.
const QFileDevice::Permission kBits[3][3] = {
    {QFileDevice::ReadOwner, QFileDevice::WriteOwner, QFileDevice::ExeOwner},
    {QFileDevice::ReadGroup, QFileDevice::WriteGroup, QFileDevice::ExeGroup},
    {QFileDevice::ReadOther, QFileDevice::WriteOther, QFileDevice::ExeOther}};

}  // namespace

PropertiesDialog::PropertiesDialog(const QStringList &paths, QWidget *parent)
    : QDialog(parent), m_paths(paths) {
    m_single = paths.size() == 1;
    for (const QString &p : paths) {
        const QFileInfo fi(p);
        if (fi.isDir() && !fi.isSymLink()) m_anyDir = true;
    }

    const QString first = QFileInfo(paths.value(0)).fileName().isEmpty()
                              ? QDir::toNativeSeparators(paths.value(0))
                              : QFileInfo(paths.value(0)).fileName();
    setWindowTitle(m_single ? tr("%1 Properties").arg(first)
                            : tr("%1, ... Properties").arg(first));

    auto *tabs = new QTabWidget(this);
    tabs->addTab(buildGeneral(), tr("General"));
#ifndef Q_OS_WIN
    if (m_single && !QFileInfo(paths.first()).isSymLink())
        tabs->addTab(buildPermissions(), tr("Permissions"));
#endif

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &PropertiesDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this,
            &PropertiesDialog::applyChanges);

    auto *lay = new QVBoxLayout(this);
    lay->addWidget(tabs);
    lay->addWidget(buttons);
    setMinimumWidth(440);

    // Sizes: a single file is known now; anything with a folder in it is
    // counted on a worker and filled in as it goes.
    if (m_single && !m_anyDir) {
        const QFileInfo fi(paths.first());
        SizeTotals t;
        t.files = 1;
        t.bytes = fi.isSymLink() ? 0 : fi.size();
        t.onDisk = sizeOnDisk(fi.filePath());
        t.onDiskKnown = t.onDisk >= 0;
        t.done = true;
        onTotals(t);
    } else {
        m_scanner = new SizeScanner(this);
        connect(m_scanner, &SizeScanner::updated, this, &PropertiesDialog::onTotals);
        m_size->setText(tr("Calculating..."));
        m_scanner->start(paths);
    }
}

QWidget *PropertiesDialog::buildGeneral() {
    auto *w = new QWidget(this);
    // ONE grid for every section, not a QFormLayout per section: separate
    // forms size their label columns separately, and "Type:", "Size on
    // disk:" and "Modified:" then start their values at three different x
    // positions. Explorer lines them all up.
    auto *g = new QGridLayout(w);
    g->setColumnStretch(1, 1);
    g->setHorizontalSpacing(14);
    int row = 0;
    const auto addRow = [&](QWidget *caption, QWidget *field) {
        g->addWidget(caption, row, 0, Qt::AlignLeft | Qt::AlignTop);
        g->addWidget(field, row, 1);
        ++row;
    };
    const auto addLabelled = [&](const QString &caption, QWidget *field) {
        addRow(new QLabel(caption, w), field);
    };
    const auto addRule = [&] { g->addWidget(rule(w), row++, 0, 1, 2); };

    // Icon and name.
    {
        auto *icon = new QLabel(w);
        QFileIconProvider provider;
        const QIcon ic = m_single ? provider.icon(QFileInfo(m_paths.first()))
                                  : provider.icon(QFileIconProvider::File);
        icon->setPixmap(ic.pixmap(48, 48));
        QWidget *nameField;
        if (m_single && !QFileInfo(m_paths.first()).fileName().isEmpty()) {
            m_name = new QLineEdit(QFileInfo(m_paths.first()).fileName(), w);
            nameField = m_name;
        } else {
            nameField = value(m_single ? QDir::toNativeSeparators(m_paths.first())
                                       : tr("%1 items").arg(m_paths.size()),
                              w);
        }
        g->addWidget(icon, row, 0, Qt::AlignLeft | Qt::AlignVCenter);
        g->addWidget(nameField, row, 1, Qt::AlignVCenter);
        ++row;
    }
    addRule();

    // Type.
    {
        QSet<QString> types;
        for (const QString &p : m_paths) types.insert(typeOf(QFileInfo(p)));
        QString t;
        if (m_single)
            t = *types.begin();
        else if (types.size() == 1)
            t = tr("All of type %1").arg(*types.begin());
        else
            t = tr("Multiple types");
        m_type = value(t, w);
        addLabelled(tr("Type:"), m_type);
    }

    // Location, and the link target for a symlink.
    {
        QSet<QString> parents;
        for (const QString &p : m_paths) parents.insert(QFileInfo(p).absolutePath());
        QString loc;
        if (m_single || parents.size() == 1)
            loc = QDir::toNativeSeparators(*parents.begin());
        else
            loc = tr("Multiple folders");
        if (!m_single && parents.size() == 1) loc = tr("All in %1").arg(loc);
        addLabelled(tr("Location:"), value(loc, w));

        if (m_single) {
            const QFileInfo fi(m_paths.first());
            if (fi.isSymLink())
                addLabelled(tr("Target:"), value(QDir::toNativeSeparators(fi.symLinkTarget()), w));
        }
    }
    addRule();

    m_size = value(QString(), w);
    m_onDisk = value(QString(), w);
    m_onDiskCaption = new QLabel(tr("Size on disk:"), w);
    addLabelled(tr("Size:"), m_size);
    addRow(m_onDiskCaption, m_onDisk);
    if (m_anyDir) {
        m_contains = value(QString(), w);
        addLabelled(tr("Contains:"), m_contains);
    }

    if (m_single) {
        addRule();
        const QFileInfo fi(m_paths.first());
        // birthTime() is invalid on filesystems that do not record it; the
        // row is left out rather than shown as "Unknown".
        if (fi.birthTime().isValid()) addLabelled(tr("Created:"), value(when(fi.birthTime()), w));
        addLabelled(tr("Modified:"), value(when(fi.lastModified()), w));
        addLabelled(tr("Accessed:"), value(when(fi.lastRead()), w));
    }

    // Attributes.
    {
        addRule();
        auto *box = new QWidget(w);
        auto *h = new QHBoxLayout(box);
        h->setContentsMargins(0, 0, 0, 0);
#ifdef Q_OS_WIN
        // Read-only is the FILE_ATTRIBUTE_READONLY bit, which Qt maps to the
        // write permission on Windows. Folders carry the bit too but Explorer
        // ignores it for them, so only files get an editable box.
        m_readOnly = new QCheckBox(tr("Read-only"), box);
        if (m_single && !m_anyDir) {
            m_readOnlyInitial = !QFileInfo(m_paths.first()).isWritable();
            m_readOnly->setChecked(m_readOnlyInitial);
        } else {
            m_readOnly->setEnabled(false);
        }
        h->addWidget(m_readOnly);
#endif
        bool allHidden = true;
        for (const QString &p : m_paths) allHidden = allHidden && QFileInfo(p).isHidden();
        auto *hidden = new QCheckBox(tr("Hidden"), box);
        hidden->setChecked(allHidden);
        hidden->setEnabled(false);
#ifndef Q_OS_WIN
        hidden->setToolTip(tr("On macOS and Linux a name that starts with a dot is hidden."));
#endif
        h->addWidget(hidden);
        h->addStretch(1);
        addLabelled(tr("Attributes:"), box);
    }

    g->setRowStretch(row, 1);
    return w;
}

QWidget *PropertiesDialog::buildPermissions() {
    auto *w = new QWidget(this);
    auto *v = new QVBoxLayout(w);
    const QFileInfo fi(m_paths.first());
    m_initialPerms = fi.permissions();
    m_isDir = fi.isDir();
    m_isLink = fi.isSymLink();

    auto *form = new QFormLayout;
    form->addRow(tr("Owner:"), value(QStringLiteral("%1 (%2)").arg(fi.owner()).arg(fi.ownerId()), w));
    form->addRow(tr("Group:"), value(QStringLiteral("%1 (%2)").arg(fi.group()).arg(fi.groupId()), w));
    m_mode = value(QString(), w);
    QFont mono = m_mode->font();
    mono.setFamily(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::Monospace);
    m_mode->setFont(mono);
    form->addRow(tr("Mode:"), m_mode);
    v->addLayout(form);
    v->addWidget(rule(w));

    auto *grid = new QGridLayout;
    const QString cols[3] = {tr("Read"), tr("Write"), m_isDir ? tr("Enter") : tr("Execute")};
    const QString rows[3] = {tr("Owner"), tr("Group"), tr("Others")};
    for (int c = 0; c < 3; ++c) grid->addWidget(new QLabel(cols[c], w), 0, c + 1, Qt::AlignHCenter);
    for (int r = 0; r < 3; ++r) {
        grid->addWidget(new QLabel(rows[r], w), r + 1, 0);
        for (int c = 0; c < 3; ++c) {
            auto *cb = new QCheckBox(w);
            cb->setChecked(m_initialPerms & kBits[r][c]);
            connect(cb, &QCheckBox::toggled, this, &PropertiesDialog::updateModeLabel);
            m_bits[r][c] = cb;
            grid->addWidget(cb, r + 1, c + 1, Qt::AlignHCenter);
        }
    }
    grid->setColumnStretch(4, 1);
    v->addLayout(grid);

    auto *note = new QLabel(tr("Applying a change sets these nine bits only; setuid, setgid and "
                               "the sticky bit are cleared."),
                            w);
    note->setWordWrap(true);
    QFont small = note->font();
    small.setPointSizeF(small.pointSizeF() * 0.9);
    note->setFont(small);
    v->addWidget(note);
    v->addStretch(1);
    updateModeLabel();
    return w;
}

QFileDevice::Permissions PropertiesDialog::gridPermissions() const {
    QFileDevice::Permissions p;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            if (m_bits[r][c] && m_bits[r][c]->isChecked()) p |= kBits[r][c];
    // QFile maps the "User" bits onto the owner bits on Unix too, so they are
    // set in step: leaving ReadUser set while ReadOwner is cleared would
    // quietly keep the read bit on.
    if (p & QFileDevice::ReadOwner) p |= QFileDevice::ReadUser;
    if (p & QFileDevice::WriteOwner) p |= QFileDevice::WriteUser;
    if (p & QFileDevice::ExeOwner) p |= QFileDevice::ExeUser;
    return p;
}

void PropertiesDialog::updateModeLabel() {
    if (!m_mode) return;
    const QFileDevice::Permissions p = gridPermissions();
    m_mode->setText(QStringLiteral("%1   %2").arg(modeString(p, m_isDir, m_isLink), modeOctal(p)));
}

void PropertiesDialog::onTotals(const SizeTotals &t) {
    m_size->setText(sizeWithBytes(t.bytes));
    const bool showDisk = t.onDiskKnown;
    m_onDisk->setVisible(showDisk);
    m_onDiskCaption->setVisible(showDisk);
    if (showDisk) m_onDisk->setText(sizeWithBytes(t.onDisk));
    if (m_contains) {
        const QLocale loc;
        QString c = tr("%1 Files, %2 Folders")
                        .arg(loc.toString(t.files), loc.toString(t.folders));
        if (t.unreadable > 0)
            c += tr("  (%1 could not be read)").arg(loc.toString(t.unreadable));
        m_contains->setText(c);
    }
}

bool PropertiesDialog::applyChanges() {
    QStringList failures;

    // Permissions first, while the path is still the old one.
    if (m_mode && m_single) {
        const QFileDevice::Permissions want = gridPermissions();
        const QFileDevice::Permissions mask =
            QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
            QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup |
            QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
        if ((want & mask) != (m_initialPerms & mask)) {
            if (QFile::setPermissions(m_paths.first(), want))
                m_initialPerms = want;
            else
                failures << tr("The permissions could not be changed. Only the owner can change them.");
        }
    }

#ifdef Q_OS_WIN
    if (m_readOnly && m_readOnly->isEnabled() && m_readOnly->isChecked() != m_readOnlyInitial) {
        QFileDevice::Permissions p = QFileInfo(m_paths.first()).permissions();
        if (m_readOnly->isChecked())
            p &= ~(QFileDevice::WriteOwner | QFileDevice::WriteUser | QFileDevice::WriteGroup |
                   QFileDevice::WriteOther);
        else
            p |= QFileDevice::WriteOwner | QFileDevice::WriteUser;
        if (QFile::setPermissions(m_paths.first(), p))
            m_readOnlyInitial = m_readOnly->isChecked();
        else
            failures << tr("The read-only attribute could not be changed.");
    }
#endif

    if (m_name && m_single) {
        const QFileInfo fi(m_paths.first());
        const QString wanted = m_name->text().trimmed();
        if (wanted != fi.fileName()) {
            if (wanted.isEmpty() || wanted.contains(QLatin1Char('/'))
#ifdef Q_OS_WIN
                || wanted.contains(QRegularExpression(QStringLiteral("[\\\\:*?\"<>|]")))
#endif
            ) {
                failures << tr("\"%1\" is not a valid name.").arg(wanted);
            } else if (pathExists(QDir(fi.absolutePath()).filePath(wanted)) &&
                       wanted.compare(fi.fileName(), Qt::CaseInsensitive) != 0) {
                failures << tr("There is already an item named \"%1\" in this folder.").arg(wanted);
            } else {
                const QString target = QDir(fi.absolutePath()).filePath(wanted);
                if (QDir().rename(fi.absoluteFilePath(), target)) {
                    m_paths[0] = target;
                    setWindowTitle(tr("%1 Properties").arg(wanted));
                } else {
                    failures << tr("\"%1\" could not be renamed.").arg(fi.fileName());
                }
            }
        }
    }

    if (failures.isEmpty()) return true;
    QMessageBox::warning(this, windowTitle(), failures.join(QLatin1Char('\n')));
    return false;
}

void PropertiesDialog::accept() {
    if (applyChanges()) QDialog::accept();
}

QString PropertiesDialog::typeText() const { return m_type ? m_type->text() : QString(); }
QString PropertiesDialog::sizeText() const { return m_size ? m_size->text() : QString(); }
QString PropertiesDialog::containsText() const { return m_contains ? m_contains->text() : QString(); }
QString PropertiesDialog::modeText() const { return m_mode ? m_mode->text() : QString(); }
void PropertiesDialog::setNameForTest(const QString &name) {
    if (m_name) m_name->setText(name);
}
void PropertiesDialog::setPermissionBitForTest(int row, int col, bool on) {
    if (m_bits[row][col]) m_bits[row][col]->setChecked(on);
}

}  // namespace anyfile
