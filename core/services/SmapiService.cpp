#include "SmapiService.h"

#include <QNetworkReply>
#include <QUrl>
#include <QXmlStreamReader>

#include "../Logging.h"
#include "../upnp/SoapResponse.h"
#include "../upnp/services/MusicServices.h"
#include "../zone/Household.h"
#include "../zone/ZonePlayer.h"

#define QLOG_CATEGORY logSmapi

namespace RoomTunes {

namespace {

QVariantMap itemToVariant(const QString &id, const QString &title, const QString &artist, const QString &album,
                           const QString &imageUrl, bool container, const QString &uri, const QString &upnpClass,
                           const QString &didlId, const QString &parentId, const QString &desc,
                           const QString &itemType)
{
    QVariantMap item;
    item[QStringLiteral("id")] = id;
    item[QStringLiteral("title")] = title;
    item[QStringLiteral("artist")] = artist;
    item[QStringLiteral("album")] = album;
    item[QStringLiteral("imageUrl")] = imageUrl;
    item[QStringLiteral("container")] = container;
    item[QStringLiteral("uri")] = uri;
    item[QStringLiteral("upnpClass")] = upnpClass;
    item[QStringLiteral("didlId")] = didlId;
    item[QStringLiteral("parentId")] = parentId;
    item[QStringLiteral("desc")] = desc;
    // SMAPI's own vocabulary (track/album/albumList/artist/playlist/
    // program/stream/container/other) -- kept alongside upnpClass since
    // lookupUpnpClass() above collapses several of these (artist/
    // container/other) into the same generic musicAlbum class, too lossy
    // on its own for QML to tell "an actual album/artist" apart from a
    // generic section like a service's "Top 10s"/"New Releases" folder.
    item[QStringLiteral("itemType")] = itemType;
    return item;
}

// SMAPI has no DIDL of its own -- a playable URI/upnpClass has to be
// synthesized from itemType/mimeType instead. Ported from roomtunes-bb10's
// SmapiParser.cpp (lookupUpnpClass/lookupResourceType/enqueued_uri/
// SonosTrack::enqueued_id) -- the legacy Rhapsody/Napster/SiriusXM special
// cases aren't ported since those services have no working SMAPI endpoint
// any more (see Household::logServiceMap()'s "legacy" list) and can never
// actually reach this code as a live SmapiService instance.

QString lookupUpnpClass(const QString &itemType, const QString &mimeType)
{
    if (itemType == QStringLiteral("track")) {
        QString upnpClass = QStringLiteral("object.item.audioItem.musicTrack");
        if (mimeType == QStringLiteral("audio/vnd.radiotime"))
            upnpClass += QStringLiteral(".recentShow");
        return upnpClass;
    }
    if (itemType == QStringLiteral("playlist"))
        return QStringLiteral("object.container.playlistContainer");
    if (itemType == QStringLiteral("program") || itemType == QStringLiteral("stream"))
        return QStringLiteral("object.item.audioItem.audioBroadcast");
    // container/album/albumList/artist/other: all the same musicAlbum
    // container class bb10 used as its catch-all.
    return QStringLiteral("object.container.album.musicAlbum");
}

// napster/rhapsody used "npsdy", siriusXM used "sirradio"/"x-sonosapi-hls"
// -- not reachable here, see the comment above.
QString lookupResourceType(const QString &itemType, const QString &mimeType)
{
    if (itemType == QStringLiteral("track")) {
        if (mimeType == QStringLiteral("audio/x-spotify"))
            return QStringLiteral("x-sonos-spotify");
        if (mimeType == QStringLiteral("audio/vnd.radiotime"))
            return QStringLiteral("x-sonosapi-rtrecent");
        return QStringLiteral("x-sonos-http");
    }
    if (itemType == QStringLiteral("program"))
        return QStringLiteral("x-sonosapi-radio");
    if (itemType == QStringLiteral("stream"))
        return QStringLiteral("x-sonosapi-stream");
    return QStringLiteral("x-rincon-cpcontainer");
}

QString enqueuedUri(const QString &resourceType, const QString &itemId, int smapiId)
{
    QString uri = resourceType + QStringLiteral(":");
    if (resourceType == QStringLiteral("x-rincon-cpcontainer"))
        uri += QStringLiteral("10030000");
    uri += QString::fromUtf8(QUrl::toPercentEncoding(itemId));
    if (resourceType != QStringLiteral("x-rincon-cpcontainer"))
        uri += QStringLiteral("?sid=%1&flags=0").arg(smapiId);
    return uri;
}

// The DIDL <item id="..."> SMAPI expects on enqueue/play -- never the
// service's own raw item id (that only applies to a *library* item parsed
// directly from real DIDL; see SonosLibraryService.cpp).
QString enqueuedId(const QString &itemId, const QString &resourceType)
{
    QString flags = QStringLiteral("10030000");
    if (resourceType == QStringLiteral("x-sonosapi-rtrecent"))
        flags.prepend(QLatin1Char('F'));
    return flags + QString::fromUtf8(QUrl::toPercentEncoding(itemId));
}

// One <mediaCollection>/<mediaMetadata> element -- ported from
// ServiceBrowser's parseOneItem (formerly SmapiService::parseOneMediaItem,
// which produced a parented MediaItem* instead; a QVariantMap owned by the
// QML list that displays it fits this call site better). smapiId/serviceId/
// username are the enclosing service's own identity, needed to synthesize
// this item's playable uri/didlId/desc (see the lookup helpers above and
// roomtunes-bb10's SonosTrack::enqueued_id()/didl_desc()).
QVariantMap parseOneItem(QXmlStreamReader &xml, bool isCollection, int smapiId, int serviceId, const QString &username)
{
    QString id, itemType, mimeType, title, artist, album, albumArtUri;

    while (xml.readNextStartElement()) {
        const QString name = xml.name().toString();
        if (name == QStringLiteral("id"))
            id = xml.readElementText(QXmlStreamReader::SkipChildElements);
        else if (name == QStringLiteral("itemType"))
            itemType = xml.readElementText(QXmlStreamReader::SkipChildElements);
        else if (name == QStringLiteral("mimeType"))
            mimeType = xml.readElementText(QXmlStreamReader::SkipChildElements);
        else if (name == QStringLiteral("title"))
            title = xml.readElementText(QXmlStreamReader::SkipChildElements);
        else if (name == QStringLiteral("albumArtURI"))
            albumArtUri = xml.readElementText(QXmlStreamReader::SkipChildElements);
        else if (name == QStringLiteral("trackMetadata")) {
            while (xml.readNextStartElement()) {
                const QString field = xml.name().toString();
                if (field == QStringLiteral("artist"))
                    artist = xml.readElementText(QXmlStreamReader::SkipChildElements);
                else if (field == QStringLiteral("album"))
                    album = xml.readElementText(QXmlStreamReader::SkipChildElements);
                else if (field == QStringLiteral("albumArtURI"))
                    albumArtUri = xml.readElementText(QXmlStreamReader::SkipChildElements);
                else
                    xml.skipCurrentElement();
            }
        } else {
            xml.skipCurrentElement();
        }
    }

    const bool container = isCollection || itemType == QStringLiteral("playlist") || itemType == QStringLiteral("album")
        || itemType == QStringLiteral("artist") || itemType == QStringLiteral("container");

    const QString resourceType = lookupResourceType(itemType, mimeType);
    const QString uri = enqueuedUri(resourceType, id, smapiId);
    const QString upnpClass = lookupUpnpClass(itemType, mimeType);
    // A <mediaMetadata> (playable) item's parentID is always the literal
    // "-1" per SMAPI's own enqueue convention -- only a <mediaCollection>
    // container keeps an empty one (roomtunes-bb10's ParseMediaMetadata/
    // ParseMediaCollection).
    const QString parentId = isCollection ? QString() : QStringLiteral("-1");
    const QString desc = QStringLiteral("SA_RINCON%1_%2").arg(serviceId).arg(username);

    return itemToVariant(id, title, artist, album, albumArtUri, container, uri, upnpClass, enqueuedId(id, resourceType),
                          parentId, desc, itemType);
}

QVariantList parseMetadataBody(const QByteArray &body, int smapiId, int serviceId, const QString &username)
{
    QVariantList items;
    QXmlStreamReader xml(body);
    while (!xml.atEnd()) {
        if (!xml.readNextStartElement())
            continue;
        if (xml.name() == QLatin1String("mediaCollection"))
            items.append(parseOneItem(xml, true, smapiId, serviceId, username));
        else if (xml.name() == QLatin1String("mediaMetadata"))
            items.append(parseOneItem(xml, false, smapiId, serviceId, username));
    }
    return items;
}

}

SmapiService::SmapiService(Household *household, int serviceId, int smapiId, const QString &serviceUri,
                            const QString &authPolicy, const QString &username, const QString &token,
                            const QString &key, const QString &title, const QString &iconUrl, QObject *parent)
    : MusicService(QStringLiteral("smapi:%1").arg(serviceId), title, iconUrl, parent)
    , m_household(household)
    , m_serviceId(serviceId)
    , m_smapiId(smapiId)
    , m_serviceUri(serviceUri)
    , m_authPolicy(authPolicy)
    , m_username(username)
    , m_token(token)
    , m_key(key)
    , m_tpmsxToken(token)
    , m_tpmsxKey(key)
    , m_smapi(household->networkAccessManager(), serviceUri)
{
}

bool SmapiService::needsSignIn() const
{
    return (m_authPolicy == QStringLiteral("DeviceLink") || m_authPolicy == QStringLiteral("AppLink"))
        && (m_token.isEmpty() || m_key.isEmpty());
}

void SmapiService::updateResolved(int smapiId, const QString &serviceUri, const QString &authPolicy,
                                   const QString &username, const QString &token, const QString &key,
                                   const QString &title, const QString &iconUrl)
{
    const bool wasNeedsSignIn = needsSignIn();

    m_smapiId = smapiId;
    if (m_serviceUri != serviceUri) {
        m_serviceUri = serviceUri;
        m_smapi.bindService(serviceUri);
    }
    m_authPolicy = authPolicy;
    m_username = username;

    // Only apply Household's token/key if TPMSX's own snapshot actually
    // changed since we last saw it (a genuine relink) -- a routine rebuild
    // (catalog/icon fetch completing, a network blip) re-passes the exact
    // same stale snapshot every time, which would otherwise silently
    // clobber a token this instance has since refreshed itself via
    // Client.TokenRefreshRequired (see runMetadataRequest()); Sonos' own
    // TPMSX blob has no idea that refresh happened.
    if (token != m_tpmsxToken || key != m_tpmsxKey) {
        m_tpmsxToken = token;
        m_tpmsxKey = key;
        m_token = token;
        m_key = key;
    }

    setTitle(title);
    setIconSource(iconUrl);

    if (wasNeedsSignIn != needsSignIn())
        emit needsSignInChanged();
}

void SmapiService::doBrowse(const QString &objectId, ResultCallback callback)
{
    const QString description = QStringLiteral("browse %1").arg(objectId);
    QLOG() << title() << description;

    withCredentials(description, callback, [this, objectId, callback, description]() {
        auto reissue = [this, objectId]() { return m_smapi.getMetadata(objectId, 0, 100); };
        runMetadataRequest(reissue(), description, callback, reissue);
    });
}

void SmapiService::doSearch(const QString &category, const QString &term, ResultCallback callback)
{
    const QString description = QStringLiteral("search category=%1 term=%2").arg(category, term);
    QLOG() << title() << description;

    withCredentials(description, callback, [this, category, term, callback, description]() {
        resolveSearchCategory(category, callback, [this, term, callback, description](const QString &categoryId) {
            auto reissue = [this, categoryId, term]() { return m_smapi.search(categoryId, term, 0, 100); };
            runMetadataRequest(reissue(), description, callback, reissue);
        });
    });
}

void SmapiService::resolveSearchCategory(const QString &hint, ResultCallback callback,
                                          std::function<void(const QString &)> onResolved)
{
    if (!m_searchCategories.isEmpty()) {
        applyResolvedSearchCategory(pickSearchCategoryId(hint), onResolved);
        return;
    }

    // "search" is a reserved getMetadata id every SMAPI service recognizes
    // specifically to mean "list your own search categories" -- see
    // roomtunes-bb10's SmapiService::initSearchTerms(). The response is
    // the same <mediaCollection> shape as an ordinary browse result, just
    // describing category choices instead of playable content, so the
    // existing parser handles it as-is.
    QNetworkReply *reply = m_smapi.getMetadata(QStringLiteral("search"), 0, 10);
    connect(reply, &QNetworkReply::finished, this, [this, reply, hint, callback, onResolved]() {
        SoapResponse response(reply);
        reply->deleteLater();

        if (response.error()) {
            QWARN() << title() << "fetching search categories failed:" << response.faultCode() << response.faultString();
            callback(false, tr("This service doesn't support search."), {});
            return;
        }

        m_searchCategories = parseMetadataBody(response.rawBody(), m_smapiId, m_serviceId, m_username);
        if (m_searchCategories.isEmpty()) {
            QWARN() << title() << "fetching search categories returned none";
            callback(false, tr("This service doesn't support search."), {});
            return;
        }

        QLOG() << title() << "search categories:" << m_searchCategories.size();
        emit searchCategoriesChanged();
        applyResolvedSearchCategory(pickSearchCategoryId(hint), onResolved);
    });
}

void SmapiService::applyResolvedSearchCategory(const QString &categoryId, std::function<void(const QString &)> onResolved)
{
    if (m_activeSearchCategoryId != categoryId) {
        m_activeSearchCategoryId = categoryId;
        emit activeSearchCategoryChanged();
    }
    onResolved(categoryId);
}

QString SmapiService::pickSearchCategoryId(const QString &hint) const
{
    for (const QVariant &v : m_searchCategories) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("id")).toString().contains(hint, Qt::CaseInsensitive)
            || m.value(QStringLiteral("title")).toString().contains(hint, Qt::CaseInsensitive))
            return m.value(QStringLiteral("id")).toString();
    }

    // No category matched the caller's hint -- fall back to whatever this
    // service listed first rather than failing outright.
    return m_searchCategories.first().toMap().value(QStringLiteral("id")).toString();
}

