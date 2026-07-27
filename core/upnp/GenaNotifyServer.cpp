#include "GenaNotifyServer.h"

#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

namespace RoomTunes {

GenaNotifyServer::GenaNotifyServer(QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection, this, &GenaNotifyServer::onNewConnection);
}

bool GenaNotifyServer::listen()
{
    return m_server->listen(QHostAddress::AnyIPv4, 0);
}

quint16 GenaNotifyServer::port() const
{
    return m_server->serverPort();
}

void GenaNotifyServer::onNewConnection()
{
    while (QTcpSocket *socket = m_server->nextPendingConnection()) {
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { onReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            m_buffers.remove(socket);
            socket->deleteLater();
        });
    }
}

void GenaNotifyServer::onReadyRead(QTcpSocket *socket)
{
    m_buffers[socket].append(socket->readAll());
    tryParse(socket);
}

void GenaNotifyServer::tryParse(QTcpSocket *socket)
{
    QByteArray &buffer = m_buffers[socket];

    const int headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0)
        return; // headers not fully received yet

    const QByteArray headerBlock = buffer.left(headerEnd);
    const QList<QByteArray> lines = headerBlock.split('\n');

    QString sid;
    int contentLength = 0;

    // line 0 is the "NOTIFY <path> HTTP/1.1" request line
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        const int colon = line.indexOf(':');
        if (colon <= 0)
            continue;

        const QByteArray name = line.left(colon).trimmed().toUpper();
        const QByteArray value = line.mid(colon + 1).trimmed();

        if (name == "SID")
            sid = QString::fromUtf8(value);
        else if (name == "CONTENT-LENGTH")
            contentLength = value.toInt();
    }

    const int bodyStart = headerEnd + 4;
    if (buffer.size() < bodyStart + contentLength)
        return; // body not fully received yet

    const QByteArray body = buffer.mid(bodyStart, contentLength);
    m_buffers.remove(socket);

    socket->write("HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
    socket->flush();
    socket->disconnectFromHost();

    emit notified(socket->peerAddress().toString(), sid, body);
}

}
