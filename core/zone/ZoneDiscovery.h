#pragma once

#include <QMap>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QTimer>

#include "../upnp/GenaNotifyServer.h"
#include "../upnp/Ssdp.h"
#include "../upnp/UpnpService.h"
#include "ZonePlayer.h"

namespace RoomTunes {

// Owns the ordered zone-discovery process for one Sonos household. Used to
// live muddled together with music-service catalog concerns inside
// Household.cpp; split out to match how the original ZoneDiscovery.cpp/
// Sonos.cpp kept discovery and catalog fetching as separate responsibilities.
//
// The strict order this follows (see also the original ZoneDiscovery.cpp's
// header comment, which this ports near-verbatim):
//
//   1. SSDP broadcast (Ssdp::discover()).
//   2. A ZonePlayer is allocated for every distinct SSDP response.
//   3. Subnet detection: if a reply's LOCATION host differs from the UDP
//      packet's actual source address, this machine has more than one
//      network path to the household (e.g. VPN, multiple NICs) -- LOCATION
//      (reported by the device itself) is what's trusted for the zone's
//      address, but the mismatch is logged since it's diagnostically
//      useful and was previously silent in this port.
//   4. updateDeviceDescription (device_description.xml) is fetched for
//      every ZonePlayer, whether it was discovered via SSDP or later via
//      topology.
//   5. GetHouseholdID is called for a zone if the household ID isn't
//      already known from an earlier zone.
//   6. Once any zone has a valid device description and household ID, one
//      zone (never a DOCK/BRIDGE/Sub) is picked to hold the household's one
//      real GENA subscription to ZoneGroupTopology; every NOTIFY on it (the
//      SUBSCRIBE response itself implies an initial one, per UPnP eventing
//      rules) parses the ZoneGroup list, allocating any zone not yet seen
//      via SSDP.
//
// A zone is 'ready' only once household ID, device description, AND
// topology have all been applied to it -- whichever of those three
// finishes last is what triggers zoneReady(). Over time many further
// ZoneGroupTopology NOTIFYs arrive as zones group/ungroup or become
// hidden (stereo pairs); those keep flowing through the same
// parseZoneGroupState() path, not just the initial one.
//
// Network-change handling (the original's "wifi connected/disconnected"
// lifecycle) is implemented via restart(), driven by NetworkWatcher rather
// than platform-specific interface events -- see restart()'s own comment.
//
// NOTE: foreground/background app-visibility lifecycle handling (the
// original ZoneDiscovery.cpp's header comment's other point) is not yet
// implemented in this port -- there's no equivalent concept for a desktop
// app the way there was for a mobile one.
class ZoneDiscovery : public QObject
{
    Q_OBJECT

public:
    explicit ZoneDiscovery(QNetworkAccessManager *netMgr, QObject *parent = nullptr);
    ~ZoneDiscovery() override;

    bool start(quint16 localPort = Ssdp::kDefaultRecvPort);

    // Tears down all known zones and re-runs discovery from scratch --
    // used when the local network itself has changed (see NetworkWatcher):
    // every previously-known zone's address, the household ID, and the
    // topology subscription may all now be invalid. Best-effort unsubscribe
    // from the old topology subscription first (it may simply fail or time
    // out if we're now on a different network entirely -- that's fine,
    // there's nothing meaningful to do about it either way), then a full
    // reset. Also rebinds the SSDP socket (see Ssdp::listen()) -- confirmed
    // against a real Ethernet-unplugged/Wi-Fi-only network switch that the
    // socket's outbound multicast interface does *not* survive an
    // interface change on its own: every M-SEARCH send silently failed
    // forever afterwards until this was added, which is why discovery
    // never recovered and the UI stayed empty.
    void restart();

    const QString &householdId() const { return m_householdId; }

    QList<ZonePlayer *> zones() const { return m_zones.values(); }
    ZonePlayer *zone(const QString &udn) const { return m_zones.value(udn); }
    ZonePlayer *zoneByRoomName(const QString &roomName) const;

