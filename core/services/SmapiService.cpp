#include "SmapiService.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSettings>
#include <QStringList>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>
#include <QUuid>

#include <cstdlib>
#include <memory>
#include <string_view>
#include <utility>

#include "../Logging.h"
#include "../control/SoapResponse.h"
#include "../control/services/MusicServices.h"
#include "../settings/Settings.h"
#include "../xml/XmlUtils.h"
#include "../zone/Household.h"
#include "../zone/ZonePlayer.h"

#define QLOG_CATEGORY logSmapi

namespace RoomTunes
{

namespace
{

constexpr quint32 kContextCapability             = 1u << 16;
// SMAPI Service/@Capabilities bit 0 advertises provider-side search support.
// Services without it can still browse, but getMetadata("search") normally
// faults or returns no categories, so exclude them from search UI upfront.
constexpr quint32 kSearchCapability              = 1u << 0;
constexpr int     kDeviceAuthTokenPollIntervalMs = 3000;
constexpr int     kDeviceAuthTokenPollTimeoutMs  = 7 * 60 * 1000;

struct DeviceLinkDetails
{
    QString registrationUrl;
    QString linkCode;
    QString linkDeviceId;
    bool    showLinkCode = true;
};

struct DeviceAuthTokenDetails
{
    QString token;
    QString key;
};

DeviceLinkDetails parseAppLinkDeviceLink(const QByteArray &body)
{
    DeviceLinkDetails details;
    const XmlDoc      doc        = XmlDoc::parse(body, XmlOptions{Qt::CaseInsensitive});
    const XmlNode     deviceLink = doc.first("//deviceLink");

    if (deviceLink)
    {
        details.registrationUrl = deviceLink.text("regUrl");
        details.linkCode        = deviceLink.text("linkCode");
        details.linkDeviceId    = deviceLink.text("linkDeviceId");
        details.showLinkCode    = deviceLink.textBool("showLinkCode", details.showLinkCode);
    }
    return details;
}

DeviceAuthTokenDetails parseDeviceAuthTokenResponse(const SoapResponse &response)
{
    DeviceAuthTokenDetails details{response.firstValue({"AuthToken", "Token"}),
                                   response.firstValue({"Key", "PrivateKey"})};

    if (!details.token.isEmpty() && !details.key.isEmpty())
        return details;

    // Some AppLink providers wrap the actual token/key one level below
    // getDeviceAuthTokenResponse. SoapResponse::value() deliberately only
    // flattens immediate response children, so scan this one specialised
    // response shape here instead of changing generic SOAP parsing.
    const XmlDoc doc = XmlDoc::parse(response.rawBody(), XmlOptions{Qt::CaseInsensitive});
    if (details.token.isEmpty())
        details.token = doc.firstText({"AuthToken", "token"});
    if (details.key.isEmpty())
        details.key = doc.firstText({"Key", "PrivateKey"});

    return details;
}

QString redactedXmlForLog(QString xml)
{
    xml.replace(
        QRegularExpression(
            QStringLiteral(
                "<((?:[A-Za-z_][\\w.-]*:)?(?:authToken|privateKey|sessionId|password|token|key))([^>]*)>.*?</\\1>"),
            QRegularExpression::CaseInsensitiveOption),
        QStringLiteral("<\\1\\2><redacted></\\1>"));
    xml.replace(QLatin1Char('\r'), QLatin1Char(' '));
    xml.replace(QLatin1Char('\n'), QLatin1Char(' '));
    xml.replace(QLatin1Char('\t'), QLatin1Char(' '));
    xml = xml.simplified();
    return xml;
}

QString compactXmlForLog(QString xml)
{
    xml = redactedXmlForLog(std::move(xml));

    constexpr qsizetype maxLength = 4000;
    if (xml.size() > maxLength)
        xml = xml.left(maxLength - 3) + QStringLiteral("...");
    return xml;
}

bool hasNilMetadataResponse(const QByteArray &body)
{
    const XmlDoc doc = XmlDoc::parse(body, XmlOptions{Qt::CaseInsensitive});
    for (const XmlNode &node : doc.all("//*"))
    {
        if (!node.nameIn({"getMetadataResponse", "searchResponse"}))
            continue;

        return node.attr("nil") == QStringLiteral("true");
    }
    return false;
}

bool hasSoapBodyPayload(const QByteArray &body)
{
    const XmlDoc  doc      = XmlDoc::parse(body, XmlOptions{Qt::CaseInsensitive});
    const XmlNode bodyNode = doc.first("//Body");
    return bodyNode && !bodyNode.children().isEmpty();
}

bool looksLikeAuthExpiredError(const QString &errorMessage)
{
    // Sonos services are not consistent about how they report an expired
    // account. Some return a direct "token expired" fault string, others
    // surface a more user-facing "account access has expired" message.
    // Treat the common variants as the same browser reauthorization case.
    const QString lower = errorMessage.toLower();
    return lower.contains(QStringLiteral("account access has expired")) ||
           lower.contains(QStringLiteral("token expired")) || lower.contains(QStringLiteral("unauthoriz")) ||
           (lower.contains(QStringLiteral("expired")) &&
            (lower.contains(QStringLiteral("token")) || lower.contains(QStringLiteral("account")) ||
             lower.contains(QStringLiteral("access")))) ||
           (lower.contains(QStringLiteral("auth")) && lower.contains(QStringLiteral("expired")));
}

bool looksLikeSearchUnsupportedProbeResult(const QString &errorMessage)
{
    const QString lower = errorMessage.toLower();
    return lower.contains(QStringLiteral("no browse data")) || lower.contains(QStringLiteral("not found")) ||
           lower.contains(QStringLiteral("unsupported")) || lower.contains(QStringLiteral("not supported"));
}

bool isRetryableDeviceAuthTokenError(const SoapResponse &response)
{
    const QString faultCode = response.faultCode();
    return faultCode == QStringLiteral("Client.NOT_LINKED_RETRY") ||
           faultCode == QStringLiteral("Client.AuthTokenExpired");
}

QString responseHeadersForLog(const QNetworkReply *reply)
{
    if (!reply)
        return {};

    QStringList   headers;
    const int     httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString httpReason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
    if (httpStatus > 0)
        headers << QStringLiteral("HTTP %1%2")
                       .arg(httpStatus)
                       .arg(httpReason.isEmpty() ? QString() : QStringLiteral(" ") + httpReason);

    for (const auto &pair : reply->rawHeaderPairs())
    {
        const QString name  = QString::fromLatin1(pair.first);
        QString       value = QString::fromLatin1(pair.second);
        if (name.compare(QStringLiteral("set-cookie"), Qt::CaseInsensitive) == 0 ||
            name.compare(QStringLiteral("authorization"), Qt::CaseInsensitive) == 0)
            value = QStringLiteral("<redacted>");
        headers << QStringLiteral("%1: %2").arg(name, value);
    }

    return headers.join(QStringLiteral("; "));
}

QString replyUrlForLog(const QNetworkReply *reply)
{
    return reply ? reply->url().toString() : QStringLiteral("<unknown>");
}

QString manifestBrowseEndpoint(const QByteArray &body)
{
    const QJsonArray endpoints = QJsonDocument::fromJson(body).object().value(QStringLiteral("endpoints")).toArray();
    for (const QJsonValue &value : endpoints)
    {
        const QJsonObject endpoint = value.toObject();
        if (endpoint.value(QStringLiteral("type")).toString() == QStringLiteral("browse"))
            return endpoint.value(QStringLiteral("uri")).toString();
    }
    return {};
}

QString sonosControllerId()
{
    // Manifest browse endpoints expect a controller identity which remains
    // stable across requests. It identifies this RoomTunes installation,
    // not a Sonos account, so generate and persist our own UUID.
    QSettings      settings(applicationSettingsFilePath(), QSettings::IniFormat);
    constexpr auto key = "network/sonosControllerId";
    QString        id  = settings.value(QLatin1String(key)).toString();
    if (id.isEmpty())
    {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        settings.setValue(QLatin1String(key), id);
    }
    return id;
}

QString deviceLinkCredentialGroup(int serviceId, const QString &householdId, const QString &deviceId)
{
    return QStringLiteral("credentials/%1/%2/%3")
        .arg(serviceId)
        .arg(QString::fromLatin1(QUrl::toPercentEncoding(householdId)))
        .arg(QString::fromLatin1(QUrl::toPercentEncoding(deviceId)));
}

QString localUtcOffset()
{
    const int   offsetSeconds = QDateTime::currentDateTime().offsetFromUtc();
    const QChar sign          = offsetSeconds < 0 ? QLatin1Char('-') : QLatin1Char('+');
    const int   offsetMinutes = std::abs(offsetSeconds) / 60;
    return QStringLiteral("%1%2:%3")
        .arg(sign)
        .arg(offsetMinutes / 60, 2, 10, QLatin1Char('0'))
        .arg(offsetMinutes % 60, 2, 10, QLatin1Char('0'));
}

QString jsonString(const QJsonObject &object, const QString &name)
{
    const QJsonValue value = object.value(name);
    if (value.isString())
        return value.toString();
    if (value.isObject())
        return value.toObject().value(QStringLiteral("name")).toString();
    return {};
}

QVariantMap itemToVariant(const QString &id, const QString &title, const QString &artist, const QString &album,
                          const QString &imageUrl, bool container, const QString &uri, const QString &upnpClass,
                          const QString &didlId, const QString &parentId, const QString &desc, const QString &itemType)
{
    QVariantMap item;
    item[QStringLiteral("id")]        = id;
    item[QStringLiteral("title")]     = title;
    item[QStringLiteral("artist")]    = artist;
    item[QStringLiteral("album")]     = album;
    item[QStringLiteral("imageUrl")]  = imageUrl;
    item[QStringLiteral("container")] = container;
    // QML uses browseId for folder descent so ContentDirectory favourites
    // can keep a distinct visible id and real browse target. SMAPI items do
    // not have that wrapper split, so their browse target is simply id.
    item[QStringLiteral("browseId")]  = id;
    item[QStringLiteral("uri")]       = uri;
    item[QStringLiteral("upnpClass")] = upnpClass;
    item[QStringLiteral("didlId")]    = didlId;
    item[QStringLiteral("parentId")]  = parentId;
    item[QStringLiteral("desc")]      = desc;
    // SMAPI's own vocabulary (track/album/albumList/artist/playlist/
    // program/stream/container/other) -- kept alongside upnpClass since
    // lookupUpnpClass() above collapses several of these (artist/
    // container/other) into the same generic musicAlbum class, too lossy
    // on its own for QML to tell "an actual album/artist" apart from a
    // generic section like a service's "Top 10s"/"New Releases" folder.
    item[QStringLiteral("itemType")]  = itemType;
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
    if (itemType == QStringLiteral("track"))
    {
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
    if (itemType == QStringLiteral("track"))
    {
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
QVariantMap parseOneItem(const XmlNode &node, bool isCollection, int smapiId, int serviceId, const QString &username)
{
    const QString id          = node.text("id");
    const QString itemType    = node.text("itemType");
    const QString mimeType    = node.text("mimeType");
    const QString title       = node.text("title");
    QString       artist      = node.text("artist");
    QString       album       = node.text("album");
    QString       albumArtUri = node.text("albumArtURI");
    const QString canPlay     = node.text("canPlay");
    const QString onDemand    = node.text("onDemand");

    const XmlNode trackMetadata = node.child("trackMetadata");
    if (trackMetadata)
    {
        const QString trackArtist = trackMetadata.text("artist");
        const QString trackAlbum  = trackMetadata.text("album");
        const QString trackArt    = trackMetadata.text("albumArtURI");
        if (!trackArtist.isEmpty())
            artist = trackArtist;
        if (!trackAlbum.isEmpty())
            album = trackAlbum;
        if (!trackArt.isEmpty())
            albumArtUri = trackArt;
    }

    const XmlNode streamMetadata = node.child("streamMetadata");
    if (streamMetadata)
    {
        if (artist.isEmpty())
        {
            for (const XmlNode &child : streamMetadata.children())
            {
                if (child.nameIn({"currentHost", "description"}))
                {
                    artist = child.text();
                    break;
                }
            }
        }
        if (albumArtUri.isEmpty())
            albumArtUri = streamMetadata.text("logo");
    }

    const bool playable =
        canPlay == QStringLiteral("true") || onDemand == QStringLiteral("true") || itemType == QStringLiteral("stream");
    const bool container = itemType == QStringLiteral("program")
                               ? false
                               : isCollection || itemType == QStringLiteral("playlist") ||
                                     itemType == QStringLiteral("album") || itemType == QStringLiteral("artist") ||
                                     itemType == QStringLiteral("container");

    const QString resourceType = lookupResourceType(itemType, mimeType);
    const QString uri          = enqueuedUri(resourceType, id, smapiId);
    const QString upnpClass    = lookupUpnpClass(itemType, mimeType);
    // A <mediaMetadata> (playable) item's parentID is always the literal
    // "-1" per SMAPI's own enqueue convention -- only a <mediaCollection>
    // container keeps an empty one (roomtunes-bb10's ParseMediaMetadata/
    // ParseMediaCollection).
    const QString parentId     = container ? QString() : QStringLiteral("-1");
    const QString desc         = QStringLiteral("SA_RINCON%1_%2").arg(serviceId).arg(username);

    QVariantMap item                 = itemToVariant(id, title, artist, album, albumArtUri, container, uri, upnpClass,
                                                     enqueuedId(id, resourceType), parentId, desc, itemType);
    item[QStringLiteral("playable")] = playable;
    return item;
}

QVariantList parseManifestBrowseItems(const QByteArray &body, int smapiId, int serviceId, const QString &username)
{
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (!document.isObject())
        return {};

    QVariantList     items;
    const QJsonArray views = document.object().value(QStringLiteral("views")).toArray();
    for (const QJsonValue &value : views)
    {
        const QJsonObject view             = value.toObject();
        const QJsonObject idObject         = view.value(QStringLiteral("id")).toObject();
        const QString     id               = idObject.isEmpty() ? view.value(QStringLiteral("id")).toString()
                                                                : idObject.value(QStringLiteral("objectId")).toString();
        const QJsonObject content          = view.value(QStringLiteral("content")).toObject();
        const QJsonObject track            = content.value(QStringLiteral("track")).toObject();
        const QJsonObject containerDetails = content.value(QStringLiteral("container")).toObject();
        const QJsonObject details          = track.isEmpty() ? containerDetails : track;
        if (id.isEmpty() || details.isEmpty())
            continue;

        QString           itemType  = details.value(QStringLiteral("type")).toString();
        const QJsonObject policies  = view.value(QStringLiteral("browsePolicies")).toObject();
        const bool        container = view.value(QStringLiteral("total")).toInt() > 0 ||
                               policies.value(QStringLiteral("canEnumerate")).toBool() ||
                               (!containerDetails.isEmpty() && itemType == QStringLiteral("container"));
        if (itemType.isEmpty())
            itemType = container ? QStringLiteral("container") : QStringLiteral("track");

        const bool    playable     = policies.value(QStringLiteral("canPlay")).toBool();
        const QString title        = details.value(QStringLiteral("name")).toString();
        const QString artist       = jsonString(details, QStringLiteral("artist"));
        const QString imageUrl     = details.value(QStringLiteral("imageUrl")).toString();
        const QString resourceType = lookupResourceType(itemType, QString());
        const QString uri          = enqueuedUri(resourceType, id, smapiId);
        const QString parentId     = container ? QString() : QStringLiteral("-1");
        const QString desc         = QStringLiteral("SA_RINCON%1_%2").arg(serviceId).arg(username);

        QVariantMap item =
            itemToVariant(id, title, artist, QString(), imageUrl, container, uri, lookupUpnpClass(itemType, QString()),
                          enqueuedId(id, resourceType), parentId, desc, itemType);
        item[QStringLiteral("playable")] = playable;
        item[QStringLiteral("summary")]  = details.value(QStringLiteral("summary")).toString();
        items.append(item);
    }
    return items;
}

QVariantList parseMetadataXml(const QByteArray &body, int smapiId, int serviceId, const QString &username)
{
    QVariantList items;
    const XmlDoc doc = XmlDoc::parse(body, XmlOptions{Qt::CaseInsensitive});
    for (const XmlNode &node : doc.all("//*"))
    {
        if (node.nameIs("mediaCollection"))
            items.append(parseOneItem(node, true, smapiId, serviceId, username));
        else if (node.nameIs("mediaMetadata"))
            items.append(parseOneItem(node, false, smapiId, serviceId, username));
    }
    return items;
}

QVariantList parseMetadataBody(const SoapResponse &response, int smapiId, int serviceId, const QString &username)
{
    // BB10's pugixml parser selected "//mediaCollection" and
    // "//mediaMetadata", so it tolerated both literal SMAPI result children
    // and service-specific wrapper elements. Start with the full SOAP body,
    // then fall back to any escaped result text if a service returns that
    // older/stringified shape.
    QVariantList items = parseMetadataXml(response.rawBody(), smapiId, serviceId, username);
    if (!items.isEmpty())
        return items;

    for (const QString &resultTag : {QStringLiteral("getMetadataResult"), QStringLiteral("searchResult")})
    {
        const QString resultXml = response.value(resultTag);
        if (resultXml.contains(QStringLiteral("mediaCollection")) ||
            resultXml.contains(QStringLiteral("mediaMetadata")))
        {
            items = parseMetadataXml(resultXml.toUtf8(), smapiId, serviceId, username);
            if (!items.isEmpty())
                return items;
        }
    }

    return {};
}

} // namespace

SmapiService::SmapiService(Household *household, int serviceId, int smapiId, const QString &serviceUri,
                           const QString &authPolicy, const QString &username, const QString &token, const QString &key,
                           const QString &title, const QString &iconUrl, quint32 capabilities,
                           const QString &manifestUri, QObject *parent)
    : MusicService(QStringLiteral("smapi:%1").arg(serviceId), title, iconUrl, parent), m_household(household),
      m_serviceId(serviceId), m_smapiId(smapiId), m_serviceUri(serviceUri), m_capabilities(capabilities),
      m_authPolicy(authPolicy), m_username(username), m_token(token), m_key(key), m_manifestUri(manifestUri),
      m_tpmsxToken(token), m_tpmsxKey(key), m_smapi(household->networkAccessManager(), serviceUri)
{
    m_smapi.setContextEnabled(m_capabilities & kContextCapability);
    applyPersistedDeviceLinkToken();
}

bool SmapiService::needsSignIn() const
{
    return (m_authPolicy == QStringLiteral("DeviceLink") || m_authPolicy == QStringLiteral("AppLink")) &&
           (m_token.isEmpty() || m_key.isEmpty());
}

bool SmapiService::shouldOfferReauthorize(const QString &errorMessage) const
{
    if (m_authPolicy != QStringLiteral("DeviceLink") && m_authPolicy != QStringLiteral("AppLink"))
        return false;
    return looksLikeAuthExpiredError(errorMessage);
}

void SmapiService::updateResolved(int smapiId, const QString &serviceUri, const QString &authPolicy,
                                  const QString &username, const QString &token, const QString &key,
                                  const QString &title, const QString &iconUrl, quint32 capabilities,
                                  const QString &manifestUri)
{
    const bool wasNeedsSignIn   = needsSignIn();
    const bool wasSearchCapable = canSearch();

    m_smapiId = smapiId;
    if (m_serviceUri != serviceUri)
    {
        m_serviceUri = serviceUri;
        m_smapi.bindService(m_serviceUri);
    }
    if (m_capabilities != capabilities)
    {
        m_capabilities = capabilities;
        m_smapi.setContextEnabled(m_capabilities & kContextCapability);
    }
    if (m_manifestUri != manifestUri)
    {
        m_manifestUri = manifestUri;
        m_manifestBrowseEndpoint.clear();
        m_manifestEndpointState = ManifestEndpointState::Unresolved;
    }
    m_authPolicy = authPolicy;
    m_username   = username;

    // Only apply Household's token/key if TPMSX's own snapshot actually
    // changed since we last saw it (a genuine relink) -- a routine rebuild
    // (catalog/icon fetch completing, a network blip) re-passes the exact
    // same stale snapshot every time, which would otherwise silently
    // clobber a token this instance has since refreshed itself via
    // Client.TokenRefreshRequired (see runMetadataRequest()); Sonos' own
    // TPMSX blob has no idea that refresh happened.
    if (token != m_tpmsxToken || key != m_tpmsxKey)
    {
        m_tpmsxToken = token;
        m_tpmsxKey   = key;
        m_token      = token;
        m_key        = key;
    }
    applyPersistedDeviceLinkToken();

    setTitle(title);
    setIconSource(iconUrl);

    if (wasNeedsSignIn != needsSignIn())
        emit needsSignInChanged();
    if (wasSearchCapable != canSearch())
        emit canSearchChanged();
}

bool SmapiService::canSearch() const
{
    return (m_capabilities & kSearchCapability) && !m_searchUnsupported;
}

void SmapiService::doBrowse(const QString &objectId, ResultCallback callback)
{
    const QString description = QStringLiteral("browse %1").arg(objectId);
    QLOG() << title() << description;

    withCredentials(description, callback,
                    [this, objectId, callback, description]()
                    {
                        auto soapFallback = [this, objectId, callback, description]()
                        {
                            browseViaSoap(objectId, description, callback);
                        };
                        if (objectId == QStringLiteral("root") && !m_manifestUri.isEmpty())
                            browseRootViaManifest(callback, soapFallback);
                        else
                            soapFallback();
                    });
}

void SmapiService::browseViaSoap(const QString &objectId, const QString &requestDescription, ResultCallback callback)
{
    auto reissue = [this, objectId]()
    {
        return m_smapi.getMetadata(objectId, 0, 100);
    };
    runMetadataRequest(reissue(), requestDescription, callback, reissue);
}

void SmapiService::doSearch(const QString &category, const QString &term, ResultCallback callback)
{
    if (!canSearch())
    {
        callback(false, tr("This service doesn't support search."), {});
        return;
    }

    const QString description = QStringLiteral("search category=%1 term=%2").arg(category, term);
    QLOG() << title() << description;

    withCredentials(description, callback,
                    [this, category, term, callback, description]()
                    {
                        resolveSearchCategory(category, callback,
                                              [this, term, callback, description](const QString &categoryId)
                                              {
                                                  auto reissue = [this, categoryId, term]()
                                                  {
                                                      return m_smapi.search(categoryId, term, 0, 100);
                                                  };
                                                  runMetadataRequest(reissue(), description, callback, reissue);
                                              });
                    });
}

void SmapiService::resolveSearchCategory(const QString &hint, ResultCallback callback,
                                         std::function<void(const QString &)> onResolved)
{
    ensureSearchCategories(callback,
                           [this, hint, onResolved]()
                           {
                               applyResolvedSearchCategory(pickSearchCategoryId(hint), onResolved);
                           });
}

void SmapiService::ensureSearchCategories(ResultCallback callback, std::function<void()> onReady)
{
    if (!m_searchCategories.isEmpty())
    {
        onReady();
        return;
    }

    // "search" is a reserved getMetadata id every SMAPI service recognizes
    // specifically to mean "list your own search categories" -- see
    // roomtunes-bb10's SmapiService::initSearchTerms(). The response is
    // the same <mediaCollection> shape as an ordinary browse result, just
    // describing category choices instead of playable content, so the
    // existing parser handles it as-is.
    const QString description = QStringLiteral("fetch search categories");
    auto          reissue     = [this]()
    {
        return m_smapi.getMetadata(QStringLiteral("search"), 0, 10);
    };
    runMetadataRequest(
        reissue(), description,
        [this, callback, onReady](bool ok, const QString &error, const QVariantList &items)
        {
            if (!ok)
            {
                QWARN() << title() << "fetching search categories failed:" << error;
                if (looksLikeSearchUnsupportedProbeResult(error) && !m_searchUnsupported)
                {
                    m_searchUnsupported = true;
                    emit canSearchChanged();
                }
                callback(false, tr("This service doesn't support search."), {});
                return;
            }

            m_searchCategories = items;
            if (m_searchCategories.isEmpty())
            {
                QWARN() << title() << "fetching search categories returned none";
                if (!m_searchUnsupported)
                {
                    m_searchUnsupported = true;
                    emit canSearchChanged();
                }
                callback(false, tr("This service doesn't support search."), {});
                return;
            }

            QLOG() << title() << "search categories:" << m_searchCategories.size();
            emit searchCategoriesChanged();
            onReady();
        },
        reissue);
}

void SmapiService::doSearchPreview(const QString &term, int limit, ResultCallback callback)
{
    if (!canSearch())
    {
        callback(true, QString(), {});
        return;
    }

    const QString description = QStringLiteral("search preview term=%1").arg(term);
    QLOG() << title() << description;

    withCredentials(
        description, callback,
        [this, term, limit, callback, description]()
        {
            ensureSearchCategories(
                callback,
                [this, term, limit, callback, description]()
                {
                    auto categories = std::make_shared<QStringList>();
                    for (const QVariant &v : m_searchCategories)
                    {
                        const QString categoryId = v.toMap().value(QStringLiteral("id")).toString();
                        if (!categoryId.isEmpty())
                            categories->append(categoryId);
                    }

                    auto merged  = std::make_shared<QVariantList>();
                    auto index   = std::make_shared<int>(0);
                    auto runNext = std::make_shared<std::function<void()>>();
                    *runNext     = [this, term, limit, callback, description, categories, merged, index, runNext]()
                    {
                        if ((limit > 0 && merged->size() >= limit) || *index >= categories->size())
                        {
                            callback(true, QString(), limit > 0 ? merged->mid(0, limit) : *merged);
                            return;
                        }

                        const QString categoryId = categories->at((*index)++);
                        auto          reissue    = [this, categoryId, term]()
                        {
                            return m_smapi.search(categoryId, term, 0, 100);
                        };
                        runMetadataRequest(
                            reissue(), QStringLiteral("%1 category=%2").arg(description, categoryId),
                            [limit, callback, merged, runNext](bool ok, const QString &, const QVariantList &items)
                            {
                                if (ok)
                                {
                                    for (const QVariant &item : items)
                                    {
                                        merged->append(item);
                                        if (limit > 0 && merged->size() >= limit)
                                            break;
                                    }
                                }
                                (*runNext)();
                            },
                            reissue);
                    };
                    (*runNext)();
                });
        });
}

void SmapiService::applyResolvedSearchCategory(const QString                       &categoryId,
                                               std::function<void(const QString &)> onResolved)
{
    if (m_activeSearchCategoryId != categoryId)
    {
        m_activeSearchCategoryId = categoryId;
        emit activeSearchCategoryChanged();
    }
    onResolved(categoryId);
}

QString SmapiService::pickSearchCategoryId(const QString &hint) const
{
    for (const QVariant &v : m_searchCategories)
    {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("id")).toString().contains(hint, Qt::CaseInsensitive) ||
            m.value(QStringLiteral("title")).toString().contains(hint, Qt::CaseInsensitive))
            return m.value(QStringLiteral("id")).toString();
    }

    // No category matched the caller's hint -- fall back to whatever this
    // service listed first rather than failing outright.
    return m_searchCategories.first().toMap().value(QStringLiteral("id")).toString();
}

void SmapiService::withCredentials(const QString &requestDescription, ResultCallback callback,
                                   std::function<void()> onReady)
{
    if (m_serviceUri.isEmpty())
    {
        QWARN() << title() << requestDescription << "failed: not in the current SMAPI catalog, no serviceUri to browse";
        callback(false, tr("This service can't be browsed yet."), {});
        return;
    }

    if (m_authPolicy == QStringLiteral("DeviceLink") || m_authPolicy == QStringLiteral("AppLink"))
    {
        if (m_household->serviceDeviceSerial().isEmpty())
        {
            QWARN() << title() << requestDescription << "failed: no SMAPI deviceId available yet";
            callback(false, tr("This service can't be browsed yet."), {});
            return;
        }
        applyPersistedDeviceLinkToken();
        if (m_token.isEmpty() || m_key.isEmpty())
        {
            QWARN() << title() << requestDescription << "failed: sign-in required (no stored DeviceLink token)";
            callback(false, tr("Sign-in required for this service."), {});
            return;
        }

        applyStoredCredentials();
        onReady();
        return;
    }

    if (m_authPolicy == QStringLiteral("UserId"))
    {
        if (m_household->serviceDeviceSerial().isEmpty())
        {
            QWARN() << title() << requestDescription << "failed: no SMAPI deviceId available yet";
            callback(false, tr("This service can't be browsed yet."), {});
            return;
        }

        ZonePlayer *zone = m_household->topologyZone();
        if (!zone)
        {
            QWARN() << title() << requestDescription << "failed: no topology zone available to fetch a sessionId with";
            callback(false, tr("No Sonos zone available to sign in with."), {});
            return;
        }

        QLOG() << title() << requestDescription << "fetching sessionId via" << zone->roomName();

        // The speaker itself already holds this service's password (from
        // ThirdPartyMediaServersX) and exchanges it for a sessionId on our
        // behalf -- no raw password ever passes through RoomTunes.
        QNetworkReply *reply = zone->musicServices().GetSessionId(m_smapiId, m_username);
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, requestDescription, callback, onReady]()
                {
                    SoapResponse response(reply);
                    reply->deleteLater();

                    if (response.error())
                    {
                        QWARN() << title() << requestDescription << "GetSessionId failed:" << response.faultCode()
                                << response.faultString();
                        callback(false, response.faultString(), {});
                        return;
                    }

                    const QString sessionId = response.value(QLatin1String("SessionId"));
                    m_smapi.setSessionIdCredentials(m_household->serviceDeviceSerial(), QStringLiteral("Sonos"),
                                                    sessionId);
                    onReady();
                });
        return;
    }

