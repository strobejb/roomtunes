#include "MusicServiceListModel.h"

#include "../services/MusicService.h"
#include "Household.h"

namespace RoomTunes {

MusicServiceListModel::MusicServiceListModel(Household *household, QObject *parent)
    : QAbstractListModel(parent)
    , m_household(household)
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
    return m_household->services().size();
}

QVariant MusicServiceListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};

    const QList<MusicService *> services = m_household->services();
    if (index.row() < 0 || index.row() >= services.size())
        return {};

    MusicService *service = services.at(index.row());
    switch (role) {
    case ServiceRole:
        return QVariant::fromValue<QObject *>(service);
    case TitleRole:
        return service->title();
    case ImageUrlRole:
        return service->iconSource();
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
    };
}

}
