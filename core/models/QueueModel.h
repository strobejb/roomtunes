#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QMetaObject>

#include "../zone/ZonePlayer.h"

namespace RoomTunes
{

class MediaItem;

// The current play queue ("Up Next") for whichever zone is assigned via
// setZone -- one row per track, fetched via ContentDirectory::Browse on
// "Q:0" (Sonos's well-known object ID for a zone's own queue). Re-fetched
// whenever the assigned zone's own queueChanged() fires (see setZone()), so
// an add/remove/replace made through this app's own context menus shows up
// without the user needing to reopen the panel.
class QueueModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(RoomTunes::ZonePlayer *zone READ zone WRITE setZone NOTIFY zoneChanged)

  public:
    enum Role
    {
        TitleRole = Qt::UserRole + 1,
        ArtistRole,
        ImageUrlRole,
        IdRole,
        ParentIdRole,
        UriRole,
        ItemRole,
    };

    explicit QueueModel(QObject *parent = nullptr);

    ZonePlayer *zone() const
    {
        return m_zone;
    }

    void setZone(ZonePlayer *zone);

    int                    rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant               data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE void       moveTrack(int fromIndex, int toIndex);
    Q_INVOKABLE void       commitTrackMove(int fromIndex, int toIndex);

  signals:
    void zoneChanged();

  private:
    void refresh();

  private:
    ZonePlayer             *m_zone = nullptr;
    QList<MediaItem *>      m_items; // owned
    int                     m_updateId = 0;
    QMetaObject::Connection m_queueChangedConnection;
};

} // namespace RoomTunes
