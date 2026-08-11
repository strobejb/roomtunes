#include "ZoneEventing.h"

#include <QNetworkReply>
#include <QUdpSocket>
#include <QXmlStreamReader>

#include "../Logging.h"
#include "../control/UpnpService.h"
#include "../zone/ZonePlayer.h"

#define QLOG_CATEGORY logEventing

namespace RoomTunes
{

namespace
{

constexpr int kSubscriptionTimeoutSeconds = 3600;

QString localAddressForPeer(const QString &peerHost)
{
    QUdpSocket probe;
    probe.connectToHost(peerHost, 1400);
    probe.waitForConnected(200);
    const QString local = probe.localAddress().toString();
    probe.close();
    return local;
}

QString extractGenaProperty(const QByteArray &body, const QString &propertyName)
{
    QXmlStreamReader xml(body);

    while (!xml.atEnd())
    {
        if (!xml.readNextStartElement())
            continue;

        // The root <e:propertyset> must be descended into; skipping every
        // non-property element here would also skip all of its children.
        if (xml.name() != QLatin1String("property"))
            continue;

        if (xml.readNextStartElement())
        {
            if (xml.name() == propertyName)
                return xml.readElementText(QXmlStreamReader::SkipChildElements);
            xml.skipCurrentElement();
        }
    }

    return {};
}

QString zoneEventSubscriptionKey(const QString &udn, const char *serviceName)
{
    return udn + QLatin1Char('|') + QLatin1String(serviceName);
}

} // namespace

ZoneEventing::ZoneEventing(QObject *parent) : QObject(parent)
{
    connect(&m_notifyServer, &GenaNotifyServer::notified, this, &ZoneEventing::onGenaNotify);

    // Renew comfortably before the subscription would otherwise expire.
    m_topologyRenewTimer.setInterval(int(kSubscriptionTimeoutSeconds * 0.6 * 1000));
    connect(&m_topologyRenewTimer, &QTimer::timeout, this, &ZoneEventing::renewTopologySubscription);
    m_zoneEventRenewTimer.setInterval(int(kSubscriptionTimeoutSeconds * 0.6 * 1000));
    connect(&m_zoneEventRenewTimer, &QTimer::timeout, this, &ZoneEventing::renewZoneEventSubscriptions);
}

ZoneEventing::~ZoneEventing()
{
    unsubscribeAll();
}

bool ZoneEventing::listen()
{
    return m_notifyServer.listen();
}

void ZoneEventing::subscribeTopology(ZonePlayer *zone)
{
    if (!zone)
        return;

    // Fired before the SUBSCRIBE reply comes back. The chosen zone is the
    // stable event endpoint other startup flows can depend on; callback
    // delivery timing is deliberately not part of discovery readiness.
    m_topologyZone = zone;
    emit topologySubscriptionZonePicked(zone);

    const QString localAddress = localAddressForPeer(zone->deviceIp());
    QLOG() << "ZoneGroupTopology SUBSCRIBE to" << zone->deviceIp() << "callback" << localAddress
           << m_notifyServer.port();

    QPointer<ZonePlayer> topologyZone = zone;
    QNetworkReply       *reply =
        zone->zoneGroupTopology().subscribe(localAddress, m_notifyServer.port(), kSubscriptionTimeoutSeconds);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, topologyZone]()
            {
                reply->deleteLater();
                if (!topologyZone)
                    return;

                const ScopedLogEndpoint logEndpoint(topologyZone->deviceIp(), LogDirection::Outbound);
                if (reply->error() != QNetworkReply::NoError)
                {
                    QWARN() << "ZoneGroupTopology subscribe failed:" << reply->errorString()
                            << "-- continuing with one-shot GetZoneGroupState refreshes";
                    return;
                }

                const QString sid         = QString::fromUtf8(reply->rawHeader("SID"));
                m_topologySubscriptionSid = sid;
                topologyZone->zoneGroupTopology().setSid(sid);
                QLOG() << "subscribed to ZoneGroupTopology, SID=" << sid;
                m_topologyRenewTimer.start();
            });
}

