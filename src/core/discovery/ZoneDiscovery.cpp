#include "ZoneDiscovery.h"

#include <algorithm>

#include <QDateTime>
#include <QHostAddress>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QUrl>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "../Logging.h"
#include "../control/Soap.h"
#include "../control/SoapResponse.h"
#include "../xml/XmlUtils.h"

#define QLOG_CATEGORY logDiscovery
static const QString LOGSEPARATOR(80, QLatin1Char('-'));

namespace RoomTunes
{

namespace
{
struct DeviceDescription
{
    QString       roomName;
    QString       displayName;
    QString       modelName;
    QString       serialNumber;
    QString       displayVersion;
    QString       softwareVersion;
    QString       zoneType;
    QStringList   features;
    QSet<QString> services;
};

// The UDP source address of an SSDP reply can come back as an IPv4-mapped
// IPv6 address ("::ffff:192.168.1.x") depending on the platform's socket
// stack, which isn't valid to embed directly in an "http://<ip>:1400/" URL.
// Only used as a fallback when a reply is missing its LOCATION header.
QString normalizeIPv4(const QString &address)
{
    bool          ok   = false;
    const quint32 ipv4 = QHostAddress(address).toIPv4Address(&ok);
    return ok ? QHostAddress(ipv4).toString() : address;
}

QString serviceNameFromType(const QString &serviceType)
{
    const QString marker = QStringLiteral(":service:");
    const int     start  = serviceType.indexOf(marker);
    if (start < 0)
        return {};

    const int nameStart = start + marker.size();
    const int nameEnd   = serviceType.indexOf(QLatin1Char(':'), nameStart);
    if (nameEnd < 0)
        return {};

    return serviceType.mid(nameStart, nameEnd - nameStart);
}

void collectDeviceServices(const XmlNode &device, DeviceDescription *description)
{
    for (const XmlNode &service : device.all(".//serviceList/service"))
    {
        const QString serviceName = serviceNameFromType(service.text("serviceType"));
        if (!serviceName.isEmpty())
            description->services.insert(serviceName);
    }
}

void parseDeviceElement(const XmlNode &device, DeviceDescription *description)
{
    description->roomName        = device.text("roomName");
    description->displayName     = device.text("displayName");
    description->modelName       = device.text("modelName");
    description->serialNumber    = device.text("serialNum");
    description->displayVersion  = device.text("displayVersion");
    description->softwareVersion = device.text("softwareVersion");
    description->zoneType        = device.text("zoneType");

    for (const XmlNode &child : device.children())
        if (child.nameStartsWith("feature"))
            description->features.append(child.text());

    // Sonos device_description.xml has useful services both on the root
    // ZonePlayer device and on nested MediaRenderer/MediaServer devices.
    // RenderingControl and AVTransport live in those nested devices, so
    // skipping nested device services makes startup mute/volume polling
    // impossible.
    collectDeviceServices(device, description);
}

DeviceDescription parseDeviceDescription(const QByteArray &body)
{
    DeviceDescription description;
    const XmlDoc      doc    = XmlDoc::parse(body);
    const XmlNode     device = doc.first("//device");

    if (device)
        parseDeviceElement(device, &description);

    return description;
}

QString featureAt(const ZonePlayer *zone, qsizetype index)
{
    return zone && index < zone->features().size() ? zone->features().at(index) : QString();
}

void logDeviceDescription(const QString &deviceIp, const DeviceDescription &description)
{
    ScopedLogEndpoint endpoint(deviceIp, LogDirection::Inbound);
    const QString     displayName = description.displayName.isEmpty() ? description.modelName : description.displayName;
    QStringList       services;
    for (const QString &service : description.services)
        services.append(service);
    services.sort(Qt::CaseInsensitive);

    QLOG() << "processDeviceDescription:";
    QLOG() << "  roomName:   " << description.roomName;
    QLOG() << "  modelName:  " << displayName << " (" << description.modelName << ")";
    QLOG() << "  version:    " << description.displayVersion << " (" << description.softwareVersion << ")";
    QLOG() << "  zoneType:   " << description.zoneType;
    if (!description.features.isEmpty())
        QLOG() << "  features:   " << description.features.join(QStringLiteral(", "));
    QLOG() << "  services:   " << services.join(QStringLiteral(", "));
}

QString zoneDisplayName(const ZonePlayer *zone)
{
    if (!zone)
        return {};
    return zone->displayName().isEmpty() ? zone->modelName() : zone->displayName();
}

bool isUsableReadyCoordinator(const ZonePlayer *zone)
{
    if (!zone || !zone->ready() || zone->invisible() || !zone->isCoordinator())        return false;
    if (zone->modelName().compare (QStringLiteral("DOCK"), Qt::CaseInsensitive) == 0)  return false;
    if (zone->modelName().contains(QStringLiteral("BRIDGE"), Qt::CaseInsensitive))     return false;
    if (zone->modelName().contains(QStringLiteral("Sub"), Qt::CaseInsensitive))        return false;
    return true;
}

// ZoneGroupState arrives as compact, unindented XML -- reformat it for
// logging via the standard Qt round-trip (read token-by-token, write back
// out with auto-formatting on) rather than trying to hand-indent it.
QString prettyPrintXml(const QByteArray &xml)
{
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

    return output;
}

// GetHouseholdID can fail transiently during the SSDP flood (a dozen+
// zones all being hit with device_description/GetHouseholdID/etc requests
// at once), and until *some* zone succeeds, nothing downstream can proceed
// -- no topology zone can be picked (topology selection skips zones with
// no household ID) and ThirdPartyMediaServersX can't be decrypted.
// Retrying (rather than the one-shot attempt this used to be) matters more
// here than almost anywhere else in discovery.
constexpr int kHouseholdIdRetrySeconds = 5;
} // namespace

ZoneDiscovery::ZoneDiscovery(QNetworkAccessManager *netMgr, QObject *parent) : QObject(parent), m_netMgr(netMgr)
{
    connect(&m_ssdp, &Ssdp::discovered, this, &ZoneDiscovery::onSsdpDiscovered);
    connect(&m_ssdp, &Ssdp::timeout, this, &ZoneDiscovery::onSsdpTimeout);
    connect(&m_ssdp, &Ssdp::socketErrorOccurred, this, [](QAbstractSocket::SocketError, const QString &errorString) {
        QWARN() << "SSDP socket warning (non-fatal):" << errorString;
    });

    m_eventing.listen();
    connect(&m_eventing, &ZoneEventing::topologySubscriptionZonePicked, this,
            &ZoneDiscovery::topologySubscriptionZonePicked);
    connect(&m_eventing, &ZoneEventing::topologyStateReceived, this, &ZoneDiscovery::parseZoneGroupState);
    connect(&m_eventing, &ZoneEventing::thirdPartyMediaServersXReceived, this,
            &ZoneDiscovery::thirdPartyMediaServersXReceived);
}

ZoneDiscovery::~ZoneDiscovery()
{
    m_eventing.unsubscribeAll();
}

bool ZoneDiscovery::start(quint16 localPort)
{
    m_localPort = localPort;

    if (!m_ssdp.listen(localPort))
    {
        QWARN() << "SSDP listen failed:" << m_ssdp.socketErrorString();
        return false;
    }

    m_ssdp.discover();
    return true;
}

void ZoneDiscovery::restart()
{
    QWARN() << "network change detected -- resetting" << m_zones.size() << "known zone(s) and restarting discovery";

    emit aboutToResetZones();

    m_eventing.unsubscribeAll();

    qDeleteAll(m_zones);
    m_zones.clear();
    m_householdId.clear();
    m_topologyZoneUdn.clear();
    m_readyCoordinatorUdn.clear();
    m_lastSsdpResponseLogTimeMs.clear();
    m_zoneCapabilitySummaryLogged = false;
    m_parsingZoneGroupState       = false;

    emit zoneListChanged();

    // Rebind, not just re-discover: see Ssdp::listen()/this method's own
    // header comment -- the socket's outbound multicast interface doesn't
    // survive an interface change (Ethernet unplugged, now Wi-Fi-only) on
    // its own, and every M-SEARCH send fails silently forever without this.
    if (!m_ssdp.listen(m_localPort))
        QWARN() << "SSDP re-listen failed during network-change restart:" << m_ssdp.socketErrorString();

    m_ssdp.discover();
}

ZonePlayer *ZoneDiscovery::zoneByRoomName(const QString &roomName) const
{
    for (ZonePlayer *zone : m_zones)
        if (zone->roomName().compare(roomName, Qt::CaseInsensitive) == 0)
            return zone;

    return nullptr;
}

void ZoneDiscovery::onSsdpDiscovered(const QString &fromAddr, const QMap<QString, QString> &headers)
{
    // USN looks like: uuid:RINCON_XXXXXXXXXXXX01400::urn:schemas-upnp-org:device:ZonePlayer:1
    const QString usn       = headers.value(QStringLiteral("USN"));
    const int     uuidStart = usn.indexOf(QStringLiteral("RINCON_"));
    if (uuidStart < 0)
        return;

    const int     separator = usn.indexOf(QStringLiteral("::"), uuidStart);
    const QString udn       = separator < 0 ? usn.mid(uuidStart) : usn.mid(uuidStart, separator - uuidStart);

    constexpr qint64 kSsdpResponseLogSuppressMs = 30 * 1000;
    const qint64     now                        = QDateTime::currentMSecsSinceEpoch();
    const qint64     lastLog                    = m_lastSsdpResponseLogTimeMs.value(udn, -kSsdpResponseLogSuppressMs);
    const bool       shouldLogResponse          = now - lastLog >= kSsdpResponseLogSuppressMs;

    if (shouldLogResponse)
    {
        // Full header dump. Repeated NOTIFY/M-SEARCH replies for already-known zones are noisy,
        // so the block is rate-limited per UDN while discovery behavior itself is unchanged.
        m_lastSsdpResponseLogTimeMs.insert(udn, now);        
        QLOG() << LOGSEPARATOR;
        QLOG() << "SSDP response from" << fromAddr;
        QLOG() << "  X-RINCON-HOUSEHOLD:" << headers.value(QStringLiteral("X-RINCON-HOUSEHOLD"));
        QLOG() << "  USN:" << usn;
        QLOG() << "  LOCATION:" << headers.value(QStringLiteral("LOCATION"));
    }

    // Future SSDP messages for a player already known (whether first seen
    // via SSDP or allocated from topology) are ignored.
    if (m_zones.contains(udn))
        return;

    const QString location     = headers.value(QStringLiteral("LOCATION"));
    const QString locationHost = QUrl(location).host();

    // A machine with more than one network path to the household (VPN,
    // multiple NICs, ...) can see the UDP reply arrive from one address
    // while the device's own LOCATION header names another -- LOCATION
    // (reported by the zone itself) is what's trusted for the zone's
    // actual address below, but the mismatch is worth surfacing since GENA
    // callback routing has to resolve the matching local address per-zone.
    if (!locationHost.isEmpty() && locationHost != fromAddr && normalizeIPv4(fromAddr) != locationHost)
    {
        QLOG() << "Subnet detected: SSDP reply from" << fromAddr << "but LOCATION reports" << locationHost;
    }
    else if (locationHost.isEmpty())
    {
        QWARN() << "SSDP reply with invalid/missing LOCATION:" << location;
    }

    const QString deviceIp = locationHost.isEmpty() ? normalizeIPv4(fromAddr) : locationHost;

    // QLOG() << "SSDP discovered" << udn << "at" << deviceIp;

    ZonePlayer *zone = allocateZone(deviceIp, udn);
    fetchDeviceDescription(zone);
}

void ZoneDiscovery::onSsdpTimeout()
{
    emit discoveryTimedOut();
}

ZonePlayer *ZoneDiscovery::allocateZone(const QString &deviceIp, const QString &udn)
{
    auto *zone = new ZonePlayer(m_netMgr, deviceIp, udn, this);
    m_zones.insert(udn, zone);
    m_zoneCapabilitySummaryLogged = false;
    emit zoneListChanged();
    return zone;
}

void ZoneDiscovery::fetchDeviceDescription(ZonePlayer *zone)
{
    const QString   udn      = zone->udn();
    const QString   deviceIp = zone->deviceIp();
    QNetworkRequest request{QUrl(zone->baseUrl() + QStringLiteral("xml/device_description.xml"))};
    QNetworkReply  *reply = m_netMgr->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, udn, deviceIp, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError)
        {
            QWARN() << "device_description fetch failed for" << deviceIp << reply->errorString();
            return;
        }

