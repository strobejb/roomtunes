#pragma once

#include <QMap>
#include <QObject>
#include <QString>
#include <QUdpSocket>

class QTimer;

namespace RoomTunes {

// M-SEARCH SSDP discovery for Sonos ZonePlayers. Ported from ssdp.hpp/.cpp
// near-verbatim; QHttpResponseHeader (removed from QtNetwork) is replaced
// with a small manual "Key: Value" header parse.
class Ssdp : public QObject
{
    Q_OBJECT

public:
    static constexpr quint16 kMulticastPort = 1900;
    static constexpr quint16 kDefaultRecvPort = 1901;
    static constexpr int kResendTimeoutMs = 3000;
    static constexpr int kResendMax = 3;

    explicit Ssdp(QObject *parent = nullptr);
    ~Ssdp() override;

    bool listen(quint16 localPort);
    void discover();

    QAbstractSocket::SocketError socketError() const;
    QString socketErrorString() const;

signals:
    // headers is the parsed set of "Key: Value" response headers (LOCATION, USN, ...)
    void discovered(const QString &fromAddr, const QMap<QString, QString> &headers);
    void timeout();
    void socketErrorOccurred(QAbstractSocket::SocketError error, const QString &errorString);

private slots:
    void sendDiscover();
    void receiveDatagrams();

private:
    QUdpSocket *m_socket;
    QTimer *m_timer;
    int m_messageCount = 0;
};

}
