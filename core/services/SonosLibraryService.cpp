#include "SonosLibraryService.h"

#include "../Logging.h"
#include "../upnp/Didl.h"
#include "../zone/Household.h"
#include "../zone/ZonePlayer.h"

#define QLOG_CATEGORY logZone

namespace RoomTunes {

namespace {
QVariantMap categoryItem(const QString &id, const QString &title)
{
    QVariantMap item;
    item[QStringLiteral("id")] = id;
    item[QStringLiteral("title")] = title;
    item[QStringLiteral("artist")] = QString();
    item[QStringLiteral("album")] = QString();
    item[QStringLiteral("imageUrl")] = QString();
    item[QStringLiteral("container")] = true;
    return item;
}

QString browseObjectIdForItem(const DidlItem &item)
{
    if (!item.id.startsWith(QStringLiteral("FV:2")))
        return item.id;

    return item.didlId.isEmpty() ? item.id : item.didlId;
}

// ContentDirectory album art URIs come back host-relative; resolve them
// against the zone's own base URL before handing them to QML.
QVariantMap didlItemToVariant(const DidlItem &item, const QString &baseUrl)
{
    QString artUrl = item.albumArtUri;
    if (!artUrl.isEmpty() && !artUrl.startsWith(QStringLiteral("http")))
        artUrl = baseUrl + (artUrl.startsWith(QLatin1Char('/')) ? artUrl.mid(1) : artUrl);

    QVariantMap variant;
    variant[QStringLiteral("id")] = item.id;
    variant[QStringLiteral("title")] = item.title;
    variant[QStringLiteral("artist")] = item.artist;
    variant[QStringLiteral("album")] = item.album;
    variant[QStringLiteral("imageUrl")] = artUrl;
    variant[QStringLiteral("container")] = item.container;
    // For FV:* wrappers, keep the parsed inner DIDL id alongside the visible
    // outer favourite id. This is only a browse target hint; the actual
    // SMAPI-vs-ContentDirectory dispatch is driven by serviceId in
    // doBrowseItem(), matching BB10's "item carries service identity" model
    // without trying to infer API routing from URI string shapes.
    variant[QStringLiteral("browseId")] = browseObjectIdForItem(item);
    variant[QStringLiteral("serviceId")] = item.serviceId;
    // Playable fields for ZonePlayer::playItem() -- the DIDL <res> URI is
    // reused as-is. For ordinary library items didlId/didlParentId match
    // id/parentId; for Sonos Favourites, Didl::parseItems() has already
    // pulled these from the favourite's r:resMD inner playable item, which
    // is exactly what roomtunes-bb10's ParseDIDL() did before enqueueing.
    variant[QStringLiteral("uri")] = item.res;
    variant[QStringLiteral("upnpClass")] = item.upnpClass;
    variant[QStringLiteral("didlId")] = item.didlId.isEmpty() ? item.id : item.didlId;
    variant[QStringLiteral("parentId")] = item.didlParentId.isEmpty() ? item.parentId : item.didlParentId;
    variant[QStringLiteral("desc")] = item.desc;
    return variant;
}

}

SonosLibraryService::SonosLibraryService(Household *household, QObject *parent)
    : MusicService(QStringLiteral("sonos-library"), tr("Music Library"),
                    QStringLiteral("qrc:/qt/qml/RoomTunes/resources/icons/library.svg"), parent)
    , m_household(household)
{
}

QVariantList SonosLibraryService::searchCategories() const
{
    // Hardcoded, matching roomtunes-bb10's SonosLibrary::m_searchTerms exactly --
    // a narrower set than the root browse categories (no Genres/Playlists).
    return {
        categoryItem(QStringLiteral("A:ALBUMARTIST"), tr("Artists")),
        categoryItem(QStringLiteral("A:ALBUM"), tr("Albums")),
        categoryItem(QStringLiteral("A:TRACKS"), tr("Tracks")),
    };
}

void SonosLibraryService::doSearch(const QString &category, const QString &term, ResultCallback callback)
{
    m_activeSearchCategoryId = category;
    emit activeSearchCategoryChanged();

    // Sonos' ContentDirectory implementation treats "<category>:<term>" as an
    // ordinary browsable object id -- there's no real ContentDirectory::Search
    // call involved, just like bb10's SonosLibrary::search().
    doBrowse(category + QStringLiteral(":") + term, callback);
}

void SonosLibraryService::doBrowseItem(const QVariantMap &item, ResultCallback callback)
{
    // This is the BB10-style browse choke point. QML passes the clicked item
    // back to the current service; C++ decides whether the item is still a
    // Sonos-library ContentDirectory object or actually belongs to a SMAPI
    // service discovered through a Sonos Favourite wrapper.
    const int itemServiceId = item.value(QStringLiteral("serviceId"), -1).toInt();
    if (itemServiceId > 0) {
        MusicService *service = m_household->serviceById(itemServiceId);
        if (!service) {
            QWARN() << "browse item" << item.value(QStringLiteral("title")).toString()
                    << "failed: SMAPI serviceId=" << itemServiceId << "not resolved";
            callback(false, tr("Music service is not available."), {});
            return;
        }

        QString smapiObjectId = item.value(QStringLiteral("didlId")).toString();
        if (smapiObjectId.isEmpty())
            smapiObjectId = item.value(QStringLiteral("browseId")).toString();
        if (smapiObjectId.isEmpty())
            smapiObjectId = item.value(QStringLiteral("id")).toString();

        QLOG() << "browse item" << item.value(QStringLiteral("title")).toString()
               << "redirecting to service" << service->title() << "objectId" << smapiObjectId;
        service->browseDirect(smapiObjectId, std::move(callback));
        return;
    }

    MusicService::doBrowseItem(item, std::move(callback));
}

void SonosLibraryService::doBrowse(const QString &objectId, ResultCallback callback)
{
    if (objectId == QStringLiteral("root")) {
        QLOG() << "browse root -- returning synthetic category list";
        callback(true, QString(),
                 {
                     // Ordinary UPnP ContentDirectory object IDs, not SMAPI
                     // concepts -- ALBUMARTIST (not ARTIST; Sonos'
                     // ContentDirectory has no "A:ARTIST" container) is the
                     // "Artists" grouping, confirmed against roomtunes-bb10's
                     // SonosLibrary.cpp.
                     categoryItem(QStringLiteral("A:ALBUMARTIST"), tr("Artists")),
                     categoryItem(QStringLiteral("A:ALBUM"), tr("Albums")),
                     categoryItem(QStringLiteral("A:GENRE"), tr("Genres")),
                     categoryItem(QStringLiteral("A:TRACKS"), tr("Tracks")),
                     categoryItem(QStringLiteral("A:PLAYLISTS"), tr("Playlists")),
                 });
        return;
    }

    ZonePlayer *zone = m_household->browseCoordinator();
    if (!zone) {
        QWARN() << "browse" << objectId << "failed: no ready coordinator available";
        callback(false, tr("No ready Sonos coordinator available to browse with."), {});
        return;
    }

    QLOG() << "browse" << objectId << "via coordinator" << zone->roomName();

    const QString baseUrl = zone->baseUrl();
    zone->browse(objectId, [callback, objectId, baseUrl](bool ok, const QString &errorMessage, const QList<DidlItem> &items) {
        if (!ok) {
            // ZonePlayer::browse() already logged the full detail (room,
            // httpStatus, UPnP error code/description) against this exact
            // objectId -- errorMessage here is that same detail, passed
            // through to the QML-facing error text too instead of a canned
            // string.
            QWARN() << "browse" << objectId << "failed:" << errorMessage;
            callback(false, errorMessage, {});
            return;
        }

        QLOG() << "browse" << objectId << "OK," << items.size() << "item(s)";

        QVariantList result;
        result.reserve(items.size());
        for (const DidlItem &item : items)
            result.append(didlItemToVariant(item, baseUrl));

        callback(true, QString(), result);
    });
}

}