        ZonePlayer *zone = m_zones.value(udn);
        if (!zone)
            return;

        const DeviceDescription description = parseDeviceDescription(reply->readAll());
        zone->setRoomName(description.roomName);
        zone->setModelName(description.modelName);
        zone->setSerialNumber(description.serialNumber);
        zone->setDeviceDescriptionDetails(description.displayName, description.displayVersion,
                                          description.softwareVersion, description.zoneType, description.features);
        zone->setDeviceServices(description.services);

        logDeviceDescription(deviceIp, description);
        logZoneCapabilitySummaryWhenComplete();

        // deviceDescriptionUpdated, conceptually: HHID + device_description
        // are the two of three ready-conditions this resolves. The HHID
        // step is skipped entirely if this zone's SSDP X-RINCON-HOUSEHOLD
        // header (or an already-known household-wide ID from an earlier
        // zone) already supplied one -- see fetchHouseholdId's fast path.
        fetchHouseholdId(zone);
    });
}

void ZoneDiscovery::fetchHouseholdId(ZonePlayer *zone)
{
    if (!m_householdId.isEmpty())
    {
        zone->setHouseholdId(m_householdId);
        checkZoneReady(zone);
        updateTopologySubscriptionSelection();
        return;
    }

    // The zone's own household ID may already be known here (from its SSDP
    // X-RINCON-HOUSEHOLD header, captured at allocation time) even though
    // the household-wide m_householdId above isn't yet -- e.g. this is the
    // very first zone to finish its device_description fetch. Promote it
    // instead of making a redundant network round trip.
    if (!zone->householdId().isEmpty())
    {
        m_householdId = zone->householdId();
        checkZoneReady(zone);
        updateTopologySubscriptionSelection();
        return;
    }

    QNetworkReply *reply = zone->deviceProperties().GetHouseholdID();
    connect(reply, &QNetworkReply::finished, this, [this, udn = zone->udn(), roomName = zone->roomName(), reply]() {
        SoapResponse response(reply);
        reply->deleteLater();

        // Re-looked-up by UDN rather than closing over the ZonePlayer*
        // directly -- defensive against a future where zones can be torn
        // down mid-flight; currently they never are, but this is the same
        // capture-by-value-then-relookup pattern already used for the
        // catalog-fetch retry below.
        ZonePlayer *zone = m_zones.value(udn);
        if (!zone)
            return;

        if (response.error())
        {
            QWARN() << "GetHouseholdID failed for" << roomName << ":" << response.faultString() << "-- retrying in"
                    << kHouseholdIdRetrySeconds << "s";
            QTimer::singleShot(kHouseholdIdRetrySeconds * 1000, this, [this, udn]() {
                if (ZonePlayer *retryZone = m_zones.value(udn))
                    fetchHouseholdId(retryZone);
            });
            return;
        }

        m_householdId = response.value(QStringLiteral("CurrentHouseholdID"));
        zone->setHouseholdId(m_householdId);
        checkZoneReady(zone);
        updateTopologySubscriptionSelection();
    });
}

