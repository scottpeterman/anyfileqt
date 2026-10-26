// anyfileqt/app/propertiesdialog.h
//
// Explorer's Properties: one dialog for one item or for a selection.
//
//   General       name (editable for a single item), type, location, link
//                 target, size and size on disk, what a folder contains --
//                 counted on a worker and updated live -- dates, attributes.
//   Permissions   macOS and Linux, single item: owner, group, the rwx grid,
//                 and the mode as ls prints it and as chmod takes it.
//
// Modeless, as Explorer's are: several can be open at once, and the window
// behind stays usable while a large folder is still being counted.
//
// OK and Apply write a rename and permission changes; nothing is written
// unless a field actually changed. Changing permissions sets the nine rwx bits
// and clears setuid, setgid and sticky, which QFile cannot express -- so the
// grid is applied only when it was touched.
#pragma once

#include <QDialog>
#include <QStringList>

#include "anyfile/fsinfo.h"

class QCheckBox;
class QLabel;
class QLineEdit;

namespace anyfile {

class PropertiesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PropertiesDialog(const QStringList &paths, QWidget *parent = nullptr);

    // Writes the changes; false (with a message box) if any of them failed.
    bool applyChanges();

    // For the tests.
    SizeScanner *scanner() const { return m_scanner; }
    QString typeText() const;
    QString sizeText() const;
    QString containsText() const;
    QString modeText() const;
    void setNameForTest(const QString &name);
    void setPermissionBitForTest(int row, int col, bool on);

    void accept() override;

private:
    QWidget *buildGeneral();
    QWidget *buildPermissions();
    void onTotals(const SizeTotals &t);
    void updateModeLabel();
    QFileDevice::Permissions gridPermissions() const;

    QStringList m_paths;
    bool m_single = false;
    bool m_anyDir = false;
    SizeScanner *m_scanner = nullptr;

    QLineEdit *m_name = nullptr;
    QLabel *m_type = nullptr;
    QLabel *m_size = nullptr;
    QLabel *m_onDisk = nullptr;
    QLabel *m_onDiskCaption = nullptr;
    QLabel *m_contains = nullptr;
    QCheckBox *m_readOnly = nullptr;
    bool m_readOnlyInitial = false;

    QCheckBox *m_bits[3][3] = {};
    QLabel *m_mode = nullptr;
    QFileDevice::Permissions m_initialPerms;
    bool m_isDir = false;
    bool m_isLink = false;
};

}  // namespace anyfile
