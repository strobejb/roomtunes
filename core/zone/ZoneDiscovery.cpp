#include "ZoneDiscovery.h"

#include <algorithm>

#include <QHostAddress>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUdpSocket>
#include <QUrl>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "../Logging.h"
#include "../upnp/Soap.h"
#include "../upnp/SoapResponse.h"
#include "../xml/XmlUtils.h"

#define QLOG_CATEGORY logDiscovery
static const QString LOGSEPARATOR(80, QLatin1Char('-'));

namespace RoomTunes {

namespace {
// The UDP source address of an SSDP reply can come back as an IPv4-mapped
// IPv6 address ("::ffff:192.168.1.x") depending on the platform's socket
// stack, which isn't valid to embed directly in an "http://<ip>:1400/" URL.
// Only used as a fallback when a reply is missing its LOCATION header.
QString normalizeIPv4(const QString &address)
{
    bool ok = false;
    const quint32 ipv4 = QHostAddress(address).toIPv4Address(&ok);
    return ok ? QHostAddress(ipv4).toString() : address;
}

// What local address would the OS use to reach peerHost? Same trick as
// Ssdp's interface picker: "connecting" a UDP socket does no I/O (UDP has
// no handshake), it just resolves a route -- this is exactly the address a
// GENA callback URL needs so the zone can call us back. Resolved per-peer
// (not a single fixed "our address") because a machine with more than one
// network path to the household (see the subnet-detection log in
// onSsdpDiscovered) needs a different local address depending on which
// zone it's talking to for the callback to actually be reachable.
QString localAddressForPeer(const QString &peerHost)
{
    QUdpSocket probe;
    probe.connectToHost(peerHost, 1400);
    probe.waitForConnected(200);
    const QString local = probe.localAddress().toString();
    probe.close();
    return local;
}

// GENA NOTIFY bodies are a <e:propertyset> of one or more <e:property>
// elements, each wrapping a single differently-named child (e.g.
// <ZoneGroupState>...</ZoneGroupState>). Finds the value of one by name.
QString extractGenaProperty(const QByteArray &body, const QString &propertyName)
{
    QXmlStreamReader xml(body);

    while (!xml.atEnd()) {
        if (!xml.readNextStartElement())
            continue;

        // Don't skip non-"property" elements -- the root <e:propertyset>
        // itself doesn't match "property" either, and needs descending into
        // (via the next bare readNextStartElement()) rather than skipping.
        if (xml.name() != QLatin1String("property"))
            continue;

        if (xml.readNextStartElement()) {
            if (xml.name() == propertyName)
                return xml.readElementText(QXmlStreamReader::SkipChildElements);
            xml.skipCurrentElement();
        }
    }

    return {};
}

// ZoneGroupState arrives as compact, unindented XML -- reformat it for
// logging via the standard Qt round-trip (read token-by-token, write back
// out with auto-formatting on) rather than trying to hand-indent it.
QString prettyPrintXml(const QByteArray &xml)
{
    QString output;
    QXmlStreamReader reader(xml);
    QXmlStreamWriter writer(&output);
    writer.setAutoFormatting(true);
    writer.setAutoFormattingIndent(2);

    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.hasError())
            break;
        if (reader.tokenType() != QXmlStreamReader::StartDocument)
            writer.writeCurrentToken(reader);
    }

    return output;
}

constexpr int kTopologySubscriptionTimeoutSeconds = 3600;

// GetHouseholdID can fail transiently during the SSDP flood (a dozen+
// zones all being hit with device_description/GetHouseholdID/etc requests
// at once), and until *some* zone succeeds, nothing downstream can proceed
// -- no topology zone can be picked (maybePickTopologyZone skips zones
// with no household ID) and ThirdPartyMediaServersX can't be decrypted.
// Retrying (rather than the one-shot attempt this used to be) matters more
// here than almost anywhere else in discovery.
constexpr int kHouseholdIdRetrySeconds = 5;
}

