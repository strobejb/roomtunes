#pragma once

#include <QHash>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QString>

#include "../services/MusicServiceCatalog.h"
#include "../services/ServiceLogoCatalog.h"
#include "../services/ThirdPartyMediaServers.h"
#include "../discovery/NetworkWatcher.h"
#include "../discovery/ZoneDiscovery.h"
#include "ZonePlayer.h"

namespace RoomTunes {

class MusicService;
class SmapiService;
class SonosLibraryService;

// Owns the music-service side of one Sonos household -- catalog/icon
// fetching, and decrypting/resolving ThirdPartyMediaServersX (the
// household's configured service logins, e.g. your own Spotify account) --
// plus a thin, unchanged-from-before public API over ZoneDiscovery for
// zone/topology access. Zone discovery itself (SSDP -> device description
// -> household ID -> ZoneGroupTopology subscription, in that strict order)
// lives entirely in ZoneDiscovery now; see its class comment for the exact
// process. No global singleton: owned by the caller (the CLI tool for now,
// the GUI later) and passed by pointer to anything that needs it.
//
// ThirdPartyMediaServersX can only be decrypted once BOTH the household ID
// and a topology subscription zone are known -- see
// decodeInstalledServicesWhenReady(), which is called from the explicit
// state transitions that provide those inputs.
//
// Also owns a NetworkWatcher: when the local network changes (disconnect,
// reconnect, or silently switching interfaces on the same LAN), every
// piece of state above is invalidated -- old zone addresses, the household
// ID, the topology subscription, the music-service catalog -- and has to
// be thrown away and rebuilt from scratch via onNetworkChanged(), which
// resets both ZoneDiscovery and Household's own state before discovery
// restarts.
class Household : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool musicServicesReady READ musicServicesReady NOTIFY musicServicesReadyChanged)

public:
    explicit Household(QObject *parent = nullptr);

    // The Sonos Music Library service specifically -- QML-callable so a
    // page can browse an arbitrary ContentDirectory object id through it
    // without going via MusicServiceListModel first (see BrowseHome.qml's
    // "FV:2" Sonos Favorites browse, an ordinary ContentDirectory container
    // id like the library's own A:ALBUM/etc, just not one this service's
    // own root category list happens to enumerate). Null until a zone is
    // reachable (see rebuildMusicServices()).
    Q_INVOKABLE MusicService *libraryService() const;
    Q_INVOKABLE MusicService *serviceById(int serviceId) const;
    bool musicServicesReady() const { return m_musicServicesReady; }

    bool startDiscovery(quint16 localPort = Ssdp::kDefaultRecvPort) { return m_discovery.start(localPort); }

    const QString &householdId() const { return m_discovery.householdId(); }

    QList<ZonePlayer *> zones() const { return m_discovery.zones(); }
    ZonePlayer *zone(const QString &udn) const { return m_discovery.zone(udn); }
    ZonePlayer *zoneByRoomName(const QString &roomName) const { return m_discovery.zoneByRoomName(roomName); }

    // The zone holding the ZoneGroupTopology subscription -- also the zone
    // SmapiService issues MusicServices:1 GetSessionId calls against
    // (any zone would do; this one's already known-reachable), and the
    // initial fallback zone for household-level service calls.
    ZonePlayer *topologyZone() const { return m_discovery.topologyZone(); }
    ZonePlayer *browseCoordinator() const;

    // R_TrialZPSerial, a per-household serial Sonos itself uses as the
    // "deviceId" for SMAPI loginToken/sessionId credentials (see
    // SmapiService::withCredentials()) -- ported from
    // SonosApp::getSerialFinished(). Empty until fetchServiceDeviceSerial()
    // completes.
    const QString &serviceDeviceSerial() const { return m_serviceDeviceSerial; }

    QNetworkAccessManager *networkAccessManager() { return &m_netMgr; }

    // One-shot GetZoneGroupState poll of the topology zone. Used as a
    // fallback if the GENA subscription fails, and available for a manual
    // "refresh" gesture; normal updates arrive via NOTIFY once subscribed.
    void refreshTopology() { m_discovery.refreshTopology(); }

    // Every browsable music service for this household: the Sonos local
    // Music Library (always present once a zone is reachable, regardless
    // of whether any SMAPI account is linked) plus one SmapiService per
    // entry actually configured/logged-in on this household (e.g. your own
    // Spotify account) -- not Sonos' global service catalog. See
    // ThirdPartyMediaServers.h/MusicServiceCatalog.h for how the SMAPI side
    // is resolved, and rebuildMusicServices() for the instance-preserving
    // rebuild that keeps this list's pointers stable across catalog/icon
    // fetches.
    QList<MusicService *> services() const { return m_services; }