    // Anonymous/Stateless (and any unrecognized policy, since there's no
    // credential step we could perform for it anyway): proceed directly.
    onReady();
}

void SmapiService::applyStoredCredentials()
{
    m_smapi.setLoginTokenCredentials(m_household->serviceDeviceSerial(), QStringLiteral("Sonos"), m_token, m_key,
                                     m_household->householdId());
}

void SmapiService::resolveManifestBrowseEndpoint(std::function<void(const QString &)> callback)
{
    if (m_manifestEndpointState == ManifestEndpointState::Resolved)
    {
        callback(m_manifestBrowseEndpoint);
        return;
    }
    if (m_manifestEndpointState == ManifestEndpointState::Unavailable || m_manifestUri.isEmpty())
    {
        callback(QString());
        return;
    }

    m_manifestEndpointWaiters.append(std::move(callback));
    if (m_manifestEndpointState == ManifestEndpointState::Resolving)
        return;

    m_manifestEndpointState = ManifestEndpointState::Resolving;
    QNetworkRequest request{QUrl(m_manifestUri)};
    request.setTransferTimeout(10000);
    request.setRawHeader("Accept", "application/json");
    QNetworkReply *reply = m_household->networkAccessManager()->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply]()
            {
                const QByteArray body = reply->readAll();
                if (reply->error() == QNetworkReply::NoError)
                    m_manifestBrowseEndpoint = manifestBrowseEndpoint(body);

                m_manifestEndpointState = m_manifestBrowseEndpoint.isEmpty() ? ManifestEndpointState::Unavailable
                                                                             : ManifestEndpointState::Resolved;
                if (m_manifestBrowseEndpoint.isEmpty())
                {
                    if (reply->error() != QNetworkReply::NoError)
                        logNetworkReplyError(logSmapi(), title() + QStringLiteral(" manifest fetch failed"), reply,
                                             body);
                    QWARN() << title() << "manifest has no usable browse endpoint:" << m_manifestUri
                            << reply->errorString();
                }
                else
                {
                    QLOG() << title() << "manifest browse endpoint:" << m_manifestBrowseEndpoint;
                }
                reply->deleteLater();

                const auto waiters = std::exchange(m_manifestEndpointWaiters, {});
                for (const auto &waiter : waiters)
                    waiter(m_manifestBrowseEndpoint);
            });
}

