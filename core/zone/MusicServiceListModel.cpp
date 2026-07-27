#include "MusicServiceListModel.h"

#include <algorithm>

#include "BrowseRecencyStore.h"
#include "../services/MusicService.h"
#include "Household.h"

namespace RoomTunes {

MusicServiceListModel::MusicServiceListModel(Household *household, BrowseRecencyStore *recencyStore, QObject *parent)
    : QAbstractListModel(parent)
    , m_household(household)
    , m_recencyStore(recencyStore)
{
    connect(household, &Household::musicServicesChanged, this, &MusicServiceListModel::rebuild);
}

void MusicServiceListModel::rebuild()
{
    // Safe as a plain reset rather than a fine-grained diff: Household
    // preserves MusicService instances across a rebuild (see
    // Household::rebuildMusicServices()), so a reset just re-reads the
    // same pointers -- nothing a QML delegate is bound to actually dangles.
    beginResetModel();
    endResetModel();
}

int MusicServiceListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;

    return services().size();
}

QVariant MusicServiceListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};

    MusicService *service = serviceAt(index.row());
    if (!service)
        return {};

    switch (role) {
    case ServiceRole:
        return QVariant::fromValue<QObject *>(service);
    case TitleRole:
        return service->title();
    case ImageUrlRole:
        return service->iconSource();
    case ServiceKeyRole:
        return service->serviceKey();
    default:
        return {};
    }
}

QHash<int, QByteArray> MusicServiceListModel::roleNames() const
{
    return {
        { ServiceRole, "serviceObject" },
        { TitleRole, "title" },
        { ImageUrlRole, "imageUrl" },
        { ServiceKeyRole, "serviceKey" },
    };
}

MusicService *MusicServiceListModel::serviceAt(int row) const
{
    const QList<MusicService *> ordered = services();
    if (row < 0 || row >= ordered.size())
        return nullptr;

    return ordered.at(row);
}

QList<MusicService *> MusicServiceListModel::services() const
{
    QList<MusicService *> ordered;
    for (MusicService *service : m_household->services()) {
        if (service->serviceKey() == QStringLiteral("sonos-library"))
            continue;
        ordered.append(service);
    }

    std::sort(ordered.begin(), ordered.end(), [this](MusicService *a, MusicService *b) {
        const qint64 aScore = m_recencyStore->score(a->serviceKey());
        const qint64 bScore = m_recencyStore->score(b->serviceKey());
        if (aScore != bScore)
            return aScore > bScore;
        return a->title().localeAwareCompare(b->title()) < 0;
    });

    return ordered;
}

}
