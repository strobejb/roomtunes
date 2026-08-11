#include "AppSettings.h"

#include <QSettings>

#include "Settings.h"

namespace RoomTunes
{

namespace
{
constexpr auto kGroupZones          = "Zones";
constexpr auto kLastSelectedZoneUdn = "lastSelectedZoneUdn";
} // namespace

AppSettings::AppSettings(QObject *parent) : QObject(parent)
{
}

QString AppSettings::lastSelectedZoneUdn() const
{
    QSettings settings(applicationSettingsFilePath(), QSettings::IniFormat);
    settings.beginGroup(QString::fromLatin1(kGroupZones));
    return settings.value(QString::fromLatin1(kLastSelectedZoneUdn)).toString();
}

void AppSettings::setLastSelectedZoneUdn(const QString &udn)
{
    if (lastSelectedZoneUdn() == udn)
        return;

    QSettings settings(applicationSettingsFilePath(), QSettings::IniFormat);
    settings.beginGroup(QString::fromLatin1(kGroupZones));
    settings.setValue(QString::fromLatin1(kLastSelectedZoneUdn), udn);
    emit lastSelectedZoneUdnChanged();
}

} // namespace RoomTunes