void SmapiService::browseRootViaManifest(ResultCallback callback, std::function<void()> fallback, bool allowAuthRefresh)
{
    resolveManifestBrowseEndpoint(
        [this, callback, fallback, allowAuthRefresh](const QString &endpoint)
        {
            if (endpoint.isEmpty())
            {
                fallback();
                return;
            }

            // A manifest "browse" endpoint is a separate JSON root-catalog
            // transport. It is GET-only and is not a replacement URL for SOAP
            // getMetadata. The complete TPMSX account token is its bearer
            // credential; SMAPI child ids continue through the SOAP endpoint.
            QNetworkRequest request{QUrl(endpoint)};
            request.setTransferTimeout(10000);
            request.setRawHeader("Accept", "application/json");
            request.setRawHeader("Content-Type", "application/json");
            request.setRawHeader("User-Agent", SoapRequest::userAgent().toUtf8());
            request.setRawHeader("X-Sonos-Corr-Id", QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8());
            request.setRawHeader("X-Sonos-Controller-ID", sonosControllerId().toUtf8());
            request.setRawHeader("X-Sonos-Device-Id", m_household->householdId().toUtf8());
            // This is the legacy Sonos controller API key used by the LAN
            // content-browse protocol. It identifies the controller client;
            // the account authorization remains the user's bearer token.
            request.setRawHeader("X-Sonos-Api-Key", "8525505d-78e5-4dab-943f-bafe95b6074d");
            if (m_capabilities & kContextCapability)
                request.setRawHeader("X-Sonos-Context-TimeZone", localUtcOffset().toUtf8());

            const QString locale = QLocale::system().name().replace(QLatin1Char('_'), QLatin1Char('-'));
            request.setRawHeader("Accept-Language",
                                 (locale == QStringLiteral("C") ? QStringLiteral("en-US") : locale).toUtf8());
            if (!m_token.isEmpty())
                request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_token.toUtf8());

            QLOG() << title() << "browse root via manifest JSON endpoint" << endpoint;
            QNetworkReply *reply = m_household->networkAccessManager()->get(request);
            connect(
                reply, &QNetworkReply::finished, this,
                [this, reply, callback, fallback, allowAuthRefresh]()
                {
                    const QByteArray body   = reply->readAll();
                    const int        status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                    const bool       authExpired =
                        reply->error() != QNetworkReply::NoError &&
                        (status == 401 || status == 403 || looksLikeAuthExpiredError(QString::fromUtf8(body)) ||
                         looksLikeAuthExpiredError(reply->errorString()));
                    const QVariantList items = reply->error() == QNetworkReply::NoError
                                                   ? parseManifestBrowseItems(body, m_smapiId, m_serviceId, m_username)
                                                   : QVariantList();

                    if (!items.isEmpty())
                    {
                        QLOG() << title() << "browse root via manifest OK," << items.size() << "item(s)";
                        reply->deleteLater();
                        callback(true, QString(), items);
                        return;
                    }

                    if (authExpired)
                    {
                        logNetworkReplyError(
                            logSmapi(), title() + QStringLiteral(" manifest browse authorization error"), reply, body);
                        QWARN() << title() << "manifest browse authorization failed";
                        reply->deleteLater();
                        if (allowAuthRefresh)
                            refreshAuthTokenForManifestBrowse(callback, fallback);
                        else
                            fallback();
                        return;
                    }

                    if (reply->error() != QNetworkReply::NoError)
                        logNetworkReplyError(logSmapi(), title() + QStringLiteral(" manifest browse failed"), reply,
                                             body);
                    QWARN() << title() << "manifest browse failed or returned no items:"
                            << "http=" << status << "network=" << reply->errorString()
                            << "-- falling back to SMAPI getMetadata";
                    if (reply->error() == QNetworkReply::NoError && !body.isEmpty())
                        QWARN() << title() << "manifest browse response:" << compactXmlForLog(QString::fromUtf8(body));
                    reply->deleteLater();
                    fallback();
                });
        });
}

