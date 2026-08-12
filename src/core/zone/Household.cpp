#include "Household.h"

#include <algorithm>

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "../Logging.h"
#include "../control/SoapResponse.h"
#include "../services/SmapiService.h"
#include "../services/SonosLibraryService.h"

#define QLOG_CATEGORY logServices
static const QString LOGSEPARATOR(80, QLatin1Char('-'));

namespace RoomTunes
{

namespace
{
// The initial music-service catalog/icon fetches land right in the middle
// of startup's SSDP discovery flood across a dozen zones -- network
// contention makes an occasional timeout/failure here more likely than for
// most other requests. Retrying (rather than the one-shot attempt this
// used to be) is the difference between "Spotify etc. never show up for
// the rest of the session" and "shows up a few seconds late".
constexpr int kServiceFetchRetrySeconds     = 5;
constexpr int kServiceIconFetchRetrySeconds = 60;

QString redactedFormattedXml(QString xml)
{
    xml.replace(
        QRegularExpression(
            QStringLiteral(
                R"(\b((?:Password|Password0|Token|Token0|Key|Key0|AuthToken|PrivateKey|SessionId)\d*)="[^"]*")"),
            QRegularExpression::CaseInsensitiveOption),
        QStringLiteral(R"(\1="<redacted>")"));
    xml.replace(
        QRegularExpression(
            QStringLiteral(
                "<((?:[A-Za-z_][\\w.-]*:)?(?:authToken|privateKey|sessionId|password|token|key))([^>]*)>.*?</\\1>"),
            QRegularExpression::CaseInsensitiveOption),
        QStringLiteral("<\\1\\2><redacted></\\1>"));

    QString          output;
    QXmlStreamReader reader(xml);
    QXmlStreamWriter writer(&output);
    writer.setAutoFormatting(true);
    writer.setAutoFormattingIndent(2);

    while (!reader.atEnd())
    {
        reader.readNext();
        if (reader.hasError())
            break;
        if (reader.tokenType() != QXmlStreamReader::StartDocument)
            writer.writeCurrentToken(reader);
    }

    return output.isEmpty() ? xml : output;
}
} // namespace

Household::Household(QObject *parent) : QObject(parent), m_discovery(&m_netMgr, this)
{
    connect(&m_discovery, &ZoneDiscovery::zoneReady, this, &Household::zoneReady);
    connect(&m_discovery, &ZoneDiscovery::zoneListChanged, this, &Household::zoneListChanged);
    connect(&m_discovery, &ZoneDiscovery::discoveryTimedOut, this, &Household::discoveryTimedOut);
    connect(&m_discovery, &ZoneDiscovery::aboutToResetZones, this, &Household::aboutToResetZones);
    connect(&m_discovery, &ZoneDiscovery::topologySubscriptionZonePicked, this,
            &Household::onTopologySubscriptionZonePicked);
    connect(&m_discovery, &ZoneDiscovery::readyCoordinator, this, &Household::onReadyCoordinator);
    connect(&m_discovery, &ZoneDiscovery::thirdPartyMediaServersXReceived, this,
            &Household::onThirdPartyMediaServersXReceived);
    connect(&m_networkWatcher, &NetworkWatcher::networkChanged, this, &Household::onNetworkChanged);
    connect(this, &Household::zoneReady, this, &Household::installServiceIconResolver);
}

MusicService *Household::libraryService() const
{
    return m_libraryService;
}

MusicService *Household::serviceById(int serviceId) const
{
    const QString key = QStringLiteral("smapi:%1").arg(serviceId);
    if (MusicService *service = m_smapiServicesByKey.value(key, nullptr))
        return service;

    // Sonos exposes two related ids for a partner service. TPMSX/
    // ListAvailableServices gives us the household-scoped serviceId used
    // as m_smapiServicesByKey's stable key, while favourite metadata and
    // playable URIs can refer to the SMAPI id instead. Accept either so a Spotify
    // favourite parsed from SA_RINCON9_ can still resolve the installed
    // Spotify service whose household id may be 2311/3079.
    for (SmapiService *service : m_smapiServicesByKey)
    {
        if (service && service->smapiId() == serviceId)
            return service;
    }

    return nullptr;
}

ZonePlayer *Household::browseCoordinator() const
{
    // make a gateway choice - cannot be a DOCK/BRIDGE/SUB/non-coordinator
    for (ZonePlayer *zone : m_discovery.zones())
    {
        if (!zone || !zone->ready() || zone->invisible())
            continue;
        if (!zone->isCoordinator())
            continue;
        if (zone->modelName().compare(QStringLiteral("DOCK"), Qt::CaseInsensitive) == 0)
            continue;
        if (zone->modelName().contains(QStringLiteral("BRIDGE"), Qt::CaseInsensitive))
            continue;
        if (zone->modelName().contains(QStringLiteral("Sub"), Qt::CaseInsensitive))
            continue;
        if (m_loggedContentDirectoryCoordinatorUdn != zone->udn())
        {
            m_loggedContentDirectoryCoordinatorUdn = zone->udn();
            QLOG() << "content-directory coordinator selected:"
                   << QStringLiteral("%1 (%2 %3)").arg(zone->roomName(), zone->deviceIp(), zone->udn());
        }
        return zone;
    }

    return nullptr;
}

void Household::onNetworkChanged()
{
    // ZoneDiscovery handles unsubscribing/tearing down zones and restarting
    // SSDP itself (see ZoneDiscovery::restart()); everything below is the
    // music-service-side state that's equally invalid once the network has
    // changed -- the old catalog fetch was against a zone that might not
    // even be reachable anymore, and a genuinely different household could
    // be on the other end of a reconnect (different Wi-Fi network).
    m_discovery.restart();

    m_musicServiceStartupStarted         = false;
    m_serviceCatalogReady                = false;
    m_serviceIconsReady                  = false;
    m_serviceDeviceSerialReady           = false;
    m_installedServicesReady             = false;
    m_unavailableInstalledServicesLogged = false;
    updateMusicServicesReady();
    m_catalogFailedZoneUdns.clear();
    m_loggedContentDirectoryCoordinatorUdn.clear();
    m_loggedCatalogFetchCoordinatorUdn.clear();
    m_serviceDeviceSerial.clear();
    m_smapiCatalog.clear();
    m_serviceIcons.clear();
    m_rawInstalledServices.clear();
    m_pendingTpmsxRaw.clear();

    // The old instances (SmapiServices and the library service alike) are
    // against a household/zone set that may no longer even be the same
    // household -- unlike a routine catalog/icon rebuild, this is not a
    // case where preserving them in place is safe. They're owned
    // (parented to this Household), so dropping the pointers is enough;
    // deleteLater() rather than delete since a QML BrowseListPage may
    // still hold one on its call stack right now.
    for (SmapiService *service : std::as_const(m_smapiServicesByKey))
        service->deleteLater();
    m_smapiServicesByKey.clear();
    if (m_libraryService)
    {
        m_libraryService->deleteLater();
        m_libraryService = nullptr;
    }

    if (!m_services.isEmpty())
    {
        m_services.clear();
        emit musicServicesChanged();
    }
}

void Household::onTopologySubscriptionZonePicked(ZonePlayer *)
{
    // Early topology subscription point only. A topology zone is reachable,
    // but ZoneGroupState may not have been parsed yet, so no group
    // coordinator is necessarily ready for ContentDirectory/MusicServices
    // calls. Those continue from onReadyCoordinator().
    decodeInstalledServicesWhenReady();
}

void Household::onReadyCoordinator(ZonePlayer *coordinator)
{
    startMusicServiceStartup(coordinator);

    // The Sonos Music Library doesn't depend on TPMSX at all -- make sure
    // it (and any already-resolved SMAPI services) show up as soon as a
    // zone is reachable, not just when a fresh TPMSX payload arrives (see
    // rebuildMusicServices()'s lazy library-service creation).
    rebuildMusicServices();

    // The household ID is guaranteed non-empty by this point (ZoneDiscovery
    // only emits readyCoordinator after HHID, device_description, and
    // ZoneGroupState have all landed), so this is one of the points a
    // pending TPMSX payload can now successfully decrypt -- see
    // decodeInstalledServicesWhenReady().
    decodeInstalledServicesWhenReady();
}

void Household::onThirdPartyMediaServersXReceived(const QString &encoded)
{
    QLOG() << "ThirdPartyMediaServersX received," << encoded.size() << "byte(s) encoded";

    // Cached unconditionally. The NOTIFY can arrive before the topology
    // subscription zone has been committed locally; when that transition
    // lands, decodeInstalledServicesWhenReady() runs again.
    m_pendingTpmsxRaw = encoded;
    decodeInstalledServicesWhenReady();
}

void Household::decodeInstalledServicesWhenReady()
{
    if (m_installedServicesReady)
        return;

    // Mirrors the original SonosApp::processThirdPartyMediaServersX()
    // preconditions: encrypted payload, household ID, and a zone selected
    // for topology/music-service household calls. Each input is delivered
    // by a separate signal, so this method is intentionally callable from
    // any of those state transitions.
    if (m_pendingTpmsxRaw.isEmpty())
        return;
    if (m_discovery.householdId().isEmpty())
        return;
    if (!m_discovery.topologyZone())
        return;

    m_rawInstalledServices   = ThirdPartyMediaServers::parse(m_discovery.householdId(), m_pendingTpmsxRaw);
    m_installedServicesReady = true;
    QLOG() << "installed services decoded:" << m_rawInstalledServices.size() << "raw installed service(s)";
    logUnavailableInstalledServices();
    rebuildMusicServices();
}

ZonePlayer *Household::pickCatalogFetchZone() const
{
    for (ZonePlayer *zone : m_discovery.zones())
    {
        if (!zone || !zone->ready() || zone->invisible())
            continue;
        if (!zone->isCoordinator())
            continue;
        if (zone->modelName().compare(QStringLiteral("DOCK"), Qt::CaseInsensitive) == 0)
            continue;
        if (zone->modelName().contains(QStringLiteral("BRIDGE"), Qt::CaseInsensitive))
            continue;
        if (zone->modelName().contains(QStringLiteral("Sub"), Qt::CaseInsensitive))
            continue;
        if (!m_catalogFailedZoneUdns.contains(zone->udn()))
        {
            if (m_loggedCatalogFetchCoordinatorUdn != zone->udn())
            {
                m_loggedCatalogFetchCoordinatorUdn = zone->udn();
                QLOG() << "catalog-fetch coordinator selected:"
                       << QStringLiteral("%1 (%2 %3)").arg(zone->roomName(), zone->deviceIp(), zone->udn());
            }
            return zone;
        }
    }

    return nullptr;
}

void Household::startMusicServiceStartup(ZonePlayer *)
{
    if (m_musicServiceStartupStarted)
        return;

    m_musicServiceStartupStarted = true;
    QLOG() << "music-service startup: ready coordinator available";
    fetchMusicServiceCatalog();
    fetchServiceIcons();
    fetchServiceDeviceSerial();
}

void Household::fetchMusicServiceCatalog()
{
    ZonePlayer *zone = pickCatalogFetchZone();
    if (!zone)
    {
        QLOG() << "ListAvailableServices: no ready coordinator yet -- retrying in" << kServiceFetchRetrySeconds << "s";
        QTimer::singleShot(kServiceFetchRetrySeconds * 1000, this, &Household::fetchMusicServiceCatalog);
        return;
    }

    QLOG() << "ListAvailableServices via coordinator" << zone->roomName();
    QNetworkReply *reply = zone->musicServices().ListAvailableServices();
    connect(reply, &QNetworkReply::finished, this, [this, reply, zoneUdn = zone->udn(), roomName = zone->roomName()]() {
        SoapResponse response(reply);
        reply->deleteLater();

        if (response.error())
        {
            QWARN() << "ListAvailableServices failed against" << roomName << ":" << response.faultString()
                    << "-- retrying (possibly against a different zone) in" << kServiceFetchRetrySeconds << "s";
            // Some zones (Subs, and indistinguishably-by-model-name
            // stereo-pair satellite slaves) never implement this action
            // correctly, so blindly retrying the exact same zone forever
            // would strand the whole household's catalog on one
            // permanently-broken device -- see pickCatalogFetchZone().
            m_catalogFailedZoneUdns.insert(zoneUdn);
            QTimer::singleShot(kServiceFetchRetrySeconds * 1000, this, &Household::fetchMusicServiceCatalog);
            return;
        }

        onServiceCatalogFetched(response.value(QStringLiteral("AvailableServiceDescriptorList")).toUtf8(),
                                response.value(QStringLiteral("AvailableServiceTypeList")));
    });
}

void Household::onServiceCatalogFetched(const QByteArray &descriptorList, const QString &typeList)
{
    m_catalogFailedZoneUdns.clear();

    if (verboseLoggingEnabled())
    {
        QLOG() << "AvailableServiceTypeList:";
        QLOG() << typeList;

        QLOG() << "AvailableServiceDescriptorList XML:";
        QLOG() << redactedFormattedXml(QString::fromUtf8(descriptorList));
    }

    m_smapiCatalog        = MusicServiceCatalog::buildSmapiMap(descriptorList, typeList);
    m_serviceCatalogReady = true;

    logServiceMap();
    logUnavailableInstalledServices();
    rebuildMusicServices();
}

void Household::logServiceMap() const
{
    QLOG() << "--------------------------------------------------------------------------------";
    QLOG() << "building service map:" << m_smapiCatalog.size() << "SMAPI service(s)";
    QLOG() << QStringLiteral("      id smapi  %1 %2 %3 %4 %5 %6 %7")
                  .arg(QStringLiteral("title"), -32)
                  .arg(QStringLiteral("auth"), -12)
                  .arg(QStringLiteral("poll"), -6)
                  .arg(QStringLiteral("caps"), -12)
                  .arg(QStringLiteral("type"), -10)
                  .arg(QStringLiteral("endpoint"), -42)
                  .arg(QStringLiteral("manifest"));
    ;

    QList<int> serviceIds = m_smapiCatalog.keys();
    std::sort(serviceIds.begin(), serviceIds.end());
    for (int serviceId : std::as_const(serviceIds))
    {
        const SmapiCatalogEntry &entry    = m_smapiCatalog.value(serviceId);
        const QString            endpoint = entry.secureUri.isEmpty() ? entry.uri : entry.secureUri;
        QLOG() << QStringLiteral("  %1 %2  %3 %4 %5 %6 %7 %8") // %9")
                      .arg(serviceId, 6)
                      .arg(entry.smapiId, 5)
                      .arg(entry.title, -32)
                      .arg(entry.auth.isEmpty() ? QStringLiteral("-") : entry.auth, -12)
                      .arg(entry.pollInterval.isEmpty() ? QStringLiteral("-") : entry.pollInterval, -6)
                      .arg(entry.capabilities.isEmpty() ? QStringLiteral("-") : entry.capabilities, -12)
                      .arg(entry.containerType.isEmpty() ? QStringLiteral("-") : entry.containerType, -10)
                      .arg(endpoint.isEmpty() ? QStringLiteral("-") : endpoint, -42)
            //.arg(entry.manifestUri.isEmpty() ? QStringLiteral("<none>") : entry.manifestUri);
            ;
    }

    for (int legacyId : {1, 2, 3, 11, 13, 14})
    {
        const QString name = MusicServiceCatalog::legacyServiceName(legacyId);
        if (!name.isEmpty())
            QLOG() << QStringLiteral("  %1 %2  %3 %4 %5 %6 %7 %8 %9")
                          .arg(legacyId, 6)
                          .arg(QStringLiteral("-"), 5)
                          .arg(name, -32)
                          .arg(QStringLiteral("legacy"), -12)
                          .arg(QStringLiteral("-"), -6)
                          .arg(QStringLiteral("-"), -12)
                          .arg(QStringLiteral("-"), -10)
                          .arg(QStringLiteral("-"), -42)
                          .arg(QStringLiteral("no SMAPI endpoint"));
    }

    QLOG() << "--------------------------------------------------------------------------------";
}

void Household::logUnavailableInstalledServices()
{
    if (m_unavailableInstalledServicesLogged || !m_installedServicesReady || !m_serviceCatalogReady ||
        m_rawInstalledServices.isEmpty())
    {
        return;
    }

    QStringList unavailable;
    for (const InstalledService &service : m_rawInstalledServices)
    {
        if (!m_smapiCatalog.contains(service.serviceId))
            unavailable.append(QString::number(service.serviceId));
    }

    if (!unavailable.isEmpty())
        QLOG() << "installed services absent from current SMAPI catalog:" << unavailable.join(QStringLiteral(", "));
    m_unavailableInstalledServicesLogged = true;
}

void Household::fetchServiceIcons()
{
    // Plain unauthenticated HTTP GET -- a static Sonos-hosted feed, not a
    // per-zone SOAP action.
    QNetworkRequest request{QUrl(QString::fromLatin1(ServiceLogoCatalog::kUrl))};
    QNetworkReply  *reply = m_netMgr.get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError)
        {
            QWARN() << "mslogo.xml fetch failed:" << reply->errorString() << "-- retrying in background in"
                    << kServiceIconFetchRetrySeconds << "s";
            m_serviceIconsReady = true;
            QTimer::singleShot(kServiceIconFetchRetrySeconds * 1000, this, &Household::fetchServiceIcons);
            return;
        }