void SmapiService::withCredentials(const QString &requestDescription, ResultCallback callback, std::function<void()> onReady)
{
    if (m_serviceUri.isEmpty()) {
        QWARN() << title() << requestDescription << "failed: not in the current SMAPI catalog, no serviceUri to browse";
        callback(false, tr("This service can't be browsed yet."), {});
        return;
    }

    // "AppLink" is the SMAPI catalog's newer name for the same sign-in
    // relationship as "DeviceLink" (confirmed against a real household:
    // Spotify/YouTube Music/BBC Sounds/TuneIn all report "AppLink").
    if (m_authPolicy == QStringLiteral("DeviceLink") || m_authPolicy == QStringLiteral("AppLink")) {
        if (m_token.isEmpty() || m_key.isEmpty()) {
            QWARN() << title() << requestDescription << "failed: sign-in required (no stored DeviceLink token)";
            callback(false, tr("Sign-in required for this service."), {});
            return;
        }

        m_smapi.setLoginTokenCredentials(m_household->serviceDeviceSerial(), QStringLiteral("Sonos"), m_token, m_key,
                                          m_household->householdId());
        onReady();
        return;
    }

    if (m_authPolicy == QStringLiteral("UserId")) {
        ZonePlayer *zone = m_household->topologyZone();
        if (!zone) {
            QWARN() << title() << requestDescription << "failed: no topology zone available to fetch a sessionId with";
            callback(false, tr("No Sonos zone available to sign in with."), {});
            return;
        }

        QLOG() << title() << requestDescription << "fetching sessionId via" << zone->roomName();

        // The speaker itself already holds this service's password (from
        // ThirdPartyMediaServersX) and exchanges it for a sessionId on our
        // behalf -- no raw password ever passes through RoomTunes.
        QNetworkReply *reply = zone->musicServices().GetSessionId(m_smapiId, m_username);
        connect(reply, &QNetworkReply::finished, this, [this, reply, requestDescription, callback, onReady]() {
            SoapResponse response(reply);
            reply->deleteLater();

            if (response.error()) {
                QWARN() << title() << requestDescription << "GetSessionId failed:" << response.faultCode()
                        << response.faultString();
                callback(false, response.faultString(), {});
                return;
            }

            const QString sessionId = response.value(QStringLiteral("SessionId"));
            m_smapi.setSessionIdCredentials(m_household->serviceDeviceSerial(), QStringLiteral("Sonos"), sessionId);
            onReady();
        });
        return;
    }

    // Anonymous/Stateless (and any unrecognized policy, since there's no
    // credential step we could perform for it anyway): proceed directly.
    onReady();
}