void SmapiService::refreshAuthTokenForManifestBrowse(ResultCallback callback, std::function<void()> fallback)
{
    QLOG() << title() << "refreshing token via SMAPI getMetadata before retrying manifest browse";
    QNetworkReply *reply = m_smapi.getMetadata(QStringLiteral("root"), 0, 1);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, callback, fallback]()
            {
                SoapResponse response(reply);
                reply->deleteLater();

                if (response.error() && response.faultCode() == QStringLiteral("Client.TokenRefreshRequired") &&
                    !response.refreshedAuthToken().isEmpty() && !response.refreshedPrivateKey().isEmpty())
                {
                    QLOG() << title() << "manifest browse token refresh required -- updating credentials and retrying";
                    m_token = response.refreshedAuthToken();
                    m_key   = response.refreshedPrivateKey();
                    persistDeviceLinkToken(m_household->serviceDeviceSerial(), m_token, m_key,
                                           m_household->householdId());
                    applyStoredCredentials();
                    browseRootViaManifest(callback, fallback, /*allowAuthRefresh=*/false);
                    return;
                }

                QWARN() << title()
                        << "manifest browse token refresh probe did not refresh credentials -- falling back to SMAPI "
                           "getMetadata";
                fallback();
            });
}

void SmapiService::runMetadataRequest(QNetworkReply *reply, const QString &requestDescription, ResultCallback callback,
                                      std::function<QNetworkReply *()> reissue, bool isRetry)
{
    if (!reply)
    {
        callback(false, tr("This service request could not be started."), {});
        return;
    }

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, requestDescription, callback, reissue, isRetry]()
            {
                SoapResponse response(reply);
                reply->deleteLater();

                if (response.error())
                {
                    // The SMAPI server hands back a fresh token/key right in this
                    // fault's detail instead of just rejecting the call -- update
                    // credentials and retry once (roomtunes-bb10's SmapiAuth::
                    // handleAuthRefresh() did the same update but never retried,
                    // so the triggering call just silently failed there; retrying
                    // here means the caller never sees this one at all).
                    if (!isRetry && response.faultCode() == QStringLiteral("Client.TokenRefreshRequired") &&
                        !response.refreshedAuthToken().isEmpty() && !response.refreshedPrivateKey().isEmpty())
                    {
                        QLOG() << title() << requestDescription
                               << "token refresh required -- updating credentials and retrying";
                        // Deliberately leaves m_tpmsxToken/m_tpmsxKey (Household's
                        // last-seen TPMSX snapshot) untouched -- see their
                        // declaration in SmapiService.h. Only m_token/m_key (the
                        // credential actually in use) changes here.
                        m_token = response.refreshedAuthToken();
                        m_key   = response.refreshedPrivateKey();
                        persistDeviceLinkToken(m_household->serviceDeviceSerial(), m_token, m_key,
                                               m_household->householdId());
                        applyStoredCredentials();
                        runMetadataRequest(reissue(), requestDescription, callback, reissue, /*isRetry=*/true);
                        return;
                    }

                    const bool authPolicySupportsReauth =
                        m_authPolicy == QStringLiteral("DeviceLink") || m_authPolicy == QStringLiteral("AppLink");
                    const bool authExpired = authPolicySupportsReauth &&
                                             (response.httpStatusCode() == 401 || response.httpStatusCode() == 403 ||
                                              response.upnpErrorCode() == QStringLiteral("401") ||
                                              looksLikeAuthExpiredError(response.faultString()) ||
                                              looksLikeAuthExpiredError(response.diagnosticText()));
                    if (authExpired)
                    {
                        QWARN() << title() << requestDescription << "reported expired account access";
                        callback(false, tr("Account access has expired."), {});
                        return;
                    }

                    QWARN() << title() << requestDescription << "failed:" << response.faultCode()
                            << response.faultString();
                    callback(false, response.faultString(), {});
                    return;
                }

                const QVariantList items = parseMetadataBody(response, m_smapiId, m_serviceId, m_username);
                if (items.isEmpty())
                {
                    if (!hasSoapBodyPayload(response.rawBody()))
                    {
                        const QString requestXml =
                            response.reply() ? response.reply()->property("soapBody").toString() : QString();
                        QWARN() << title() << requestDescription << "returned empty SMAPI SOAP body from"
                                << replyUrlForLog(response.reply());
                        const QString headers = responseHeadersForLog(response.reply());
                        if (!headers.isEmpty())
                            QWARN() << title() << requestDescription << "SMAPIHDR:" << headers;
                        if (!requestXml.isEmpty())
                            QWARN() << title() << requestDescription << "SMAPIENV:" << compactXmlForLog(requestXml);
                        QWARN() << title() << requestDescription
                                << "SMAPIXML:" << compactXmlForLog(QString::fromUtf8(response.rawBody()));
                        callback(false, tr("This service returned no browse data."), {});
                        return;
                    }

                    if (hasNilMetadataResponse(response.rawBody()))
                    {
                        const QString requestXml =
                            response.reply() ? response.reply()->property("soapBody").toString() : QString();
                        QWARN() << title() << requestDescription << "returned nil SMAPI metadata response";
                        if (!requestXml.isEmpty())
                            QWARN() << title() << requestDescription << "SMAPIENV:" << compactXmlForLog(requestXml);
                        QWARN() << title() << requestDescription
                                << "SMAPIXML:" << compactXmlForLog(QString::fromUtf8(response.rawBody()));
                        callback(false, tr("This service returned no browse data."), {});
                        return;
                    }

                    const QString body = QString::fromUtf8(response.rawBody());
                    QWARN() << title() << requestDescription << "parsed zero SMAPI items; body has mediaCollection="
                            << body.contains(QStringLiteral("mediaCollection"))
                            << "mediaMetadata=" << body.contains(QStringLiteral("mediaMetadata"));
                    const QString requestXml =
                        response.reply() ? response.reply()->property("soapBody").toString() : QString();
                    if (!requestXml.isEmpty())
                        QWARN() << title() << requestDescription << "SMAPIENV:" << compactXmlForLog(requestXml);
                    QWARN() << title() << requestDescription << "SMAPIXML:" << compactXmlForLog(body);
                }
                QLOG() << title() << requestDescription << "OK," << items.size() << "item(s)";
                callback(true, QString(), items);
            });
}

