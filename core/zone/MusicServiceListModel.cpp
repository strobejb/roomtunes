#include "MusicServiceListModel.h"

#include <algorithm>

#include "BrowseHistoryStore.h"
#include "../services/MusicService.h"
#include "Household.h"

namespace RoomTunes {

namespace {

QVariantMap serviceItem(MusicService *service)
{
    QVariantMap item;
    item[QStringLiteral("id")] = service->serviceKey();
    item[QStringLiteral("objectId")] = QStringLiteral("root");
    item[QStringLiteral("root")] = QStringLiteral("root");
    item[QStringLiteral("title")] = service->title();
    item[QStringLiteral("imageUrl")] = service->iconSource();
    item[QStringLiteral("serviceId")] = service->serviceId();
    item[QStringLiteral("serviceKey")] = service->serviceKey();
    item[QStringLiteral("serviceObject")] = QVariant::fromValue<QObject *>(service);
    item[QStringLiteral("container")] = true;
    item[QStringLiteral("kind")] = QStringLiteral("service");
    return item;
}

}

MusicServiceListModel::MusicServiceListModel(Household *household, BrowseHistoryStore *browseHistoryStore, QObject *parent)
    : QAbstractListModel(parent)
    , m_household(household)
    , m_browseHistoryStore(browseHistoryStore)
{
    m_services = orderedServices();
    connect(household, &Household::musicServicesChanged, this, &MusicServiceListModel::rebuild);
}

void MusicServiceListModel::rebuild()
{
    // Safe as a plain reset rather than a fine-grained diff: Household
    // preserves MusicService instances across a rebuild (see
    // Household::rebuildMusicServices()), so a reset just re-reads the
    // same pointers -- nothing a QML delegate is bound to actually dangles.
    beginResetModel();
    m_services = orderedServices();
    endResetModel();
}

int MusicServiceListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;

    return m_services.size();
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
    case ServiceIdRole:
        return service->serviceId();
    case ItemRole:
        return serviceItem(service);
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
        { ServiceIdRole, "serviceId" },
        { ItemRole, "item" },
    };
}

MusicService *MusicServiceListModel::serviceAt(int row) const
{
    if (row < 0 || row >= m_services.size())
        return nullptr;

    return m_services.at(row);
}

QVariantList MusicServiceListModel::searchServiceItems() const
{
    QList<MusicService *> ordered;
    if (MusicService *library = m_household->libraryService())
        ordered.append(library);

    for (MusicService *service : m_household->services()) {
        if (!service || service->serviceKey() == QStringLiteral("sonos-library"))
            continue;
        if (service->canSearch())
            ordered.append(service);
    }

    std::sort(ordered.begin(), ordered.end(), [this](MusicService *a, MusicService *b) {
        const qint64 aScore = browseHistoryScore(a);
        const qint64 bScore = browseHistoryScore(b);
        if (aScore != bScore)
            return aScore > bScore;
        return a->title().localeAwareCompare(b->title()) < 0;
    });

    QVariantList result;
    result.reserve(ordered.size());
    for (MusicService *service : ordered)
        result.append(serviceItem(service));
    return result;
}

QList<MusicService *> MusicServiceListModel::orderedServices() const
{
    QList<MusicService *> ordered;
    for (MusicService *service : m_household->services()) {
        if (service->serviceKey() == QStringLiteral("sonos-library"))
            continue;
        ordered.append(service);
    }

    std::sort(ordered.begin(), ordered.end(), [this](MusicService *a, MusicService *b) {
        const qint64 aScore = m_browseHistoryStore->score(a->serviceKey());
        const qint64 bScore = m_browseHistoryStore->score(b->serviceKey());
        if (aScore != bScore)
            return aScore > bScore;
        return a->title().localeAwareCompare(b->title()) < 0;
    });

    return ordered;
}

qint64 MusicServiceListModel::browseHistoryScore(MusicService *service) const
{
    if (!service)
        return 0;

    qint64 score = m_browseHistoryStore->score(service->serviceKey());
    if (service->serviceKey() == QStringLiteral("sonos-library")) {
        // BrowseHome records the visible "Music Library" tile with the same
        // key scheme as Line-In/TV sources, while the service itself is
        // identified as sonos-library everywhere else. Treat either as a
        // library access so all-source search stays ordered by what the user
        // actually opened most recently.
        score = std::max(score, m_browseHistoryStore->score(QStringLiteral("browse:source:library")));
    }
    return score;
}

}
