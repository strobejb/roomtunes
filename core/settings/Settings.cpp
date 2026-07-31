#include "Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

namespace RoomTunes {

namespace {

constexpr auto kOrganizationName = "catch22";
constexpr auto kApplicationName = "roomtunes";

QString appConfigDirPath()
{
    const QString configRoot = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    QDir dir(configRoot);
    const QString appConfigDir = QString::fromLatin1(kOrganizationName) + QLatin1Char('/')
        + QString::fromLatin1(kApplicationName);
    dir.mkpath(appConfigDir);
    return dir.filePath(appConfigDir);
}

QString settingsFilePath(const QString &fileName)
{
    QDir dir(appConfigDirPath());
    return dir.filePath(fileName);
}

}

void configureApplicationSettings()
{
    QCoreApplication::setOrganizationName(QString::fromLatin1(kOrganizationName));
    QCoreApplication::setApplicationName(QString::fromLatin1(kApplicationName));
}

QString configDirectoryPath()
{
    return appConfigDirPath();
}

QString applicationSettingsFilePath()
{
    return settingsFilePath(QStringLiteral("roomtunes.conf"));
}

QString smapiSettingsFilePath()
{
    return settingsFilePath(QStringLiteral("smapi.conf"));
}

}