    // The zone holding the ZoneGroupTopology subscription -- also the zone
    // SmapiService issues MusicServices:1 GetSessionId calls against
    // (any zone would do; this one's already known-reachable).
    ZonePlayer *topologyZone() const { return m_zones.value(m_topologyZoneUdn); }

    // One-shot GetZoneGroupState poll of the topology zone. Used as a
    // fallback if the GENA subscription fails, and available for a manual
    // "refresh" gesture; normal updates arrive via NOTIFY once subscribed.
    void refreshTopology();

signals:
    void zoneReady(ZonePlayer *zone);
    void zoneListChanged();
    void discoveryTimedOut();
    // Fired by restart() *before* any existing ZonePlayer is destroyed --
    // separate from zoneListChanged() (which fires after, once the list is
    // actually empty/repopulating) so a UI holding a raw ZonePlayer*
    // (e.g. QML's "currently selected zone") gets a safe chance to drop
    // that reference while the object is still alive, rather than relying
    // on QML's own deleted-QObject guard for a value it never expected to
    // become invalid mid-session.
    void aboutToResetZones();
    // Fired once per ZoneDiscovery lifetime, right as the ZoneGroupTopology
    // SUBSCRIBE is issued (not once it completes). Household uses this as
    // the trigger for its one-time music-service catalog/icon/serial
    // fetches -- those have nothing to do with discovery itself, they just
    // need *a* reachable zone to run against, and this is the first point
    // one is known good.
    void topologyZonePicked(ZonePlayer *zone);
    // Raw <ThirdPartyMediaServersX> GENA payload -- arrives on the same
    // ZoneGroupTopology subscription's NOTIFY channel as topology changes
    // (Sonos multiplexes both properties onto one subscription), but
    // decrypting/resolving it is a music-services concern, not a discovery
    // one, so it's just handed off rather than processed here.
    void thirdPartyMediaServersXReceived(const QString &encoded);

private slots:
    void onSsdpDiscovered(const QString &fromAddr, const QMap<QString, QString> &headers);
    void onSsdpTimeout();
    void onGenaNotify(const QString &sid, const QByteArray &body);
    void renewTopologySubscription();
    void renewZoneEventSubscriptions();

private:
    enum class ZoneEventService {
        AVTransport,
        RenderingControl,
        ContentDirectory,
        AudioIn
    };

    struct ZoneEventSubscription {
        QString zoneUdn;
        ZoneEventService service;
    };

    ZonePlayer *allocateZone(const QString &deviceIp, const QString &udn);
    void fetchDeviceDescription(ZonePlayer *zone);
    void fetchHouseholdId(ZonePlayer *zone);
    void maybePickTopologyZone();
    void subscribeTopology();
    void subscribeZoneEvents(ZonePlayer *zone);
    void subscribeZoneEvent(ZonePlayer *zone, UpnpService &service, ZoneEventService serviceType);
    void unsubscribeZoneEvents();
    void routeZoneEvent(const ZoneEventSubscription &subscription, const QByteArray &body);
    void parseZoneGroupState(const QByteArray &xml);
    void checkZoneReady(ZonePlayer *zone);

private:
    QNetworkAccessManager *m_netMgr;
    Ssdp m_ssdp;
    GenaNotifyServer m_notifyServer;

    quint16 m_localPort = Ssdp::kDefaultRecvPort; // captured in start(), reused by restart() to rebind Ssdp

    QString m_householdId;
    QMap<QString, ZonePlayer *> m_zones; // keyed by UDN
    QString m_topologyZoneUdn;
    QString m_topologySubscriptionSid;
    QMap<QString, ZoneEventSubscription> m_zoneEventSubscriptions;
    QTimer m_topologyRenewTimer;
    QTimer m_zoneEventRenewTimer;
};

}