void SmapiService::runMetadataRequest(QNetworkReply *reply, const QString &requestDescription, ResultCallback callback,
                                       std::function<QNetworkReply *()> reissue, bool isRetry)
{
    connect(reply, &QNetworkReply::finished, this, [this, reply, requestDescription, callback, reissue, isRetry]() {
        SoapResponse response(reply);
        reply->deleteLater();

        if (response.error()) {
            // The SMAPI server hands back a fresh token/key right in this
            // fault's detail instead of just rejecting the call -- update
            // credentials and retry once (roomtunes-bb10's SmapiAuth::
            // handleAuthRefresh() did the same update but never retried,
            // so the triggering call just silently failed there; retrying
            // here means the caller never sees this one at all).
            if (!isRetry && response.faultCode() == QStringLiteral("Client.TokenRefreshRequired")
                && !response.refreshedAuthToken().isEmpty() && !response.refreshedPrivateKey().isEmpty()) {
                QLOG() << title() << requestDescription << "token refresh required -- updating credentials and retrying";
                // Deliberately leaves m_tpmsxToken/m_tpmsxKey (Household's
                // last-seen TPMSX snapshot) untouched -- see their
                // declaration in SmapiService.h. Only m_token/m_key (the
                // credential actually in use) changes here.
                m_token = response.refreshedAuthToken();
                m_key = response.refreshedPrivateKey();
                m_smapi.setLoginTokenCredentials(m_household->serviceDeviceSerial(), QStringLiteral("Sonos"), m_token,
                                                  m_key, m_household->householdId());
                runMetadataRequest(reissue(), requestDescription, callback, reissue, /*isRetry=*/true);
                return;
            }

            QWARN() << title() << requestDescription << "failed:" << response.faultCode() << response.faultString();
            callback(false, response.faultString(), {});
            return;
        }

        const QVariantList items = parseMetadataBody(response.rawBody(), m_smapiId, m_serviceId, m_username);
        QLOG() << title() << requestDescription << "OK," << items.size() << "item(s)";
        callback(true, QString(), items);
    });
}

