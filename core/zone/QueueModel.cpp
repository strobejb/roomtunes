#include "QueueModel.h"

#include "../Logging.h"
#include "../media/MediaItem.h"

namespace RoomTunes {

QueueModel::QueueModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void QueueModel::setZone(ZonePlayer *zone)
{
    if (m_zone == zone)
        return;

    QObject::disconnect(m_queueChangedConnection);

    m_zone = zone;
    emit zoneChanged();
    refresh();

    if (m_zone)
        m_queueChangedConnection = connect(m_zone, &ZonePlayer::queueChanged, this, &QueueModel::refresh);
}

void QueueModel::refresh()
{
    beginResetModel();
    qDeleteAll(m_items);
    m_items.clear();
    endResetModel();

    if (!m_zone)
        return;

    ZonePlayer *zone = m_zone;
    zone->browse(QStringLiteral("Q:0"), [this, zone](bool ok, const QString &, const QList<DidlItem> &items) {
        // The selected zone may have changed again while this request was
        // in flight -- a stale reply landing after that shouldn't clobber
        // the (already-requested-fresh) model for the new selection.
        if (!ok || m_zone != zone)
            return;

        beginResetModel();
        qDeleteAll(m_items);
        m_items.clear();
        for (const DidlItem &item : items) {
            DidlItem resolved = item;
            // Local-library album art comes back as a path relative to the
            // zone itself -- same resolution ZonePlayer::refreshTransportState
            // does for the current track's art.
            if (!resolved.albumArtUri.isEmpty() && resolved.albumArtUri.startsWith(QLatin1Char('/')))
                resolved.albumArtUri = zone->baseUrl().chopped(1) + resolved.albumArtUri;
            m_items.append(MediaItem::fromDidl(resolved, this));
        }
        endResetModel();
    });
}

int QueueModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_items.size();
}

QVariant QueueModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};

    const MediaItem *item = m_items.at(index.row());
    switch (role) {
    case TitleRole:
        return item->title();
    case ArtistRole:
        return item->artist();
    case ImageUrlRole:
        return item->imageUrl();
    case IdRole:
        return item->id();
    case ParentIdRole:
        return item->parentId();
    case UriRole:
        return item->uri();
    default:
        return {};
    }
}

QHash<int, QByteArray> QueueModel::roleNames() const
{
    return {
        { TitleRole, "title" },
        { ArtistRole, "artist" },
        { ImageUrlRole, "imageUrl" },
        { IdRole, "id" },
        { ParentIdRole, "parentId" },
        { UriRole, "uri" },
    };
}

}