ZoneDiscovery::ZoneDiscovery(QNetworkAccessManager *netMgr, QObject *parent)
    : QObject(parent)
    , m_netMgr(netMgr)
{
    connect(&m_ssdp, &Ssdp::discovered, this, &ZoneDiscovery::onSsdpDiscovered);
    connect(&m_ssdp, &Ssdp::timeout, this, &ZoneDiscovery::onSsdpTimeout);
    connect(&m_ssdp, &Ssdp::socketErrorOccurred, this, [](QAbstractSocket::SocketError, const QString &errorString) {
        QWARN() << "SSDP socket warning (non-fatal):" << errorString;
    });

    m_notifyServer.listen();
    connect(&m_notifyServer, &GenaNotifyServer::notified, this, &ZoneDiscovery::onGenaNotify);

    // Renew comfortably before the subscription would otherwise expire.
    m_topologyRenewTimer.setInterval(int(kTopologySubscriptionTimeoutSeconds * 0.6 * 1000));
    connect(&m_topologyRenewTimer, &QTimer::timeout, this, &ZoneDiscovery::renewTopologySubscription);
    m_zoneEventRenewTimer.setInterval(int(kTopologySubscriptionTimeoutSeconds * 0.6 * 1000));
    connect(&m_zoneEventRenewTimer, &QTimer::timeout, this, &ZoneDiscovery::renewZoneEventSubscriptions);
}

ZoneDiscovery::~ZoneDiscovery()
{
    unsubscribeZoneEvents();

    ZonePlayer *topologyZoneVal = m_zones.value(m_topologyZoneUdn);
    if (topologyZoneVal && topologyZoneVal->zoneGroupTopology().subscribed())
        topologyZoneVal->zoneGroupTopology().unsubscribe();
}

bool ZoneDiscovery::start(quint16 localPort)
{
    m_localPort = localPort;

    if (!m_ssdp.listen(localPort)) {
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

    ZonePlayer *topologyZoneVal = m_zones.value(m_topologyZoneUdn);
    if (topologyZoneVal && topologyZoneVal->zoneGroupTopology().subscribed())
        topologyZoneVal->zoneGroupTopology().unsubscribe();
    unsubscribeZoneEvents();

    m_topologyRenewTimer.stop();

    qDeleteAll(m_zones);
    m_zones.clear();
    m_householdId.clear();
    m_topologyZoneUdn.clear();
    m_topologySubscriptionSid.clear();
    m_zoneEventSubscriptions.clear();

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
    const QString usn = headers.value(QStringLiteral("USN"));
    const int uuidStart = usn.indexOf(QStringLiteral("RINCON_"));
    if (uuidStart < 0)
        return;

    // Full header dump, matching roomtunes-bb10's own SSDP response block
    // (household ID/USN/LOCATION) -- useful on its own for diagnosing a
    // zone that never gets past discovery (wrong household, unreachable
    // LOCATION, ...), not just the one-line summary below.
    QLOG() << LOGSEPARATOR;
    QLOG() << "SSDP response from" << fromAddr;
    QLOG() << "  X-RINCON-HOUSEHOLD:" << headers.value(QStringLiteral("X-RINCON-HOUSEHOLD"));
    QLOG() << "  USN:" << usn;
    QLOG() << "  LOCATION:" << headers.value(QStringLiteral("LOCATION"));
    const int separator = usn.indexOf(QStringLiteral("::"), uuidStart);
    const QString udn = separator < 0 ? usn.mid(uuidStart) : usn.mid(uuidStart, separator - uuidStart);

    // Future SSDP messages for a player already known (whether first seen
    // via SSDP or allocated from topology) are ignored.
    if (m_zones.contains(udn))
        return;

    const QString location = headers.value(QStringLiteral("LOCATION"));
    const QString locationHost = QUrl(location).host();

    // A machine with more than one network path to the household (VPN,
    // multiple NICs, ...) can see the UDP reply arrive from one address
    // while the device's own LOCATION header names another -- LOCATION
    // (reported by the zone itself) is what's trusted for the zone's
    // actual address below, but the mismatch is worth surfacing since it's
    // exactly the situation localAddressForPeer() exists to handle
    // correctly per-zone rather than assuming one fixed local address.
    if (!locationHost.isEmpty() && locationHost != fromAddr && normalizeIPv4(fromAddr) != locationHost) {
        QLOG() << "Subnet detected: SSDP reply from" << fromAddr << "but LOCATION reports" << locationHost;
    } else if (locationHost.isEmpty()) {
        QWARN() << "SSDP reply with invalid/missing LOCATION:" << location;
    }

    const QString deviceIp = locationHost.isEmpty() ? normalizeIPv4(fromAddr) : locationHost;

    //QLOG() << "SSDP discovered" << udn << "at" << deviceIp;

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
    emit zoneListChanged();
    return zone;
}

void ZoneDiscovery::fetchDeviceDescription(ZonePlayer *zone)
{
    const QString udn = zone->udn();
    const QString deviceIp = zone->deviceIp();
    QNetworkRequest request{QUrl(zone->baseUrl() + QStringLiteral("xml/device_description.xml"))};
    QNetworkReply *reply = m_netMgr->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, udn, deviceIp, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            QWARN() << "device_description fetch failed for" << deviceIp << reply->errorString();
            return;
        }

        ZonePlayer *zone = m_zones.value(udn);
        if (!zone)
            return;

        QXmlStreamReader xml(reply->readAll());

        while (!xml.atEnd()) {
            if (!xml.readNextStartElement())
                continue;
            if (xml.name() != QLatin1String("device"))
                continue;

            const QMap<QString, QString> fields = flattenElement(xml);
            zone->setRoomName(fields.value(QStringLiteral("roomName")));
            zone->setModelName(fields.value(QStringLiteral("modelName")));
            zone->setSerialNumber(fields.value(QStringLiteral("serialNum")));
            break;
        }

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
    if (!m_householdId.isEmpty()) {
        zone->setHouseholdId(m_householdId);
        checkZoneReady(zone);
        maybePickTopologyZone();
        return;
    }

    // The zone's own household ID may already be known here (from its SSDP
    // X-RINCON-HOUSEHOLD header, captured at allocation time) even though
    // the household-wide m_householdId above isn't yet -- e.g. this is the
    // very first zone to finish its device_description fetch. Promote it
    // instead of making a redundant network round trip.
    if (!zone->householdId().isEmpty()) {
        m_householdId = zone->householdId();
        checkZoneReady(zone);
        maybePickTopologyZone();
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

        if (response.error()) {
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
        maybePickTopologyZone();
    });
}