signals:
    void zoneReady(ZonePlayer *zone);
    void zoneListChanged();
    void discoveryTimedOut();
    void musicServicesChanged();
    void musicServicesReadyChanged();
    // See ZoneDiscovery::aboutToResetZones() -- forwarded as-is so QML can
    // drop any raw ZonePlayer* it's holding (e.g. the selected zone)
    // before a network-change-triggered restart() destroys it.
    void aboutToResetZones();

private:
    void onTopologySubscriptionZonePicked(ZonePlayer *zone);
    void onReadyCoordinator(ZonePlayer *zone);
    void onThirdPartyMediaServersXReceived(const QString &encoded);
    void onNetworkChanged();

    ZonePlayer *pickCatalogFetchZone() const;
    void startMusicServiceStartup(ZonePlayer *coordinator);
    void fetchMusicServiceCatalog();
    void onServiceCatalogFetched(const QByteArray &descriptorList, const QString &typeList);
    void fetchServiceIcons();
    void onServiceIconsFetched(const QHash<int, QString> &icons);
    void fetchServiceDeviceSerial();
    void onServiceDeviceSerialFetched(const QString &serial);
    void decodeInstalledServicesWhenReady();
    void rebuildMusicServices();
    void updateMusicServicesReady();
    void logServiceMap() const;
    void logUnavailableInstalledServices();

private:
    // Declaration order matters: m_discovery holds a pointer to m_netMgr,
    // taken in the member-initializer list, so m_netMgr must be
    // constructed first.
    QNetworkAccessManager m_netMgr;
    ZoneDiscovery m_discovery;
    NetworkWatcher m_networkWatcher;

    bool m_musicServiceStartupStarted = false;
    bool m_serviceCatalogReady = false;
    bool m_serviceIconsReady = false;
    bool m_serviceDeviceSerialReady = false;
    bool m_installedServicesReady = false;
    bool m_unavailableInstalledServicesLogged = false;
    bool m_musicServicesReady = false;
    QSet<QString> m_catalogFailedZoneUdns; // zones ListAvailableServices has already failed against this session
    mutable QString m_loggedContentDirectoryCoordinatorUdn;
    mutable QString m_loggedCatalogFetchCoordinatorUdn;
    QString m_serviceDeviceSerial;
    QHash<int, SmapiCatalogEntry> m_smapiCatalog;
    QHash<int, QString> m_serviceIcons; // keyed by smapiId (SMAPI) or raw legacy id
    QList<InstalledService> m_rawInstalledServices; // as decrypted, titles unresolved

    // rebuildMusicServices() fires repeatedly (catalog fetch, icon fetch,
    // TPMSX arrival) -- recreating service objects on every rebuild would
    // dangle any `service` a QML BrowseListPage is currently holding
    // mid-navigation. Existing instances are refreshed in place via
    // SmapiService::updateResolved() instead; only genuinely new services
    // are constructed, and only ones that truly disappear are removed. Both
    // maps/pointers own their objects (parented to this Household).
    QHash<QString, SmapiService *> m_smapiServicesByKey; // keyed by "smapi:<serviceId>"
    SonosLibraryService *m_libraryService = nullptr;     // created lazily once any zone is reachable
    QList<MusicService *> m_services;                    // [library] + [smapi services...], ready for display

    // Raw encrypted <ThirdPartyMediaServersX> payload, cached as soon as it
    // arrives regardless of whether the household ID/topology zone are
    // known yet -- see decodeInstalledServicesWhenReady().
    QString m_pendingTpmsxRaw;
};

}
