#include "Household.h"

#include <algorithm>

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include "../Logging.h"
#include "../services/SmapiService.h"
#include "../services/SonosLibraryService.h"
#include "../upnp/SoapResponse.h"

#define QLOG_CATEGORY logDiscovery

namespace RoomTunes {

namespace {
// The initial music-service catalog/icon fetches land right in the middle
// of startup's SSDP discovery flood across a dozen zones -- network
// contention makes an occasional timeout/failure here more likely than for
// most other requests. Retrying (rather than the one-shot attempt this
// used to be) is the difference between "Spotify etc. never show up for
// the rest of the session" and "shows up a few seconds late".
constexpr int kServiceFetchRetrySeconds = 5;
}

Household::Household(QObject *parent)
    : QObject(parent)
    , m_discovery(&m_netMgr, this)
{
    connect(&m_discovery, &ZoneDiscovery::zoneReady, this, &Household::zoneReady);
    connect(&m_discovery, &ZoneDiscovery::zoneListChanged, this, &Household::zoneListChanged);
    connect(&m_discovery, &ZoneDiscovery::discoveryTimedOut, this, &Household::discoveryTimedOut);
    connect(&m_discovery, &ZoneDiscovery::aboutToResetZones, this, &Household::aboutToResetZones);
    connect(&m_discovery, &ZoneDiscovery::topologyZonePicked, this, &Household::onTopologyZonePicked);
    connect(&m_discovery, &ZoneDiscovery::thirdPartyMediaServersXReceived, this,
            &Household::onThirdPartyMediaServersXReceived);
    connect(&m_networkWatcher, &NetworkWatcher::networkChanged, this, &Household::onNetworkChanged);
}

MusicService *Household::libraryService() const
{
    return m_libraryService;
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

    m_catalogFetched = false;
    m_catalogFailedZoneUdns.clear();
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
    if (m_libraryService) {
        m_libraryService->deleteLater();
        m_libraryService = nullptr;
    }

    if (!m_services.isEmpty()) {
        m_services.clear();
        emit musicServicesChanged();
    }
}

void Household::onTopologyZonePicked(ZonePlayer *)
{
    if (!m_catalogFetched) {
        m_catalogFetched = true;
        fetchMusicServiceCatalog();
        fetchServiceIcons();
        fetchServiceDeviceSerial();
    }

    // The Sonos Music Library doesn't depend on TPMSX at all -- make sure
    // it (and any already-resolved SMAPI services) show up as soon as a
    // zone is reachable, not just when a fresh TPMSX payload arrives (see
    // rebuildMusicServices()'s lazy library-service creation).
    rebuildMusicServices();

    // The household ID is guaranteed non-empty by this point (ZoneDiscovery
    // only ever picks a topology zone once its household ID is known), so
    // this is one of the two points a pending TPMSX payload can now
    // successfully decrypt -- see tryProcessThirdPartyMediaServersX().
    tryProcessThirdPartyMediaServersX();
}

void Household::onThirdPartyMediaServersXReceived(const QString &encoded)
{
    QLOG() << "ThirdPartyMediaServersX received," << encoded.size() << "byte(s) encoded";

    // Cached unconditionally -- the household ID or topology zone might
    // not be ready yet (in practice, by construction, they always are by
    // the time any NOTIFY can arrive; this is the defensive path in case
    // that ever stops being true, e.g. a future change adds a delay
    // somewhere upstream). Either way, the raw payload isn't lost: it's
    // retried from onTopologyZonePicked too.
    m_pendingTpmsxRaw = encoded;
    tryProcessThirdPartyMediaServersX();
}

void Household::tryProcessThirdPartyMediaServersX()
{
    // Mirrors the original SonosApp::processThirdPartyMediaServersX()'s
    // three preconditions -- topology zone known, raw payload received,
    // household ID known -- and is safe to call redundantly from either
    // trigger point (a fresh payload arriving, or the topology zone first
    // being picked): it just no-ops until all three are actually true.
    if (m_pendingTpmsxRaw.isEmpty()) {
        QLOG() << "tryProcessThirdPartyMediaServersX: no payload received yet, nothing to do";
        return;
    }
    if (m_discovery.householdId().isEmpty()) {
        QLOG() << "tryProcessThirdPartyMediaServersX: waiting on household ID";
        return;
    }
    if (!m_discovery.topologyZone()) {
        QLOG() << "tryProcessThirdPartyMediaServersX: waiting on a topology zone";
        return;
    }

    m_rawInstalledServices = ThirdPartyMediaServers::parse(m_discovery.householdId(), m_pendingTpmsxRaw);
    QLOG() << "tryProcessThirdPartyMediaServersX: decrypted" << m_rawInstalledServices.size() << "raw installed service(s)";
    rebuildMusicServices();
}

ZonePlayer *Household::pickCatalogFetchZone() const
{
    for (ZonePlayer *zone : m_discovery.zones()) {
        if (zone->householdId().isEmpty())
            continue;
        if (!m_catalogFailedZoneUdns.contains(zone->udn()))
            return zone;
    }

    // Every known zone has already failed at least once this session --
    // nothing left to prefer, so fall back to the topology zone (or, if
    // that's somehow also gone, whatever we've got). Still bounded/retried
    // via the usual kServiceFetchRetrySeconds delay, not a tight loop.
    if (ZonePlayer *fallback = m_discovery.topologyZone())
        return fallback;
    const QList<ZonePlayer *> all = m_discovery.zones();
    return all.isEmpty() ? nullptr : all.first();
}

void Household::fetchMusicServiceCatalog()
{
    ZonePlayer *zone = pickCatalogFetchZone();
    if (!zone)
        return;

    QNetworkReply *reply = zone->musicServices().ListAvailableServices();
    connect(reply, &QNetworkReply::finished, this, [this, reply, zoneUdn = zone->udn(), roomName = zone->roomName()]() {
        SoapResponse response(reply);
        reply->deleteLater();

        if (response.error()) {
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

        m_catalogFailedZoneUdns.clear();

        const QByteArray descriptorList = response.value(QStringLiteral("AvailableServiceDescriptorList")).toUtf8();
        const QString typeList = response.value(QStringLiteral("AvailableServiceTypeList"));
        m_smapiCatalog = MusicServiceCatalog::build(descriptorList, typeList);

        logServiceMap();
        rebuildMusicServices();
    });
}

// Matches roomtunes-bb10's "building service map:" dump (ServiceDiscovery.cpp)
// -- one line per catalog entry: household-scoped serviceId -> smapiId,
// title, auth policy, container type, SMAPI endpoint.
void Household::logServiceMap() const
{
    QLOG() << "--------------------------------------------------------------------------------";
    QLOG() << "building service map:" << m_smapiCatalog.size() << "SMAPI service(s)";

    QList<int> serviceIds = m_smapiCatalog.keys();
    std::sort(serviceIds.begin(), serviceIds.end());
    for (int serviceId : std::as_const(serviceIds)) {
        const SmapiCatalogEntry &entry = m_smapiCatalog.value(serviceId);
        QLOG().noquote() << QStringLiteral("  %1 -> %2  %3 %4 %5 %6")
                                 .arg(serviceId, 6)
                                 .arg(entry.smapiId, 5)
                                 .arg(entry.title, -32)
                                 .arg(entry.auth, -10)
                                 .arg(entry.containerType, -8)
                                 .arg(entry.uri);
    }

    for (int legacyId : {1, 2, 3, 11, 13, 14}) {
        const QString name = MusicServiceCatalog::legacyServiceName(legacyId);
        if (!name.isEmpty())
            QLOG().noquote() << QStringLiteral("  %1 ->     -  %2 (legacy, no SMAPI endpoint)").arg(legacyId, 6).arg(name);
    }

    QLOG() << "--------------------------------------------------------------------------------";
}

void Household::fetchServiceIcons()
{
    // Plain unauthenticated HTTP GET -- a static Sonos-hosted feed, not a
    // per-zone SOAP action.
    QNetworkRequest request{QUrl(QString::fromLatin1(ServiceLogoCatalog::kUrl))};
    QNetworkReply *reply = m_netMgr.get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            QWARN() << "mslogo.xml fetch failed:" << reply->errorString() << "-- retrying in" << kServiceFetchRetrySeconds
                    << "s";
            QTimer::singleShot(kServiceFetchRetrySeconds * 1000, this, &Household::fetchServiceIcons);
            return;
        }

        m_serviceIcons = ServiceLogoCatalog::parse(reply->readAll());
        QLOG() << "service icon catalog built," << m_serviceIcons.size() << "icons";
        rebuildMusicServices();
    });
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
        m_serviceDeviceSerial = response.error() ? QString() : response.value(QStringLiteral("StringValue"));
        if (m_serviceDeviceSerial.isEmpty())
            m_serviceDeviceSerial = topZone->serialNumber();

        QLOG() << "service device serial:" << m_serviceDeviceSerial;
    });
}