void ZoneDiscovery::checkZoneReady(ZonePlayer *zone)
{
    // All three conditions, whichever finishes last: HHID valid,
    // device_description loaded, topology processed.
    if (!zone->householdId().isEmpty() && !zone->roomName().isEmpty() && zone->hasValidTopology() && !zone->ready()) {
        zone->setReady(true);
        emit zoneReady(zone);
        subscribeZoneEvents(zone);
    }
}

void ZoneDiscovery::maybePickTopologyZone()
{
    if (!m_topologyZoneUdn.isEmpty())
        return;

    for (ZonePlayer *zone : std::as_const(m_zones)) {
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

        m_topologyZoneUdn = zone->udn();
        QLOG() << "picked topology zone" << m_topologyZoneUdn;
        subscribeTopology();
        break;
    }
}

void ZoneDiscovery::subscribeTopology()
{
    ZonePlayer *topologyZoneVal = m_zones.value(m_topologyZoneUdn);
    if (!topologyZoneVal)
        return;

    // Fired right here -- before the SUBSCRIBE reply even comes back --
    // since by this point m_householdId is already guaranteed non-empty
    // (maybePickTopologyZone only ever picks a zone once its household ID
    // is known) and topologyZoneVal is now a reachable, known-good zone.
    // Household uses this as the single trigger for its one-time
    // music-service catalog/icon/serial fetches, and as one of the two
    // retry points for ThirdPartyMediaServersX processing (the other being
    // whenever a fresh TPMSX payload itself arrives) -- see the class
    // comment on why both a household ID AND a topology zone have to be in
    // hand before either of those can proceed.
    emit topologyZonePicked(topologyZoneVal);

    const QString localAddress = localAddressForPeer(topologyZoneVal->deviceIp());
    QLOG() << "ZoneGroupTopology SUBSCRIBE to" << topologyZoneVal->deviceIp() << "callback" << localAddress
           << m_notifyServer.port();
    const QString topologyZoneUdn = topologyZoneVal->udn();
    QNetworkReply *reply = topologyZoneVal->zoneGroupTopology().subscribe(localAddress, m_notifyServer.port(),
                                                                           kTopologySubscriptionTimeoutSeconds);

    connect(reply, &QNetworkReply::finished, this, [this, reply, topologyZoneUdn, topologyZoneIp = topologyZoneVal->deviceIp()]() {
        reply->deleteLater();
        const ScopedLogEndpoint logEndpoint(topologyZoneIp, LogDirection::Outbound);

        if (reply->error() != QNetworkReply::NoError) {
            QWARN() << "ZoneGroupTopology subscribe failed:"
                    << reply->errorString() << "-- falling back to a one-shot GetZoneGroupState";
            refreshTopology();
            return;
        }

        ZonePlayer *topologyZoneVal = m_zones.value(topologyZoneUdn);
        if (!topologyZoneVal)
            return;

        const QString sid = QString::fromUtf8(reply->rawHeader("SID"));
        m_topologySubscriptionSid = sid;
        topologyZoneVal->zoneGroupTopology().setSid(sid);

        QLOG() << "subscribed to ZoneGroupTopology, SID=" << sid;

        m_topologyRenewTimer.start();

        // UPnP eventing requires the publisher to send an initial NOTIFY
        // with the current state right after a successful SUBSCRIBE, so no
        // separate poll is needed here -- but if that initial NOTIFY is
        // ever missed, refreshTopology() remains available as a manual /
        // fallback re-poll.
    });
}