void ZoneDiscovery::checkZoneReady(ZonePlayer *zone)
{
    // All three conditions, whichever finishes last: HHID valid,
    // device_description loaded, topology processed.
    if (!zone->householdId().isEmpty() && !zone->roomName().isEmpty() && zone->hasValidTopology() && !zone->ready())
    {
        zone->setReady(true);
        emit zoneReady(zone);
        m_eventing.subscribeZoneEvents(zone);
        if (!zone->invisible() && zone->hasDeviceService(QStringLiteral("RenderingControl")))
        {
            zone->refreshVolume();
            zone->refreshMute();
        }
        if (!m_parsingZoneGroupState && isUsableReadyCoordinator(zone))
            publishReadyCoordinator(zone);
    }
}

void ZoneDiscovery::ensureVisibleZoneEventsAndRenderingState()
{
    for (ZonePlayer *zone : std::as_const(m_zones))
    {
        if (!zone || !zone->ready() || zone->invisible())
            continue;

        const bool needsEventSubscription =
            (zone->hasDeviceService(QStringLiteral("AVTransport")) && !zone->avTransport().subscribed()) ||
            (zone->hasDeviceService(QStringLiteral("RenderingControl")) && !zone->renderingControl().subscribed()) ||
            (zone->hasDeviceService(QStringLiteral("ContentDirectory")) && !zone->contentDirectory().subscribed()) ||
            (zone->hasDeviceService(QStringLiteral("AudioIn")) && !zone->audioIn().subscribed());
        if (needsEventSubscription)
        {
            QLOG() << "visible ready zone needs event subscription:" << zone->roomName();
            m_eventing.subscribeZoneEvents(zone);
        }

        if (zone->hasDeviceService(QStringLiteral("RenderingControl")) && (!zone->volumeKnown() || !zone->muteKnown()))
        {
            QLOG() << "visible ready zone needs RenderingControl state:" << zone->roomName();
            zone->refreshVolume();
            zone->refreshMute();
        }
    }
}

