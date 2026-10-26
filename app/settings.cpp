// app/settings.cpp
#include "app/settings.h"

#include <QDir>

namespace anyfile {

QString Settings::configDir() {
    const QString env = qEnvironmentVariable("ANYFILEQT_CONFIG_DIR");
    if (!env.isEmpty()) return QDir::cleanPath(env);
    return QDir::home().filePath(QStringLiteral(".anyfileqt"));
}

QString Settings::configPath() {
    return QDir(configDir()).filePath(QStringLiteral("settings.ini"));
}

// Constructing a QSettings writes nothing; the file and its directory appear on
// the first sync() after a setValue(). `anyfile --check` relies on that.
Settings::Settings() : m_s(configPath(), QSettings::IniFormat) {}

}  // namespace anyfile
