#include "QueueModel.h"

#include "../Logging.h"
#include "../media/MediaItem.h"

namespace RoomTunes
{

QueueModel::QueueModel(QObject *parent) : QAbstractListModel(parent)
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
    m_updateId = 0;
    endResetModel();

    if (!m_zone)
        return;

    ZonePlayer *zone = m_zone;
    zone->browseQueue([this, zone](bool ok, const QString &, const QList<DidlItem> &items, int updateId) {
        // The selected zone may have changed again while this request was
        // in flight -- a stale reply landing after that shouldn't clobber
        // the (already-requested-fresh) model for the new selection.
        if (!ok || m_zone != zone)
            return;

        beginResetModel();
        qDeleteAll(m_items);
        m_items.clear();
        for (const DidlItem &item : items)
        {
            DidlItem resolved = item;
            // Local-library album art comes back as a path relative to the
            // zone itself -- same resolution ZonePlayer::refreshTransportState
            // does for the current track's art.
            if (!resolved.albumArtUri.isEmpty() && resolved.albumArtUri.startsWith(QLatin1Char('/')))
                resolved.albumArtUri = zone->baseUrl().chopped(1) + resolved.albumArtUri;
            m_items.append(MediaItem::fromDidl(resolved, this));
        }
        m_updateId = updateId;
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
    switch (role)
    {
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
    case ItemRole:
        return item->toVariantMap();
    default:
        return {};
    }
}

QHash<int, QByteArray> QueueModel::roleNames() const
{
    return {
        {TitleRole, "title"},       {ArtistRole, "artist"}, {ImageUrlRole, "imageUrl"}, {IdRole, "id"},
        {ParentIdRole, "parentId"}, {UriRole, "uri"},       {ItemRole, "item"},
    };
}

void QueueModel::moveTrack(int fromIndex, int toIndex)
{
    if (fromIndex == toIndex)
        return;
    if (fromIndex < 0 || fromIndex >= m_items.size())
        return;
    if (toIndex < 0 || toIndex >= m_items.size())
        return;

    beginMoveRows(QModelIndex(), fromIndex, fromIndex, QModelIndex(), toIndex > fromIndex ? toIndex + 1 : toIndex);
    m_items.move(fromIndex, toIndex);
    endMoveRows();
}

void QueueModel::commitTrackMove(int fromIndex, int toIndex)
{
    if (!m_zone || fromIndex == toIndex)
        return;
    if (fromIndex < 0 || toIndex < 0)
        return;

    m_zone->reorderQueueTrack(fromIndex, toIndex, m_updateId);
}

} // namespace RoomTunes
