#pragma once

#include <QAbstractListModel>
#include <QVariantList>

namespace RoomTunes {

class Household;
class ZonePlayer;
class MediaItem;

// Sonos itself doesn't expose a "recently played" ContentDirectory
// container -- confirmed empirically: browsing the true root ("0") on a
// real household lists only A:/S:/SQ:/R:/FV:/Q:, nothing else, and
// RecentlyPlayedUpdateID (a real GENA LastChange variable, so Sonos does
// track *something* internally) doesn't correspond to any browsable id.
// This is RoomTunes' own client-side substitute instead: watches every
// known zone's currentTrackChanged and keeps a persisted JSON,
// most-recent-first, deduplicated-by-uri history across the whole
// household (not scoped to one room -- matches how a shared household
// history reads in practice). A play event is recorded once per actual
// track change; a GENA metadata tick that re-announces the same
// track/station (e.g. Internet radio periodically resending "now playing"
// info) doesn't count as a fresh play since it wouldn't change what's
// already the most recent entry.
class RecentlyPlayedModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        TitleRole = Qt::UserRole + 1,
        ArtistRole,
        ImageUrlRole,
        // The full playable-item QVariantMap ({id, parentId, title,
        // artist, album, imageUrl, uri, upnpClass, container}) --
        // everything ZonePlayer::playItem() needs, so a QML delegate can
        // just call zone.playItem(model.item) directly.
        ItemRole,
    };

    explicit RecentlyPlayedModel(Household *household, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    void watchZone(ZonePlayer *zone);
    void onTrackChanged(ZonePlayer *zone);
    void recordPlay(MediaItem *track);
    void load();
    void save();

    Household *m_household;
    QVariantList m_entries; // most-recent-first
};

}