void SmapiService::beginSignIn()
{
    QWARN() << title() << "beginSignIn requested:"
            << "authPolicy=" << m_authPolicy << "householdId=" << !m_household->householdId().isEmpty()
            << "deviceSerial=" << !m_household->serviceDeviceSerial().isEmpty();

    if (m_household->householdId().isEmpty() || m_household->serviceDeviceSerial().isEmpty())
    {
        QWARN() << title() << "beginSignIn blocked: Sonos service prerequisites are not ready";
        emit authorizationFailed(tr("Sonos is still preparing music services."));
        return;
    }

    m_pendingLinkDeviceId.clear();
    m_pendingAuthLinkCode.clear();
    m_pendingShowLinkCode   = true;
    m_authTokenPolling      = false;
    m_authTokenPollInFlight = false;
    m_smapi.setDeviceCredentials(m_household->serviceDeviceSerial(), QStringLiteral("Sonos"));

    auto onLinkReady = [this](bool ok, const QString &linkCode, const QString &regUrl)
    {
        if (!ok)
            return;

        startDeviceAuthTokenPolling(linkCode);
        emit deviceLinkCodeReady(linkCode, regUrl, m_pendingShowLinkCode);
    };

    if (m_authPolicy == QStringLiteral("AppLink"))
        requestAppLinkCode(m_household->householdId(), onLinkReady);
    else if (m_authPolicy == QStringLiteral("DeviceLink"))
        requestDeviceLinkCode(m_household->householdId(), onLinkReady);
    else
    {
        QWARN() << title() << "beginSignIn blocked: unsupported auth policy" << m_authPolicy;
        emit authorizationFailed(tr("This service does not use link-code sign-in."));
    }
}

