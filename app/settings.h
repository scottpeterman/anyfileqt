// app/settings.h
//
// ~/.anyfileqt/settings.ini, through QSettings in INI format at an explicit
// path -- not QSettings' platform default, which is the registry on Windows
// and a plist under ~/Library on macOS. One readable file in one place on all
// three, beside ~/.omega and ~/.anyeditqt.
//
// ANYFILEQT_CONFIG_DIR overrides the directory. The app tests point it at a
// fresh temporary directory so they never read or write the real one.
#pragma once

#include <QSettings>
#include <QString>

namespace anyfile {

class Settings {
public:
    static QString configDir();
    static QString configPath();

    Settings();

    QSettings &q() { return m_s; }
    void sync() { m_s.sync(); }

private:
    QSettings m_s;
};

}  // namespace anyfile
