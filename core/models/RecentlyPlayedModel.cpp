#include "RecentlyPlayedModel.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "../Logging.h"
#include "../media/MediaItem.h"
#include "../settings/Settings.h"
#include "../zone/Household.h"
#include "../zone/ZonePlayer.h"

#define QLOG_CATEGORY logZone

namespace RoomTunes {

namespace {
constexpr int kMaxEntries = 30;

QString recentlyPlayedFilePath()
{
    return QDir(configDirectoryPath()).filePath(QStringLiteral("recently-played.json"));
}

QJsonObject variantMapToJsonObject(const QVariantMap &map)
{
    QJsonObject object;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        object.insert(it.key(), QJsonValue::fromVariant(it.value()));
    return object;
}

QVariantMap jsonObjectToVariantMap(const QJsonObject &object)
{
    QVariantMap map;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        map.insert(it.key(), it.value().toVariant());
    return map;
}
}

RecentlyPlayedModel::RecentlyPlayedModel(Household *household, QObject *parent)
    : QAbstractListModel(parent)
    , m_household(household)
{
    load();

    for (ZonePlayer *zone : household->zones())
        watchZone(zone);
    connect(household, &Household::zoneReady, this, &RecentlyPlayedModel::watchZone);
}

int RecentlyPlayedModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_entries.size();
}

QVariant RecentlyPlayedModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};

    const QVariantMap entry = m_entries.at(index.row()).toMap();
    switch (role) {
    case TitleRole:
        return entry.value(QStringLiteral("title"));
    case ArtistRole:
        return entry.value(QStringLiteral("artist"));
    case ImageUrlRole:
        return entry.value(QStringLiteral("imageUrl"));
    case ItemRole:
        return entry;
    default:
        return {};
    }
}

QHash<int, QByteArray> RecentlyPlayedModel::roleNames() const
{
    return {
        { TitleRole, "title" },
        { ArtistRole, "artist" },
        { ImageUrlRole, "imageUrl" },
        { ItemRole, "item" },
    };
}

void RecentlyPlayedModel::watchZone(ZonePlayer *zone)
{
    connect(zone, &ZonePlayer::playbackItemSelected, this, &RecentlyPlayedModel::recordSelectedItem,
            Qt::UniqueConnection);
}

void RecentlyPlayedModel::recordSelectedItem(const QVariantMap &item)
{
    const QString uri = item.value(QStringLiteral("uri")).toString();
    if (uri.isEmpty() || item.value(QStringLiteral("container")).toBool())
        return;

    if (!m_entries.isEmpty() && m_entries.first().toMap().value(QStringLiteral("uri")).toString() == uri)
        return;

    QVariantMap entry;
    entry[QStringLiteral("id")] = item.value(QStringLiteral("id"));
    entry[QStringLiteral("parentId")] = item.value(QStringLiteral("parentId"));
    entry[QStringLiteral("title")] = item.value(QStringLiteral("title"));
    entry[QStringLiteral("artist")] = item.value(QStringLiteral("artist"));
    entry[QStringLiteral("album")] = item.value(QStringLiteral("album"));
    entry[QStringLiteral("imageUrl")] = item.value(QStringLiteral("imageUrl"));
    entry[QStringLiteral("uri")] = uri;
    entry[QStringLiteral("upnpClass")] = item.value(QStringLiteral("upnpClass"));
    entry[QStringLiteral("container")] = false;

    QLOG() << "recently played: selected track:" << entry.value(QStringLiteral("title")).toString()
           << "--" << entry.value(QStringLiteral("artist")).toString();

    beginResetModel();

    // Any existing entry for the same uri moves back to the front rather
    // than appearing twice.
    for (int i = m_entries.size() - 1; i >= 0; --i) {
        if (m_entries.at(i).toMap().value(QStringLiteral("uri")).toString() == uri)
            m_entries.removeAt(i);
    }

    m_entries.prepend(entry);
    while (m_entries.size() > kMaxEntries)
        m_entries.removeLast();

    endResetModel();

    save();
}

void RecentlyPlayedModel::load()
{
    QFile file(recentlyPlayedFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return;

    const QJsonArray entries = QJsonDocument::fromJson(file.readAll()).array();
    for (const QJsonValue &entry : entries) {
        if (entry.isObject())
            m_entries.append(jsonObjectToVariantMap(entry.toObject()));
    }
}

void RecentlyPlayedModel::save()
{
    QJsonArray entries;
    for (const QVariant &entry : std::as_const(m_entries))
        entries.append(variantMapToJsonObject(entry.toMap()));

    QFile file(recentlyPlayedFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;

    file.write(QJsonDocument(entries).toJson(QJsonDocument::Indented));
}

}