void SmapiService::completeSignIn(const QString &linkCode)
{
    QWARN() << title() << "completeSignIn requested:"
            << "linkCodeEmpty=" << linkCode.isEmpty()
            << "linkDeviceId=" << (m_pendingLinkDeviceId.isEmpty() ? QStringLiteral("<empty>") : m_pendingLinkDeviceId);

    if (linkCode.isEmpty())
    {
        QWARN() << title() << "completeSignIn blocked: no link code";
        emit authorizationFailed(tr("This service did not provide a sign-in code."));
        return;
    }

    if (m_authTokenPolling)
    {
        QWARN() << title() << "completeSignIn check requested while getDeviceAuthToken polling is already active";
    }
    else
    {
        startDeviceAuthTokenPolling(linkCode);
    }
    pollDeviceAuthToken();
}

void SmapiService::cancelSignIn()
{
    if (m_authTokenPolling)
        QWARN() << title() << "cancel sign-in polling";
    m_authTokenPolling      = false;
    m_authTokenPollInFlight = false;
    m_pendingAuthLinkCode.clear();
    m_pendingLinkDeviceId.clear();
}

void SmapiService::startDeviceAuthTokenPolling(const QString &linkCode)
{
    if (linkCode.isEmpty())
    {
        QWARN() << title() << "getDeviceAuthToken polling not started: no link code";
        return;
    }

    m_pendingAuthLinkCode   = linkCode;
    m_authTokenPolling      = true;
    m_authTokenPollInFlight = false;
    m_authTokenPollStarted.restart();
    QWARN() << title() << "begin polling getDeviceAuthToken after sign-in link was issued";
    pollDeviceAuthToken();
}

