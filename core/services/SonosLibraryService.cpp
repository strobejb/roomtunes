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
    // Playable fields for ZonePlayer::playItem() -- the DIDL <res> URI is
    // reused as-is (roomtunes-bb10's SonosTrack::didl_item() rebuilds the
    // metadata but always replays the original href verbatim for library
    // items); didlId/parentId are the item's own real DIDL id/parentID
    // (unlike a SMAPI item's, which get rewritten -- see SmapiService.cpp),
    // and desc is left unset so playItem() falls back to Didl::buildItem()'s
    // own default ("RINCON_AssociatedZPUDN").
    variant[QStringLiteral("uri")] = item.res;
    variant[QStringLiteral("upnpClass")] = item.upnpClass;
    variant[QStringLiteral("didlId")] = item.id;
    variant[QStringLiteral("parentId")] = item.parentId;
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

    ZonePlayer *zone = m_household->topologyZone();
    if (!zone) {
        QWARN() << "browse" << objectId << "failed: no topology zone available";
        callback(false, tr("No Sonos zone available to browse with."), {});
        return;
    }

    QLOG() << "browse" << objectId << "via zone" << zone->roomName();

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
