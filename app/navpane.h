// app/navpane.h
//
// The navigation pane on the left: Quick access (home and the standard user
// folders that exist) and This PC (the volumes VolumeMonitor reports). On
// macOS and Linux the second group is called Locations, and it has no folder
// of its own to open; on Windows clicking This PC shows the drives.
#pragma once

#include <QList>
#include <QTreeWidget>

#include "anyfile/volumes.h"

namespace anyfile {

class NavPane : public QTreeWidget {
    Q_OBJECT
public:
    explicit NavPane(QWidget *parent = nullptr);

    void setVolumes(const QList<VolumeInfo> &volumes);

    // For the tests: the paths of the volume entries, in order.
    QStringList volumeRoots() const;

signals:
    // Empty means This PC.
    void navigate(const QString &path);

private:
    void onItem(QTreeWidgetItem *item);

    QTreeWidgetItem *m_quick = nullptr;
    QTreeWidgetItem *m_pc = nullptr;
};

}  // namespace anyfile
