// app/conflictdialog.h
//
// "Replace or Skip Files", Explorer's shape: one command-link button per
// choice, each saying what it means for this file, plus "Do this for all
// conflicts". Closing the dialog is Cancel, which stops the whole job.
#pragma once

#include <QDialog>

#include "anyfile/fileop.h"

class QCheckBox;

namespace anyfile {

class ConflictDialog : public QDialog {
    Q_OBJECT
public:
    ConflictDialog(const ConflictInfo &info, QWidget *parent = nullptr);

    ConflictChoice choice() const { return m_choice; }
    bool applyToAll() const;

private:
    void choose(ConflictChoice c);

    ConflictChoice m_choice = ConflictChoice::Cancel;
    QCheckBox *m_all = nullptr;
};

}  // namespace anyfile