void ZoneDiscovery::renewTopologySubscription()
{
    ZonePlayer *topologyZoneVal = m_zones.value(m_topologyZoneUdn);
    if (!topologyZoneVal || !topologyZoneVal->zoneGroupTopology().subscribed())
        return;

    const QString localAddress = localAddressForPeer(topologyZoneVal->deviceIp());
    const QString topologyZoneUdn = topologyZoneVal->udn();
    QNetworkReply *reply = topologyZoneVal->zoneGroupTopology().subscribe(localAddress, m_notifyServer.port(),
                                                                           kTopologySubscriptionTimeoutSeconds);

    connect(reply, &QNetworkReply::finished, this, [this, reply, topologyZoneUdn, topologyZoneIp = topologyZoneVal->deviceIp()]() {
        reply->deleteLater();
        const ScopedLogEndpoint logEndpoint(topologyZoneIp, LogDirection::Outbound);

        if (reply->error() != QNetworkReply::NoError) {
            QWARN() << "ZoneGroupTopology resubscribe failed:" << reply->errorString();
            return;
        }

        ZonePlayer *topologyZoneVal = m_zones.value(topologyZoneUdn);
        if (!topologyZoneVal)
            return;

        const QString sid = QString::fromUtf8(reply->rawHeader("SID"));
        m_topologySubscriptionSid = sid;
        topologyZoneVal->zoneGroupTopology().setSid(sid);
    });
}

void ZoneDiscovery::subscribeZoneEvents(ZonePlayer *zone)
{
    if (!zone || zone->invisible())
        return;

    subscribeZoneEvent(zone, zone->avTransport(), ZoneEventService::AVTransport);
    subscribeZoneEvent(zone, zone->renderingControl(), ZoneEventService::RenderingControl);
    subscribeZoneEvent(zone, zone->contentDirectory(), ZoneEventService::ContentDirectory);
    subscribeZoneEvent(zone, zone->audioIn(), ZoneEventService::AudioIn);
}

void ZoneDiscovery::subscribeZoneEvent(ZonePlayer *zone, UpnpService &service, ZoneEventService serviceType)
{
    const QString localAddress = localAddressForPeer(zone->deviceIp());
    const QString udn = zone->udn();
    const QString oldSid = service.sid();
    QNetworkReply *reply = service.subscribe(localAddress, m_notifyServer.port(), kTopologySubscriptionTimeoutSeconds);

    connect(reply, &QNetworkReply::finished, this, [this, reply, udn, oldSid, serviceType, serviceName = service.serviceName()]() {
        reply->deleteLater();

        ZonePlayer *zone = m_zones.value(udn);
        if (!zone)
            return;
        const ScopedLogEndpoint logEndpoint(zone->deviceIp(), LogDirection::Outbound);

        UpnpService *service = nullptr;
        switch (serviceType) {
        case ZoneEventService::AVTransport:
            service = &zone->avTransport();
            break;
        case ZoneEventService::RenderingControl:
            service = &zone->renderingControl();
            break;
        case ZoneEventService::ContentDirectory:
            service = &zone->contentDirectory();
            break;
        case ZoneEventService::AudioIn:
            service = &zone->audioIn();
            break;
        }

        if (reply->error() != QNetworkReply::NoError) {
            QWARN() << zone->roomName() << serviceName << "subscribe failed:" << reply->errorString();
            return;
        }

        const QString sid = QString::fromUtf8(reply->rawHeader("SID"));
        if (sid.isEmpty()) {
            QWARN() << zone->roomName() << serviceName << "subscribe returned no SID";
            return;
        }

        if (!oldSid.isEmpty() && oldSid != sid)
            m_zoneEventSubscriptions.remove(oldSid);

        service->setSid(sid);
        m_zoneEventSubscriptions.insert(sid, ZoneEventSubscription{udn, serviceType, serviceName});
        m_zoneEventRenewTimer.start();
        QLOG() << "SUBSCRIBED:" << sid << serviceName;
    });
}

