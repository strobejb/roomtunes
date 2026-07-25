#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>

class QTcpServer;
class QTcpSocket;

namespace RoomTunes {

// Minimal HTTP listener for UPnP GENA NOTIFY callbacks -- the receiving
// half of the subscribe()/unsubscribe() calls already in UpnpServiceBase.
// Sonos zones send a NOTIFY request here whenever a subscribed service's
// state changes (e.g. ZoneGroupTopology on a group/topology change).
// Hand-parses the minimal HTTP needed (method line, SID/Content-Length
// headers, body) rather than pulling in a full HTTP server module, in
// keeping with how SSDP/SOAP responses are parsed elsewhere in this library.
class GenaNotifyServer : public QObject
{
    Q_OBJECT

public:
    explicit GenaNotifyServer(QObject *parent = nullptr);

    // Binds an ephemeral port on all IPv4 interfaces; use port() for the
    // actual bound value to put in a SUBSCRIBE request's callback URL.
    bool listen();
    quint16 port() const;

signals:
    // sid is the NOTIFY's SID header (identifies which subscription this
    // is for); body is the raw GENA propertyset XML payload.
    void notified(const QString &sid, const QByteArray &body);

private slots:
    void onNewConnection();

private:
    void onReadyRead(QTcpSocket *socket);
    void tryParse(QTcpSocket *socket);

private:
    QTcpServer *m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

}