void SmapiService::pollDeviceAuthToken()
{
    if (!m_authTokenPolling)
        return;

    if (m_authTokenPollInFlight)
    {
        QWARN() << title() << "getDeviceAuthToken poll skipped: request already in flight";
        return;
    }

    if (m_authTokenPollStarted.elapsed() > kDeviceAuthTokenPollTimeoutMs)
    {
        QWARN() << title() << "getDeviceAuthToken polling timed out after" << m_authTokenPollStarted.elapsed() << "ms";
        m_authTokenPolling      = false;
        m_authTokenPollInFlight = false;
        emit authorizationFailed(tr("Sign-in timed out."));
        return;
    }

    QWARN() << title() << "polling getDeviceAuthToken"
            << "elapsed=" << m_authTokenPollStarted.elapsed() << "ms";
    m_authTokenPollInFlight = true;
    QNetworkReply *reply =
        m_smapi.getDeviceAuthToken(m_household->householdId(), m_pendingAuthLinkCode, m_pendingLinkDeviceId);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply]()
            {
                SoapResponse response(reply);
                reply->deleteLater();
                m_authTokenPollInFlight = false;

                if (!m_authTokenPolling)
                    return;

                if (response.error())
                {
                    if (isRetryableDeviceAuthTokenError(response))
                    {
                        QWARN() << title() << "getDeviceAuthToken pending; retrying in"
                                << kDeviceAuthTokenPollIntervalMs << "ms";
                        QTimer::singleShot(kDeviceAuthTokenPollIntervalMs, this,
                                           [this]()
                                           {
                                               pollDeviceAuthToken();
                                           });
                        return;
                    }

                    QWARN() << title() << "getDeviceAuthToken failed:" << response.faultCode()
                            << response.faultString();
                    m_authTokenPolling      = false;
                    m_authTokenPollInFlight = false;
                    emit authorizationFailed(response.faultString());
                    return;
                }

                const DeviceAuthTokenDetails authToken = parseDeviceAuthTokenResponse(response);
                if (authToken.token.isEmpty() || authToken.key.isEmpty())
                {
                    // BBC Sounds/AppLink can answer getDeviceAuthToken with HTTP 200
                    // before the browser-side authorization has fully completed, but
                    // without returning a token/key yet. Treat that response like the
                    // older DeviceLink NOT_LINKED_RETRY fault and let the existing
                    // poll timeout decide when to fail the sign-in attempt.
                    QWARN() << title() << "getDeviceAuthToken returned no token/key yet; retrying in"
                            << kDeviceAuthTokenPollIntervalMs << "ms";
                    QWARN() << title() << "getDeviceAuthToken SMAPIXML:"
                            << redactedXmlForLog(QString::fromUtf8(response.rawBody()));
                    QTimer::singleShot(kDeviceAuthTokenPollIntervalMs, this,
                                       [this]()
                                       {
                                           pollDeviceAuthToken();
                                       });
                    return;
                }

                QWARN() << title() << "getDeviceAuthToken succeeded; applying refreshed credentials";
                m_authTokenPolling      = false;
                m_authTokenPollInFlight = false;
                m_pendingLinkDeviceId.clear();
                m_pendingAuthLinkCode.clear();
                m_token = authToken.token;
                m_key   = authToken.key;
                setDeviceLinkToken(m_household->serviceDeviceSerial(), authToken.token, authToken.key,
                                   m_household->householdId());
                emit needsSignInChanged();
            });
}