void ZoneDiscovery::renewZoneEventSubscriptions()
{
    for (ZonePlayer *zone : std::as_const(m_zones)) {
        if (zone && zone->ready())
            subscribeZoneEvents(zone);
    }
}

void ZoneDiscovery::unsubscribeZoneEvents()
{
    m_zoneEventRenewTimer.stop();

    for (ZonePlayer *zone : std::as_const(m_zones)) {
        if (!zone)
            continue;
        if (zone->avTransport().subscribed())
            zone->avTransport().unsubscribe();
        if (zone->renderingControl().subscribed())
            zone->renderingControl().unsubscribe();
        if (zone->contentDirectory().subscribed())
            zone->contentDirectory().unsubscribe();
        if (zone->audioIn().subscribed())
            zone->audioIn().unsubscribe();
    }

    m_zoneEventSubscriptions.clear();
}

void ZoneDiscovery::routeZoneEvent(const ZoneEventSubscription &subscription, const QByteArray &body)
{
    ZonePlayer *zone = m_zones.value(subscription.zoneUdn);
    if (!zone)
        return;

    switch (subscription.service) {
    case ZoneEventService::AVTransport:
        zone->handleAVTransportEvent(body);
        break;
    case ZoneEventService::RenderingControl:
        zone->handleRenderingControlEvent(body);
        break;
    case ZoneEventService::ContentDirectory:
        zone->handleContentDirectoryEvent(body);
        break;
    case ZoneEventService::AudioIn:
        zone->handleAudioInEvent(body);
        break;
    }
}

void ZoneDiscovery::onGenaNotify(const QString &peerAddress, const QString &sid, const QByteArray &body)
{
    const ScopedLogEndpoint logEndpoint(peerAddress, LogDirection::Inbound);
    const auto zoneEvent = m_zoneEventSubscriptions.constFind(sid);
    if (zoneEvent != m_zoneEventSubscriptions.constEnd()) {
        const ZoneEventSubscription subscription = zoneEvent.value();
        ZonePlayer *zone = m_zones.value(subscription.zoneUdn);
        QLOG() << "GENA NOTIFY " << subscription.serviceName
                    << sid
                    << (zone ? zone->roomName() : subscription.zoneUdn)
               << "(" << body.size() << "bytes)";

        routeZoneEvent(subscription, body);
        return;
    }

    if (sid != m_topologySubscriptionSid) {
        QLOG() << "GENA UNKNOWN " << sid << "(" << body.size() << "bytes)";
        return;
    }

    QLOG() << "GENA NOTIFY  ZoneGroupTopology " << sid << "(" << body.size() << "bytes)";

    const QString zoneGroupState = extractGenaProperty(body, QStringLiteral("ZoneGroupState"));
    if (zoneGroupState.isEmpty())
        QLOG() << "ZoneGroupTopology NOTIFY carried no ZoneGroupState property";
    else
        parseZoneGroupState(zoneGroupState.toUtf8());

    // Sent on the same event channel whenever the household's configured
    // music service accounts change (a login, a logout, a new device
    // link). Just forwarded, not processed here -- decrypting it needs the
    // household ID, which is a music-services concern handled by
    // Household, not discovery. Household re-attempts processing both when
    // this arrives AND when topologyZonePicked fires, since either could
    // be the one still missing when the other happens.
    const QString tpmsx = extractGenaProperty(body, QStringLiteral("ThirdPartyMediaServersX"));
    if (!tpmsx.isEmpty())
        emit thirdPartyMediaServersXReceived(tpmsx);
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

        if (response.error()) {
            QWARN() << "GetZoneGroupState failed:" << response.faultString();
            return;
        }

        parseZoneGroupState(response.value(QStringLiteral("ZoneGroupState")).toUtf8());
    });
}