ZonePlayer *ZoneDiscovery::findReadyCoordinator() const
{
    if (ZonePlayer *current = m_zones.value(m_readyCoordinatorUdn))
    {
        if (isUsableReadyCoordinator(current))
            return current;
    }

    for (ZonePlayer *zone : std::as_const(m_zones))
    {
        if (!isUsableReadyCoordinator(zone))
            continue;
        return zone;
    }

    return nullptr;
}

void ZoneDiscovery::publishReadyCoordinator(ZonePlayer *zone)
{
    Q_ASSERT(zone);
    Q_ASSERT(isUsableReadyCoordinator(zone));

    if (m_readyCoordinatorUdn == zone->udn())
        return;

    m_readyCoordinatorUdn = zone->udn();
    QLOG() << "ready coordinator selected:"
           << QStringLiteral("%1 (%2 %3)").arg(zone->roomName(), zone->deviceIp(), zone->udn());
    emit readyCoordinator(zone);
}

void ZoneDiscovery::updateReadyCoordinatorSelection()
{
    if (ZonePlayer *zone = findReadyCoordinator())
    {
        publishReadyCoordinator(zone);
        return;
    }

    m_readyCoordinatorUdn.clear();
}

ZonePlayer *ZoneDiscovery::findTopologySubscriptionCandidate() const
{
    for (ZonePlayer *zone : std::as_const(m_zones))
    {
        if (zone->householdId().isEmpty())
            continue;
        if (zone->modelName().compare(QStringLiteral("DOCK"), Qt::CaseInsensitive) == 0)
            continue;
        if (zone->modelName().contains(QStringLiteral("BRIDGE"), Qt::CaseInsensitive))
            continue;
        // A Sub is always a bonded satellite, never an independently
        // controllable zone -- confirmed empirically that its
        // MusicServices control point doesn't implement
        // ListAvailableServices properly (a bare, bodyless HTTP 500, not
        // even a well-formed UPnP fault). If SSDP timing ever let one win
        // the race to be picked here, every music-service catalog fetch
        // for the whole household would fail against it forever (the
        // retry always re-targets the same topology zone). Model name
        // can't catch every kind of bonded satellite -- a stereo-pair
        // slave reports the same model as a standalone speaker -- but a
        // Sub is unambiguous and worth ruling out up front.
        if (zone->modelName().contains(QStringLiteral("Sub"), Qt::CaseInsensitive))
            continue;

        return zone;
    }

    return nullptr;
}

