#include "BrowseRecencyStore.h"

#include <QSettings>

namespace RoomTunes {

BrowseRecencyStore::BrowseRecencyStore(QObject *parent)
    : QObject(parent)
{
}

void BrowseRecencyStore::recordUse(const QString &key)
{
    if (key.isEmpty())
        return;

    QSettings settings;
    settings.setValue(settingKey(key), now());
}

void BrowseRecencyStore::refresh()
{
    ++m_revision;
    emit changed();
}

qint64 BrowseRecencyStore::score(const QString &key) const
{
    if (key.isEmpty())
        return 0;

    QSettings settings;
    return settings.value(settingKey(key), 0).toLongLong();
}

QString BrowseRecencyStore::settingKey(const QString &key) const
{
    return QStringLiteral("BrowseRecency/%1").arg(key);
}

qint64 BrowseRecencyStore::now() const
{
    return QDateTime::currentMSecsSinceEpoch();
}

}
