#include "Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

namespace RoomTunes {

namespace {

constexpr auto kOrganizationName = "catch22";
constexpr auto kApplicationName = "q22";

}

void configureApplicationSettings()
{
    QCoreApplication::setOrganizationName(QString::fromLatin1(kOrganizationName));
    QCoreApplication::setApplicationName(QString::fromLatin1(kApplicationName));
}

QString smapiSettingsFilePath()
{
    const QString configRoot = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    QDir dir(configRoot);
    dir.mkpath(QString::fromLatin1(kOrganizationName));
    return dir.filePath(QString::fromLatin1(kOrganizationName) + QStringLiteral("/smapi.conf"));
}

}