        onServiceIconsFetched(ServiceLogoCatalog::parse(reply->readAll()));
    });
}

void Household::onServiceIconsFetched(const QHash<int, QString> &icons)
{
    m_serviceIcons      = icons;
    m_serviceIconsReady = true;
    QLOG() << "service icon catalog built," << m_serviceIcons.size() << "icons";
    rebuildMusicServices();
}

void Household::fetchServiceDeviceSerial()
{
    ZonePlayer *topZone = topologyZone();
    if (!topZone)
        return;

    QNetworkReply *reply = topZone->systemProperties().GetString(QStringLiteral("R_TrialZPSerial"));
    connect(reply, &QNetworkReply::finished, this, [this, reply, topZone]() {
        SoapResponse response(reply);
        reply->deleteLater();

        // Falls back to the zone's own MAC-derived serial number, matching
        // SonosApp::getSerialFinished() -- R_TrialZPSerial can come back
        // empty on some systems.
        QString serial = response.error() ? QString() : response.value(QStringLiteral("StringValue"));
        if (serial.isEmpty())
            serial = topZone->serialNumber();

        onServiceDeviceSerialFetched(serial);
    });
}

void Household::onServiceDeviceSerialFetched(const QString &serial)
{
    m_serviceDeviceSerial      = serial;
    m_serviceDeviceSerialReady = !m_serviceDeviceSerial.isEmpty();
    QLOG() << "service device serial:"
           << (m_serviceDeviceSerial.isEmpty() ? QStringLiteral("<empty>") : m_serviceDeviceSerial);
    updateMusicServicesReady();
}

