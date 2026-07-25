#include "RecentlyPlayedModel.h"

#include <QSettings>

#include "../Logging.h"
#include "../media/MediaItem.h"
#include "Household.h"
#include "ZonePlayer.h"

#define QLOG_CATEGORY logZone

namespace RoomTunes {

namespace {
constexpr int kMaxEntries = 30;
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
    connect(zone, &ZonePlayer::currentTrackChanged, this, [this, zone]() { onTrackChanged(zone); });

    // Seeds the list with whatever's already loaded on this zone, not
    // just future changes -- a zone's currentTrack is very likely already
    // set by the time this runs (zoneReady fires once the zone's own
    // topology/transport state, including its current track, has already
    // been fetched and parsed), so waiting only for the *next*
    // currentTrackChanged would miss it entirely until something actually
    // changes tracks again this session.
    onTrackChanged(zone);
}

void RecentlyPlayedModel::onTrackChanged(ZonePlayer *zone)
{
    MediaItem *track = zone->currentTrack();
    // No uri -- nothing playable was actually loaded (e.g. the zone just
    // stopped) -- and a container entry can't be replayed via playItem()
    // the same way a leaf track/station can, so neither belongs in a
    // "recently played" list of playable things.
    if (!track || track->uri().isEmpty() || track->isContainer())
        return;

    recordPlay(track);
}

void RecentlyPlayedModel::recordPlay(MediaItem *track)
{
    // A metadata-only tick on what's already the most recent entry (e.g.
    // Internet radio periodically re-announcing the same now-playing
    // info via GENA) isn't a fresh play.
    if (!m_entries.isEmpty()
        && m_entries.first().toMap().value(QStringLiteral("uri")).toString() == track->uri())
        return;

    QVariantMap entry;
    entry[QStringLiteral("id")] = track->id();
    entry[QStringLiteral("parentId")] = track->parentId();
    entry[QStringLiteral("title")] = track->title();
    entry[QStringLiteral("artist")] = track->artist();
    entry[QStringLiteral("album")] = track->album();
    entry[QStringLiteral("imageUrl")] = track->imageUrl();
    entry[QStringLiteral("uri")] = track->uri();
    entry[QStringLiteral("upnpClass")] = track->upnpClass();
    entry[QStringLiteral("container")] = false;

    QLOG() << "recording play:" << track->title() << "--" << track->artist();

    beginResetModel();

    // Any existing entry for the same uri moves back to the front rather
    // than appearing twice.
    for (int i = m_entries.size() - 1; i >= 0; --i) {
        if (m_entries.at(i).toMap().value(QStringLiteral("uri")).toString() == track->uri())
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
    QSettings settings;
    settings.beginGroup(QStringLiteral("RecentlyPlayed"));
    m_entries = settings.value(QStringLiteral("entries")).toList();
    settings.endGroup();
}

void RecentlyPlayedModel::save()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("RecentlyPlayed"));
    settings.setValue(QStringLiteral("entries"), m_entries);
    settings.endGroup();
}

}