void ZoneDiscovery::selectTopologySubscriptionZone(ZonePlayer *zone)
{
    Q_ASSERT(zone);
    Q_ASSERT(!zone->householdId().isEmpty());

    m_topologyZoneUdn = zone->udn();
    QLOG() << "picked topology zone" << m_topologyZoneUdn;
    m_eventing.subscribeTopology(zone);
    refreshTopology();
}

void ZoneDiscovery::updateTopologySubscriptionSelection()
{
    if (!m_topologyZoneUdn.isEmpty())
        return;

    if (ZonePlayer *zone = findTopologySubscriptionCandidate())
        selectTopologySubscriptionZone(zone);
}

void ZoneDiscovery::refreshTopology()
{
    ZonePlayer *topologyZoneVal = m_zones.value(m_topologyZoneUdn);
    if (!topologyZoneVal)
        return;

    QNetworkReply *reply = topologyZoneVal->zoneGroupTopology().GetZoneGroupState();
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        SoapResponse response(reply);
        reply->deleteLater();

        if (response.error())
        {
            QWARN() << "GetZoneGroupState failed:" << response.faultString();
            return;
        }

        parseZoneGroupState(response.value(QStringLiteral("ZoneGroupState")).toUtf8());
    });
}

void ZoneDiscovery::parseZoneGroupState(const QByteArray &xmlBody)
{
    m_parsingZoneGroupState = true;

    if (verboseLoggingEnabled())
    {
        QLOG() << "ZoneGroupState XML:";
        QLOG() << prettyPrintXml(xmlBody);
    }

    const XmlDoc doc = XmlDoc::parse(xmlBody);

    for (const XmlNode &group : doc.all("//ZoneGroup"))
    {
        const QString coordinatorUdn = group.attr("Coordinator");

        for (const XmlNode &member : group.children("ZoneGroupMember"))
        {
            const QString udn             = member.attr("UUID");
            const QString location        = member.attr("Location");
            const QString roomName        = member.attr("ZoneName");
            const bool    invisible       = member.attrBool01("Invisible");
            const QString softwareVersion = member.attr("SoftwareVersion");
            if (SoapRequest::userAgent().isEmpty() && !softwareVersion.isEmpty())
            {
                // BB10 set the SOAP User-Agent from ZoneGroupState's real
                // Sonos SoftwareVersion before browsing SMAPI services.
                // Keep that behavior so modern partner endpoints do not
                // see a stale/fake controller version.
                SoapRequest::setUserAgent(
                    QStringLiteral("Linux UPnP/1.0 Sonos/%1 (WDCR:Microsoft Windows NT 10.0.22631)")
                        .arg(softwareVersion));
            }

            if (!udn.isEmpty())
            {
                // Allocate any ZonePlayer not yet discovered via SSDP; future
                // SSDP messages for it are ignored (see onSsdpDiscovered).
                ZonePlayer *zone = m_zones.value(udn);
                if (!zone)
                {
                    zone = allocateZone(QUrl(location).host(), udn);
                    fetchDeviceDescription(zone);
                }

                if (!roomName.isEmpty())
                    zone->setRoomName(roomName);

                zone->setCoordinatorUdn(coordinatorUdn);
                zone->setInvisible(invisible);
                zone->setHasValidTopology(true);
                checkZoneReady(zone);
            }

            // Bonded satellites (SUB, surround L/R, stereo-pair slave) are
            // nested *inside* their primary ZoneGroupMember as <Satellite>
            // children -- not siblings, and not flagged Invisible at this
            // level -- so this member has to be descended into (not
            // skipped) to find and hide them.
            for (const XmlNode &sat : member.children("Satellite"))
            {
                const QString satUdn      = sat.attr("UUID");
                const QString satLocation = sat.attr("Location");
                const QString satRoomName = sat.attr("ZoneName");

                if (satUdn.isEmpty())
                    continue;

                ZonePlayer *satellite = m_zones.value(satUdn);
                if (!satellite)
                {
                    satellite = allocateZone(QUrl(satLocation).host(), satUdn);
                    fetchDeviceDescription(satellite);
                }

                if (!satRoomName.isEmpty())
                    satellite->setRoomName(satRoomName);

                // Bonded to its primary zone player -- not a play-group
                // coordinator in the synced-playback sense, but the right
                // relationship for a satellite. invisible() is what
                // actually excludes it from the UI.
                satellite->setCoordinatorUdn(udn);
                satellite->setInvisible(true);
                satellite->setHasValidTopology(true);
                checkZoneReady(satellite);
            }
        }
    }

    m_parsingZoneGroupState = false;

    // Matches roomtunes-bb10's "processZone:" summary table -- one line
    // per known zone reflecting exactly what this parse just set. A zone
    // discovered here for the first time (rather than via SSDP) hasn't had
    // its device_description fetched yet, so roomName/modelName can be
    // blank the very first time this logs for it.
    QList<ZonePlayer *> zones = m_zones.values();
    std::sort(zones.begin(), zones.end(), [](ZonePlayer *a, ZonePlayer *b) {
        return a->roomName().compare(b->roomName(), Qt::CaseInsensitive) < 0;
    });

    QLOG() << LOGSEPARATOR;
    QLOG() << "processZone:" << zones.size() << "zone(s)";
    for (ZonePlayer *zone : std::as_const(zones))
    {
        QLOG() << QStringLiteral("  %1 coordinator=%2 invisible=%3 topology=%4 %5")
                      .arg(zone->roomName(), -24)
                      .arg(zone->isCoordinator() ? 1 : 0)
                      .arg(zone->invisible() ? 1 : 0)
                      .arg(zone->hasValidTopology() ? 1 : 0)
                      .arg(zone->modelName());
    }
    logZoneCapabilitySummaryWhenComplete();
    updateReadyCoordinatorSelection();
    ensureVisibleZoneEventsAndRenderingState();
}

