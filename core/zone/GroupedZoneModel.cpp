#include "GroupedZoneModel.h"

#include <algorithm>

#include <QTimer>

#include "Household.h"
#include "ZonePlayer.h"

namespace RoomTunes {

GroupedZoneModel::GroupedZoneModel(Household *household, QObject *parent)
    : QAbstractListModel(parent)
    , m_household(household)
{
    connect(household, &Household::zoneListChanged, this, &GroupedZoneModel::scheduleRebuild);
    rebuild();
}

int GroupedZoneModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_groups.size();
}

QVariant GroupedZoneModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_groups.size())
        return {};

    const Group &group = m_groups.at(index.row());

    switch (role) {
    case CoordinatorRole:
        return QVariant::fromValue(group.coordinator);
    case MembersRole: {
        QVariantList list;
        for (ZonePlayer *zone : group.members)
            list.append(QVariant::fromValue(zone));
        return list;
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> GroupedZoneModel::roleNames() const
{
    return {
        { CoordinatorRole, "coordinator" },
        { MembersRole, "members" },
    };
}

void GroupedZoneModel::rebuild()
{
    beginResetModel();

    QMap<QString, Group> byCoordinator;
    const QList<ZonePlayer *> zones = m_household->zones();

    for (ZonePlayer *zone : zones) {
        if (zone->invisible())
            continue;

        Group &group = byCoordinator[zone->coordinatorUdn()];
        if (zone->isCoordinator())
            group.coordinator = zone;
        group.members.append(zone);
    }

    m_groups = byCoordinator.values();

    for (Group &group : m_groups) {
        std::sort(group.members.begin(), group.members.end(), [&group](ZonePlayer *a, ZonePlayer *b) {
            if (a == group.coordinator)
                return b != group.coordinator;
            if (b == group.coordinator)
                return false;
            return a->roomName() < b->roomName();
        });
    }

    std::sort(m_groups.begin(), m_groups.end(), [](const Group &a, const Group &b) {
        const QString nameA = a.coordinator ? a.coordinator->roomName() : QString();
        const QString nameB = b.coordinator ? b.coordinator->roomName() : QString();
        return nameA < nameB;
    });

    endResetModel();

    for (ZonePlayer *zone : zones) {
        if (m_connected.contains(zone))
            continue;
        m_connected.insert(zone);

        connect(zone, &ZonePlayer::roomNameChanged, this, &GroupedZoneModel::scheduleRebuild);
        connect(zone, &ZonePlayer::readyChanged, this, &GroupedZoneModel::scheduleRebuild);
        connect(zone, &ZonePlayer::coordinatorChanged, this, &GroupedZoneModel::scheduleRebuild);
        connect(zone, &ZonePlayer::invisibleChanged, this, &GroupedZoneModel::scheduleRebuild);
    }
}

void GroupedZoneModel::scheduleRebuild()
{
    if (m_rebuildScheduled)
        return;

    m_rebuildScheduled = true;
    QTimer::singleShot(0, this, [this]() {
        m_rebuildScheduled = false;
        rebuild();
    });
}

}