void ZoneEventing::renewTopologySubscription()
{
    ZonePlayer *zone = m_topologyZone.data();
    if (!zone || !zone->zoneGroupTopology().subscribed())
        return;

    const QString        localAddress = localAddressForPeer(zone->deviceIp());
    QPointer<ZonePlayer> topologyZone = zone;
    QNetworkReply       *reply =
        zone->zoneGroupTopology().subscribe(localAddress, m_notifyServer.port(), kSubscriptionTimeoutSeconds);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, topologyZone]()
            {
                reply->deleteLater();
                if (!topologyZone)
                    return;

                const ScopedLogEndpoint logEndpoint(topologyZone->deviceIp(), LogDirection::Outbound);
                if (reply->error() != QNetworkReply::NoError)
                {
                    QWARN() << "ZoneGroupTopology resubscribe failed:" << reply->errorString();
                    return;
                }

                const QString sid         = QString::fromUtf8(reply->rawHeader("SID"));
                m_topologySubscriptionSid = sid;
                topologyZone->zoneGroupTopology().setSid(sid);
            });
}

void ZoneEventing::subscribeZoneEvents(ZonePlayer *zone)
{
    if (!zone || zone->invisible())
        return;

    if (zone->hasDeviceService(QStringLiteral("AVTransport")))
        subscribeZoneEvent(zone, zone->avTransport(), ZoneEventService::AVTransport);
    if (zone->hasDeviceService(QStringLiteral("RenderingControl")))
        subscribeZoneEvent(zone, zone->renderingControl(), ZoneEventService::RenderingControl);
    if (zone->hasDeviceService(QStringLiteral("ContentDirectory")))
        subscribeZoneEvent(zone, zone->contentDirectory(), ZoneEventService::ContentDirectory);
    if (zone->hasDeviceService(QStringLiteral("AudioIn")))
        subscribeZoneEvent(zone, zone->audioIn(), ZoneEventService::AudioIn);
}

void ZoneEventing::subscribeZoneEvent(ZonePlayer *zone, UpnpService &service, ZoneEventService serviceType)
{
    const QString pendingKey = zoneEventSubscriptionKey(zone->udn(), service.serviceName());
    if (m_pendingZoneEventSubscriptions.contains(pendingKey))
        return;

    const QString localAddress = localAddressForPeer(zone->deviceIp());
    const QString oldSid       = service.sid();
    m_pendingZoneEventSubscriptions.insert(pendingKey);

    QPointer<ZonePlayer> targetZone = zone;
    QNetworkReply       *reply = service.subscribe(localAddress, m_notifyServer.port(), kSubscriptionTimeoutSeconds);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, targetZone, oldSid, serviceType, serviceName = service.serviceName(), pendingKey]()
            {
                reply->deleteLater();
                m_pendingZoneEventSubscriptions.remove(pendingKey);
                if (!targetZone)
                    return;

                const ScopedLogEndpoint logEndpoint(targetZone->deviceIp(), LogDirection::Outbound);

                UpnpService *service = nullptr;
                switch (serviceType)
                {
                case ZoneEventService::AVTransport:
                    service = &targetZone->avTransport();
                    break;
                case ZoneEventService::RenderingControl:
                    service = &targetZone->renderingControl();
                    break;
                case ZoneEventService::ContentDirectory:
                    service = &targetZone->contentDirectory();
                    break;
                case ZoneEventService::AudioIn:
                    service = &targetZone->audioIn();
                    break;
                }

                if (reply->error() != QNetworkReply::NoError)
                {
                    QWARN() << targetZone->roomName() << serviceName << "subscribe failed:" << reply->errorString();
                    return;
                }

                const QString sid = QString::fromUtf8(reply->rawHeader("SID"));
                if (sid.isEmpty())
                {
                    QWARN() << targetZone->roomName() << serviceName << "subscribe returned no SID";
                    return;
                }

                if (!oldSid.isEmpty() && oldSid != sid)
                    m_zoneEventSubscriptions.remove(oldSid);

                service->setSid(sid);
                m_zoneEventSubscriptions.insert(sid, ZoneEventSubscription{targetZone, serviceType, serviceName});
                m_zoneEventRenewTimer.start();
                QLOG() << "SUBSCRIBED:" << sid << serviceName;
            });
}