bool ZoneDiscovery::zoneCapabilitySummaryAvailable() const
{
    if (m_zoneCapabilitySummaryLogged || m_zones.isEmpty())
        return false;

    for (ZonePlayer *zone : m_zones)
    {
        if (!zone->hasValidTopology() || zone->modelName().isEmpty())
            return false;
    }

    return true;
}

void ZoneDiscovery::logZoneCapabilitySummary()
{
    Q_ASSERT(zoneCapabilitySummaryAvailable());

    QList<ZonePlayer *> zones = m_zones.values();
    std::sort(zones.begin(), zones.end(), [](ZonePlayer *a, ZonePlayer *b) {
        return a->roomName().compare(b->roomName(), Qt::CaseInsensitive) < 0;
    });

    QLOG() << LOGSEPARATOR;
    QLOG() << "zone capability summary:" << zones.size() << "zone(s)";
    QLOG() << QStringLiteral("  %1 %2 %3 %4 %5 %6 %7 %8")
                  .arg(QStringLiteral("room"), -18)
                  .arg(QStringLiteral("model"), -16)
                  .arg(QStringLiteral("type"), -6)
                  .arg(QStringLiteral("feature1"), -10)
                  .arg(QStringLiteral("feature2"), -10)
                  .arg(QStringLiteral("feature3"), -10)
                  .arg(QStringLiteral("feature4"), -10)
                  .arg(QStringLiteral("feature5"), -10);
    for (ZonePlayer *zone : std::as_const(zones))
    {
        QLOG() << QStringLiteral("  %1 %2 %3 %4 %5 %6 %7 %8")
                      .arg(zone->roomName(), -18)
                      .arg(zoneDisplayName(zone), -16)
                      .arg(zone->zoneType(), -6)
                      .arg(featureAt(zone, 0), -10)
                      .arg(featureAt(zone, 1), -10)
                      .arg(featureAt(zone, 2), -10)
                      .arg(featureAt(zone, 3), -10)
                      .arg(featureAt(zone, 4), -10);
    }
    m_zoneCapabilitySummaryLogged = true;
    QLOG() << LOGSEPARATOR;
}

void ZoneDiscovery::logZoneCapabilitySummaryWhenComplete()
{
    if (zoneCapabilitySummaryAvailable())
        logZoneCapabilitySummary();
}

} // namespace RoomTunes