void Household::rebuildMusicServices()
{
    // Created lazily, the first time any zone is reachable -- deliberately
    // not gated on TPMSX at all, so the library shows up even on a
    // household with zero linked SMAPI accounts. Never recreated/replaced
    // once created (see onNetworkChanged() for the one case it's torn
    // down: the whole household changed out from under us).
    if (!m_libraryService && m_discovery.topologyZone())
        m_libraryService = new SonosLibraryService(this, this);

    QSet<QString> seenKeys;
    QList<MusicService *> rebuilt;
    if (m_libraryService)
        rebuilt.append(m_libraryService);

    // Matches roomtunes-bb10's "installed services:" summary block
    // (SonosBrowse.cpp's login() dump) -- one line per resolved TPMSX
    // entry, reconstructing the UDN shape Sonos itself uses
    // ("SA_RINCON<serviceId>_<username>") since InstalledService only
    // keeps the fields parsed out of it, not the raw string.
    QStringList installedServiceLines;

    for (InstalledService service : std::as_const(m_rawInstalledServices)) {
        // TPMSX is an account/config snapshot, not proof that a service is
        // still usable. Legacy registrations such as Pandora/Last.fm can
        // remain in TPMSX long after Sonos has removed them from the modern
        // controller UX. Match the official app by only showing installed
        // services that also resolve through the current ListAvailableServices
        // catalog, which gives us the SMAPI endpoint/auth policy needed to
        // browse them anyway.
        const auto catalogEntry = m_smapiCatalog.constFind(service.serviceId);
        if (catalogEntry == m_smapiCatalog.constEnd()) {
            QLOG() << "service skipped: serviceId=" << service.serviceId
                   << "(stored in TPMSX but not in current SMAPI catalog)";
            continue;
        }

        service.title = catalogEntry->title;
        service.serviceUri = catalogEntry->uri;
        service.authPolicy = catalogEntry->auth;
        const int smapiId = catalogEntry->smapiId;
        // Confirmed empirically against the live mslogo.xml feed: unlike
        // bb10-era Sonos (where the icon feed and the SMAPI descriptor list
        // used genuinely different id schemes, needing the smapiId
        // translation), the modern feed keys every entry -- legacy and
        // catalog services alike -- by the household's own serviceId
        // directly. Looking icons up by smapiId (the old scheme) silently
        // found nothing for every catalog-resolved service.
        service.iconUrl = m_serviceIcons.value(service.serviceId);

        //QLOG() << "service:" << service.title << "serviceId=" << service.serviceId << "smapiId=" << smapiId
        //                       << "icon=" << (service.iconUrl.isEmpty() ? QStringLiteral("<none found>") : service.iconUrl);

        // loginName mirrors the UDN suffix Sonos itself uses: a real
        // username for UserId-auth services, or the synthetic
        // "X_#Svc<id>-0-Token" placeholder for a DeviceLink/AppLink service
        // linked via the official Sonos app's own OAuth flow (see
        // ThirdPartyMediaServers.h) -- matches roomtunes-bb10's own
        // "installed services:" dump, which repeats this same identifier
        // twice (once in the reconstructed UDN, once as its own column).
        const QString loginName = service.username.isEmpty() ? QStringLiteral("X_#Svc%1-0-Token").arg(service.serviceId)
                                                               : service.username;
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
        if (!smapiService) {
            smapiService = new SmapiService(this, service.serviceId, smapiId, service.serviceUri, service.authPolicy,
                                             service.username, service.token, service.key, service.title,
                                             service.iconUrl, this);
            m_smapiServicesByKey.insert(key, smapiService);
        } else {
            smapiService->updateResolved(smapiId, service.serviceUri, service.authPolicy, service.username,
                                          service.token, service.key, service.title, service.iconUrl);
        }
        rebuilt.append(smapiService);
    }

    if (!installedServiceLines.isEmpty()) {
        QLOG() << "installed services:";
        for (const QString &line : std::as_const(installedServiceLines))
            QLOG().noquote() << line;
    }

    // Drop instances for services that genuinely disappeared (e.g.
    // unlinked in the official Sonos app) rather than just failed to
    // resolve on this particular rebuild pass.
    for (auto it = m_smapiServicesByKey.begin(); it != m_smapiServicesByKey.end();) {
        if (!seenKeys.contains(it.key())) {
            it.value()->deleteLater();
            it = m_smapiServicesByKey.erase(it);
        } else {
            ++it;
        }
    }

    m_services = rebuilt;
    QLOG() << "rebuildMusicServices:" << m_services.size() << "service(s) total (library=" << (m_libraryService != nullptr)
           << ", smapi=" << m_smapiServicesByKey.size() << ", rawInstalled=" << m_rawInstalledServices.size() << ")";
    emit musicServicesChanged();
}

}
