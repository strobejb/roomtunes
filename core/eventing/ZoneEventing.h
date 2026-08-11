#pragma once

#include <QMap>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QTimer>

#include "GenaNotifyServer.h"

namespace RoomTunes
{

class UpnpService;
class ZonePlayer;

// Owns the Sonos GENA eventing phase: callback listener, SUBSCRIBE/renewal,
// SID bookkeeping, and NOTIFY routing. Discovery decides which zones exist and
// parses topology; eventing keeps the live subscription channel working.
class ZoneEventing : public QObject
{
    Q_OBJECT

  public:
    explicit ZoneEventing(QObject *parent = nullptr);
    ~ZoneEventing() override;

    bool listen();
    void subscribeTopology(ZonePlayer *zone);
    void subscribeZoneEvents(ZonePlayer *zone);
    void unsubscribeAll();

  signals:
    void topologySubscriptionZonePicked(ZonePlayer *zone);
    void topologyStateReceived(const QByteArray &zoneGroupState);
    void thirdPartyMediaServersXReceived(const QString &encoded);

  private slots:
    void onGenaNotify(const QString &peerAddress, const QString &sid, const QByteArray &body);
    void renewTopologySubscription();
    void renewZoneEventSubscriptions();

  private:
    enum class ZoneEventService
    {
        AVTransport,
        RenderingControl,
        ContentDirectory,
        AudioIn
    };

    struct ZoneEventSubscription
    {
        QPointer<ZonePlayer> zone;
        ZoneEventService     service;
        QString              serviceName;
    };

    void subscribeZoneEvent(ZonePlayer *zone, UpnpService &service, ZoneEventService serviceType);
    void routeZoneEvent(const ZoneEventSubscription &subscription, const QByteArray &body);

  private:
    GenaNotifyServer                     m_notifyServer;
    QPointer<ZonePlayer>                 m_topologyZone;
    QString                              m_topologySubscriptionSid;
    QMap<QString, ZoneEventSubscription> m_zoneEventSubscriptions;
    QSet<QString>                        m_pendingZoneEventSubscriptions;
    QTimer                               m_topologyRenewTimer;
    QTimer                               m_zoneEventRenewTimer;
};

} // namespace RoomTunes