void SmapiService::requestDeviceLinkCode(const QString                                              &householdId,
                                         std::function<void(bool, const QString &, const QString &)> callback)
{
    QWARN() << title() << "requesting getDeviceLinkCode";
    QNetworkReply *reply = m_smapi.getDeviceLinkCode(householdId);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, callback]()
            {
                SoapResponse response(reply);
                reply->deleteLater();

                if (response.error())
                {
                    QWARN() << title() << "getDeviceLinkCode failed:" << response.faultString();
                    emit authorizationFailed(response.faultString());
                    if (callback)
                        callback(false, QString(), QString());
                    return;
                }

                m_pendingShowLinkCode = response.boolValue("ShowLinkCode", true);
                if (callback)
                {
                    const QString linkCode = response.value(QLatin1String("LinkCode"));
                    const QString regUrl   = response.value(QLatin1String("RegUrl"));
                    callback(true, linkCode, regUrl);
                    QLOG() << title() << "sign-in browser url:" << regUrl << "showCode=" << m_pendingShowLinkCode;
                }
            });
}

void SmapiService::requestAppLinkCode(const QString                                              &householdId,
                                      std::function<void(bool, const QString &, const QString &)> callback)
{
    // RoomTunes is a desktop controller, so identify it with Sonos'
    // documented WDCR prefix. AppLink services then return their browser
    // DeviceLink fallback instead of requiring the provider's mobile app.
    QWARN() << title() << "requesting getAppLink";
    QNetworkReply *reply =
        m_smapi.getAppLink(householdId, QSysInfo::prettyProductName(), QSysInfo::productVersion(),
                           QStringLiteral("WDCR_RoomTunes"), QStringLiteral("roomtunes://x-callback-url/addAccount"));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, callback]()
            {
                SoapResponse            response(reply);
                const DeviceLinkDetails details = parseAppLinkDeviceLink(response.rawBody());
                reply->deleteLater();

                if (response.error())
                {
                    QWARN() << title() << "getAppLink failed:" << response.faultString();
                    emit authorizationFailed(response.faultString());
                    if (callback)
                        callback(false, QString(), QString());
                    return;
                }

                if (details.linkCode.isEmpty() || details.registrationUrl.isEmpty())
                {
                    const QString message = tr("This service did not provide a browser sign-in option.");
                    QWARN() << title() << "getAppLink returned no DeviceLink browser fallback";
                    emit authorizationFailed(message);
                    if (callback)
                        callback(false, QString(), QString());
                    return;
                }

                m_pendingLinkDeviceId = details.linkDeviceId;
                m_pendingShowLinkCode = details.showLinkCode;
                if (callback)
                    callback(true, details.linkCode, details.registrationUrl);
                QLOG() << title() << "sign-in browser url:" << details.registrationUrl
                       << "showCode=" << details.showLinkCode;
            });
}

bool SmapiService::applyPersistedDeviceLinkToken()
{
    if (m_authPolicy != QStringLiteral("DeviceLink") && m_authPolicy != QStringLiteral("AppLink"))
        return false;

    const QString deviceId    = m_household->serviceDeviceSerial();
    const QString householdId = m_household->householdId();
    if (deviceId.isEmpty() || householdId.isEmpty())
        return false;

    QSettings settings(smapiSettingsFilePath(), QSettings::IniFormat);
    settings.beginGroup(deviceLinkCredentialGroup(m_serviceId, householdId, deviceId));
    const QString baseToken = settings.value(QStringLiteral("baseToken")).toString();
    const QString baseKey   = settings.value(QStringLiteral("baseKey")).toString();
    const QString token     = settings.value(QStringLiteral("token")).toString();
    const QString key       = settings.value(QStringLiteral("key")).toString();
    settings.endGroup();

    if (token.isEmpty() || key.isEmpty())
        return false;

    if (baseToken != m_tpmsxToken || baseKey != m_tpmsxKey)
    {
        QLOG() << title() << "discarding persisted sign-in token because Sonos service credentials changed";
        settings.remove(deviceLinkCredentialGroup(m_serviceId, householdId, deviceId));
        return false;
    }

    m_token = token;
    m_key   = key;
    QLOG() << title() << "using persisted sign-in token";
    return true;
}

void SmapiService::persistDeviceLinkToken(const QString &deviceId, const QString &token, const QString &key,
                                          const QString &householdId)
{
    QSettings settings(smapiSettingsFilePath(), QSettings::IniFormat);
    settings.beginGroup(deviceLinkCredentialGroup(m_serviceId, householdId, deviceId));
    // TPMSX can lag behind token refreshes and AppLink browser sign-ins, so
    // RoomTunes keeps its own override. The base token/key pins that override
    // to the TPMSX snapshot it was created from; if Sonos later reports a
    // different snapshot, applyPersistedDeviceLinkToken() drops the override
    // and trusts the network state again.
    settings.setValue(QStringLiteral("baseToken"), m_tpmsxToken);
    settings.setValue(QStringLiteral("baseKey"), m_tpmsxKey);
    settings.setValue(QStringLiteral("token"), token);
    settings.setValue(QStringLiteral("key"), key);
    settings.endGroup();
}

void SmapiService::setDeviceLinkToken(const QString &deviceId, const QString &token, const QString &key,
                                      const QString &householdId)
{
    persistDeviceLinkToken(deviceId, token, key, householdId);
    m_smapi.setLoginTokenCredentials(deviceId, QStringLiteral("Sonos"), token, key, householdId);
    emit authorized();
}

} // namespace RoomTunes