void SmapiService::beginSignIn()
{
    requestDeviceLinkCode(m_household->householdId(), [this](bool ok, const QString &linkCode, const QString &regUrl) {
        if (ok)
            emit deviceLinkCodeReady(linkCode, regUrl);
    });
}

void SmapiService::completeSignIn(const QString &linkCode)
{
    exchangeDeviceLinkCode(m_household->householdId(), linkCode, [this](bool ok, const QString &token, const QString &key) {
        if (!ok)
            return;

        m_token = token;
        m_key = key;
        setDeviceLinkToken(m_household->serviceDeviceSerial(), token, key, m_household->householdId());
        emit needsSignInChanged();
    });
}

void SmapiService::requestDeviceLinkCode(const QString &householdId,
                                          std::function<void(bool, const QString &, const QString &)> callback)
{
    QNetworkReply *reply = m_smapi.getDeviceLinkCode(householdId);
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        SoapResponse response(reply);
        reply->deleteLater();

        if (response.error()) {
            QWARN() << title() << "getDeviceLinkCode failed:" << response.faultString();
            emit authorizationFailed(response.faultString());
            if (callback)
                callback(false, QString(), QString());
            return;
        }

        if (callback)
            callback(true, response.value(QStringLiteral("LinkCode")), response.value(QStringLiteral("RegUrl")));
    });
}

void SmapiService::exchangeDeviceLinkCode(const QString &householdId, const QString &linkCode,
                                           std::function<void(bool, const QString &, const QString &)> callback)
{
    QNetworkReply *reply = m_smapi.getDeviceAuthToken(householdId, linkCode);
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        SoapResponse response(reply);
        reply->deleteLater();

        if (response.error()) {
            QWARN() << title() << "getDeviceAuthToken failed:" << response.faultString();
            emit authorizationFailed(response.faultString());
            if (callback)
                callback(false, QString(), QString());
            return;
        }

        if (callback)
            callback(true, response.value(QStringLiteral("AuthToken")), response.value(QStringLiteral("Key")));
    });
}

void SmapiService::setDeviceLinkToken(const QString &deviceId, const QString &token, const QString &key, const QString &householdId)
{
    m_smapi.setLoginTokenCredentials(deviceId, QStringLiteral("Sonos"), token, key, householdId);
    emit authorized();
}

}