void Household::installServiceIconResolver(ZonePlayer *zone)
{
    if (!zone)
        return;

    zone->setServiceIconResolver([this](int serviceId) {
        if (const MusicService *service = serviceById(serviceId))
            return service->iconSource();
        return QString();
    });
}

void Household::rebuildMusicServices()
{
    // Created lazily, the first time any zone is reachable -- deliberately
    // not gated on TPMSX at all, so the library shows up even on a
    // household with zero linked SMAPI accounts. Never recreated/replaced
    // once created (see onNetworkChanged() for the one case it's torn
    // down: the whole household changed out from under us).
    if (!m_libraryService && browseCoordinator())
        m_libraryService = new SonosLibraryService(this, this);

    QSet<QString>         seenKeys;
    QList<MusicService *> rebuilt;
    if (m_libraryService)
        rebuilt.append(m_libraryService);

    // Build an "installed services:" summary block - one line per
    // resolved TPMSX entry, reconstructing the UDN shape Sonos itself uses
    QStringList installedServiceLines;

    for (InstalledService service : std::as_const(m_rawInstalledServices))
    {
        // TPMSX is an account/config snapshot, not proof that a service is
        // still usable. Legacy registrations such as Pandora/Last.fm can
        // remain in TPMSX long after Sonos has removed them from the modern
        // controller UX. Match the official app by only showing installed
        // services that also resolve through the current ListAvailableServices
        // catalog, which gives us the SMAPI endpoint/auth policy needed to
        // browse them anyway.
        const auto catalogEntry = m_smapiCatalog.constFind(service.serviceId);
        if (catalogEntry == m_smapiCatalog.constEnd())
            continue;

        service.title      = catalogEntry->title;
        service.serviceUri = catalogEntry->secureUri.isEmpty() ? catalogEntry->uri : catalogEntry->secureUri;
        service.authPolicy = catalogEntry->auth;
        const int smapiId  = catalogEntry->smapiId;

        // Confirmed empirically against the live mslogo.xml feed: unlike
        // bb10-era Sonos (where the icon feed and the SMAPI descriptor list
        // used genuinely different id schemes, needing the smapiId
        // translation), the modern feed keys every entry -- legacy and
        // catalog services alike -- by the household's own serviceId
        // directly. Looking icons up by smapiId (the old scheme) silently
        // found nothing for every catalog-resolved service.
        service.iconUrl = m_serviceIcons.value(service.serviceId);

        // loginName mirrors the UDN suffix Sonos itself uses: a real
        // username for UserId-auth services, or the synthetic
        // "X_#Svc<id>-0-Token" placeholder for a DeviceLink/AppLink service
        // linked via the official Sonos app's own OAuth flow (see
        // ThirdPartyMediaServers.h)
        const QString loginName =
            service.username.isEmpty() ? QStringLiteral("X_#Svc%1-0-Token").arg(service.serviceId) : service.username;
        installedServiceLines << QStringLiteral("  SA_RINCON%1_%2 -> %3 %4")
                                     .arg(service.serviceId)
                                     .arg(loginName, -40)
                                     .arg(service.title, -30)
                                     .arg(loginName);

        // serviceId (not smapiId) is the stable identity across rebuilds:
        // it's known as soon as TPMSX decrypts, before the catalog has
        // necessarily resolved a smapiId for it at all.
        const QString key = QStringLiteral("smapi:%1").arg(service.serviceId);
        seenKeys.insert(key);

        SmapiService *smapiService = m_smapiServicesByKey.value(key);
        const quint32 capabilities = catalogEntry->capabilities.toUInt();
        if (!smapiService)
        {
            smapiService = new SmapiService(this, service.serviceId, smapiId, service.serviceUri, service.authPolicy,
                                            service.username, service.token, service.key, service.title,
                                            service.iconUrl, capabilities, catalogEntry->manifestUri, this);
            m_smapiServicesByKey.insert(key, smapiService);
        }
        else
        {
            smapiService->updateResolved(smapiId, service.serviceUri, service.authPolicy, service.username,
                                         service.token, service.key, service.title, service.iconUrl, capabilities,
                                         catalogEntry->manifestUri);
        }
        rebuilt.append(smapiService);
    }

    if (!installedServiceLines.isEmpty())
    {
        QLOG() << "installed services:";
        for (const QString &line : std::as_const(installedServiceLines))
            QLOG() << line;

        QLOG() << LOGSEPARATOR;
    }

    // Drop instances for services that genuinely disappeared (e.g.
    // unlinked in the official Sonos app) rather than just failed to
    // resolve on this particular rebuild pass.
    for (auto it = m_smapiServicesByKey.begin(); it != m_smapiServicesByKey.end();)
    {
        if (!seenKeys.contains(it.key()))
        {
            it.value()->deleteLater();
            it = m_smapiServicesByKey.erase(it);
        }
        else
        {
            ++it;
        }
    }

    m_services = rebuilt;
    for (ZonePlayer *zone : m_discovery.zones())
        installServiceIconResolver(zone);

    updateMusicServicesReady();
    QLOG() << "rebuildMusicServices:" << m_services.size()
           << "service(s) total (library=" << (m_libraryService != nullptr) << ", smapi=" << m_smapiServicesByKey.size()
           << ", rawInstalled=" << m_rawInstalledServices.size() << ")";
    emit musicServicesChanged();
}

void Household::updateMusicServicesReady()
{
    // Do not let QML browse SMAPI-backed entries until all BB10-era service
    // prerequisites are known: ListAvailableServices gives the catalog/ids,
    // ThirdPartyMediaServersX gives installed account credentials, and
    // R_TrialZPSerial supplies the deviceId used in SMAPI credential
    // headers. Service icons are tracked separately but deliberately do
    // not block browsing.
    // Spotify is tolerant of a missing deviceId; BBC Sounds can return an
    // empty SOAP body, so this readiness gate must include the serial too.
    const bool ready =
        m_libraryService && m_serviceCatalogReady && m_installedServicesReady && m_serviceDeviceSerialReady;
    if (m_musicServicesReady == ready)
        return;

    m_musicServicesReady = ready;
    emit musicServicesReadyChanged();
}

} // namespace RoomTunes
