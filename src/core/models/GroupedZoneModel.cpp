#include "GroupedZoneModel.h"

#include <algorithm>

#include <QTimer>

#include "../zone/Household.h"
#include "../zone/ZonePlayer.h"

namespace RoomTunes
{

GroupedZoneModel::GroupedZoneModel(Household *household, QObject *parent)
    : QAbstractListModel(parent), m_household(household)
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

    switch (role)
    {
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
        {CoordinatorRole, "coordinator"},
        {MembersRole, "members"},
    };
}

void GroupedZoneModel::rebuild()
{
    beginResetModel();

    QMap<QString, Group>      byCoordinator;
    const QList<ZonePlayer *> zones = m_household->zones();

    for (ZonePlayer *zone : zones)
    {
        if (zone->invisible() || !zone->hasValidTopology())
            continue;

        Group &group = byCoordinator[zone->coordinatorUdn()];
        if (zone->isCoordinator())
            group.coordinator = zone;
        group.members.append(zone);
    }

    m_groups.clear();
    for (const Group &group : byCoordinator)
    {
        if (group.coordinator)
            m_groups.append(group);
    }

    for (Group &group : m_groups)
    {
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

    for (ZonePlayer *zone : zones)
    {
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

ZonePlayer *GroupedZoneModel::firstCoordinator() const
{
    return m_groups.isEmpty() ? nullptr : m_groups.constFirst().coordinator;
}

ZonePlayer *GroupedZoneModel::coordinatorByUdn(const QString &udn) const
{
    if (udn.isEmpty())
        return nullptr;

    for (const Group &group : m_groups)
    {
        if (group.coordinator && group.coordinator->udn() == udn)
            return group.coordinator;
    }

    return nullptr;
}

int GroupedZoneModel::coordinatorIndex(const QString &udn) const
{
    if (udn.isEmpty())
        return -1;

    for (int i = 0; i < m_groups.size(); ++i)
    {
        ZonePlayer *coordinator = m_groups.at(i).coordinator;
        if (coordinator && coordinator->udn() == udn)
            return i;
    }

    return -1;
}

ZonePlayer *GroupedZoneModel::canonicalCoordinator(ZonePlayer *zone) const
{
    if (!zone)
        return firstCoordinator();

    for (const Group &group : m_groups)
    {
        if (group.coordinator == zone)
            return zone;
        if (group.members.contains(zone))
            return group.coordinator;
    }

    return firstCoordinator();
}

} // namespace RoomTunes