void ZoneEventing::renewZoneEventSubscriptions()
{
    QSet<ZonePlayer *> zones;
    for (const ZoneEventSubscription &subscription : std::as_const(m_zoneEventSubscriptions))
    {
        if (subscription.zone)
            zones.insert(subscription.zone.data());
    }

    for (ZonePlayer *zone : zones)
        subscribeZoneEvents(zone);
}

void ZoneEventing::unsubscribeAll()
{
    m_topologyRenewTimer.stop();
    m_zoneEventRenewTimer.stop();

    if (m_topologyZone && m_topologyZone->zoneGroupTopology().subscribed())
        m_topologyZone->zoneGroupTopology().unsubscribe();

    QSet<ZonePlayer *> zones;
    for (const ZoneEventSubscription &subscription : std::as_const(m_zoneEventSubscriptions))
    {
        if (subscription.zone)
            zones.insert(subscription.zone.data());
    }

    for (ZonePlayer *zone : zones)
    {
        if (zone->avTransport().subscribed())
            zone->avTransport().unsubscribe();
        if (zone->renderingControl().subscribed())
            zone->renderingControl().unsubscribe();
        if (zone->contentDirectory().subscribed())
            zone->contentDirectory().unsubscribe();
        if (zone->audioIn().subscribed())
            zone->audioIn().unsubscribe();
    }

    m_topologyZone.clear();
    m_topologySubscriptionSid.clear();
    m_zoneEventSubscriptions.clear();
    m_pendingZoneEventSubscriptions.clear();
}

void ZoneEventing::routeZoneEvent(const ZoneEventSubscription &subscription, const QByteArray &body)
{
    ZonePlayer *zone = subscription.zone.data();
    if (!zone)
        return;

    switch (subscription.service)
    {
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

void ZoneEventing::onGenaNotify(const QString &peerAddress, const QString &sid, const QByteArray &body)
{
    const ScopedLogEndpoint logEndpoint(peerAddress, LogDirection::Inbound);
    const auto              zoneEvent = m_zoneEventSubscriptions.constFind(sid);
    if (zoneEvent != m_zoneEventSubscriptions.constEnd())
    {
        const ZoneEventSubscription subscription = zoneEvent.value();
        QLOG() << "GENA NOTIFY " << subscription.serviceName << sid
               << (subscription.zone ? subscription.zone->roomName() : QStringLiteral("<deleted>")) << "("
               << body.size() << "bytes)";
        routeZoneEvent(subscription, body);
        return;
    }

    if (sid != m_topologySubscriptionSid)
    {
        QLOG() << "GENA UNKNOWN " << sid << "(" << body.size() << "bytes)";
        return;
    }

    QLOG() << "GENA NOTIFY  ZoneGroupTopology " << sid << "(" << body.size() << "bytes)";

    const QString zoneGroupState = extractGenaProperty(body, QStringLiteral("ZoneGroupState"));
    if (zoneGroupState.isEmpty())
        QLOG() << "ZoneGroupTopology NOTIFY carried no ZoneGroupState property";
    else
        emit topologyStateReceived(zoneGroupState.toUtf8());

    // Sonos multiplexes service-account updates on the topology event channel.
    // Discovery does not decrypt or resolve them; services/Household handle it.
    const QString tpmsx = extractGenaProperty(body, QStringLiteral("ThirdPartyMediaServersX"));
    if (!tpmsx.isEmpty())
        emit thirdPartyMediaServersXReceived(tpmsx);
}

} // namespace RoomTunes