void ZoneDiscovery::parseZoneGroupState(const QByteArray &xmlBody)
{
    if (verboseLoggingEnabled()) {
        QLOG() << "ZoneGroupState XML:";
        QLOG() << prettyPrintXml(xmlBody);
    }

    QXmlStreamReader xml(xmlBody);

    while (!xml.atEnd()) {
        if (!xml.readNextStartElement())
            continue;
        if (xml.name() != QLatin1String("ZoneGroup"))
            continue;

        const QString coordinatorUdn = xml.attributes().value(QStringLiteral("Coordinator")).toString();

        while (xml.readNextStartElement()) {
            if (xml.name() != QLatin1String("ZoneGroupMember")) {
                xml.skipCurrentElement();
                continue;
            }

            const QXmlStreamAttributes attrs = xml.attributes();
            const QString udn = attrs.value(QStringLiteral("UUID")).toString();
            const QString location = attrs.value(QStringLiteral("Location")).toString();
            const QString roomName = attrs.value(QStringLiteral("ZoneName")).toString();
            const bool invisible = attrs.value(QStringLiteral("Invisible")) == QStringLiteral("1");
            const QString softwareVersion = attrs.value(QStringLiteral("SoftwareVersion")).toString();
            if (SoapRequest::userAgent().isEmpty() && !softwareVersion.isEmpty()) {
                // BB10 set the SOAP User-Agent from ZoneGroupState's real
                // Sonos SoftwareVersion before browsing SMAPI services.
                // Keep that behavior so modern partner endpoints do not
                // see a stale/fake controller version.
                SoapRequest::setUserAgent(
                    QStringLiteral("Linux UPnP/1.0 Sonos/%1 (WDCR:Microsoft Windows NT 10.0.22631)").arg(softwareVersion));
            }

            if (!udn.isEmpty()) {
                // Allocate any ZonePlayer not yet discovered via SSDP; future
                // SSDP messages for it are ignored (see onSsdpDiscovered).
                ZonePlayer *zone = m_zones.value(udn);
                if (!zone) {
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
            while (xml.readNextStartElement()) {
                if (xml.name() != QLatin1String("Satellite")) {
                    xml.skipCurrentElement();
                    continue;
                }

                const QXmlStreamAttributes satAttrs = xml.attributes();
                const QString satUdn = satAttrs.value(QStringLiteral("UUID")).toString();
                const QString satLocation = satAttrs.value(QStringLiteral("Location")).toString();
                const QString satRoomName = satAttrs.value(QStringLiteral("ZoneName")).toString();

                xml.skipCurrentElement();

                if (satUdn.isEmpty())
                    continue;

                ZonePlayer *satellite = m_zones.value(satUdn);
                if (!satellite) {
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

    // Matches roomtunes-bb10's "processZone:" summary table -- one line
    // per known zone reflecting exactly what this parse just set. A zone
    // discovered here for the first time (rather than via SSDP) hasn't had
    // its device_description fetched yet, so roomName/modelName can be
    // blank the very first time this logs for it.
    QList<ZonePlayer *> zones = m_zones.values();
    std::sort(zones.begin(), zones.end(),
              [](ZonePlayer *a, ZonePlayer *b) { return a->roomName().compare(b->roomName(), Qt::CaseInsensitive) < 0; });

    QLOG() << LOGSEPARATOR;
    QLOG() << "processZone:" << zones.size() << "zone(s)";
    for (ZonePlayer *zone : std::as_const(zones)) {
        QLOG() << QStringLiteral("  %1 coordinator=%2 invisible=%3 topology=%4 %5")
                                 .arg(zone->roomName(), -24)
                                 .arg(zone->isCoordinator() ? 1 : 0)
                                 .arg(zone->invisible() ? 1 : 0)
                                 .arg(zone->hasValidTopology() ? 1 : 0)
                                 .arg(zone->modelName());
    }
}

}
