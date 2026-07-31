#pragma once

#include <QAbstractListModel>
#include <QSet>

#include "../zone/ZonePlayer.h"

namespace RoomTunes {

class Household;

// One row per Sonos play-group -- a set of zones playing in sync, sharing
// a coordinator -- built from Household's zones. Zones the topology marks
// invisible (bonded SUB/surround satellites, stereo-pair slaves) are
// excluded entirely: they aren't independently controllable rooms.
class GroupedZoneModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        CoordinatorRole = Qt::UserRole + 1,
        MembersRole, // QVariantList of ZonePlayer*, coordinator first
    };

    explicit GroupedZoneModel(Household *household, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE RoomTunes::ZonePlayer *firstCoordinator() const;
    Q_INVOKABLE RoomTunes::ZonePlayer *coordinatorByUdn(const QString &udn) const;
    Q_INVOKABLE int coordinatorIndex(const QString &udn) const;
    Q_INVOKABLE RoomTunes::ZonePlayer *canonicalCoordinator(RoomTunes::ZonePlayer *zone) const;

private slots:
    void rebuild();

private:
    // Every zone property change (roomName, ready, coordinator, invisible)
    // requests a rebuild, and there can be a dozen zones each firing
    // several of these in a burst during startup. Rebuilding is a full
    // model reset (destroys/recreates every delegate), so coalesce any
    // number of requests within the same event-loop iteration into a
    // single rebuild rather than one reset per signal.
    void scheduleRebuild();

    struct Group
    {
        ZonePlayer *coordinator = nullptr;
        QList<ZonePlayer *> members;
    };

private:
    Household *m_household;
    QList<Group> m_groups;
    QSet<ZonePlayer *> m_connected;
    bool m_rebuildScheduled = false;
};

}
