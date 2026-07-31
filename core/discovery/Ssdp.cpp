#include "Ssdp.h"

#include <QHostAddress>
#include <QNetworkInterface>
#include <QTimer>
#include <QVariant>

#include "../Logging.h"

#define QLOG_CATEGORY logDiscovery

namespace RoomTunes {

namespace {
constexpr char kMulticastAddress[] = "239.255.255.250";
constexpr int kMulticastTtl = 3;
constexpr int kMaxDatagramSize = 1024;

// How many consecutive ports past the requested one to try before giving
// up -- SSDP M-SEARCH replies are unicast back to whatever source port the
// request came from (see kDiscoverMessage below), so it doesn't matter to
// any ZonePlayer which of these we actually land on; this just makes a
// single already-in-use port a non-fatal condition instead of a hard
// discovery failure.
constexpr int kMaxBindAttempts = 8;

const char kDiscoverMessage[] =
    "M-SEARCH * HTTP/1.1\r\n"
    "HOST: 239.255.255.250:1900\r\n"
    "MAN: \"ssdp:discover\"\r\n"
    "MX: 1\r\n"
    "ST: urn:schemas-upnp-org:device:ZonePlayer:1\r\n"
    "\r\n";

QMap<QString, QString> parseHttpHeaders(const QString &response)
{
    QMap<QString, QString> headers;
    const QStringList lines = response.split(QStringLiteral("\r\n"));

    // line 0 is the status line ("HTTP/1.1 200 OK" or "NOTIFY * HTTP/1.1")
    for (int i = 1; i < lines.size(); ++i) {
        const QString &line = lines.at(i);
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;

        headers.insert(line.left(colon).trimmed().toUpper(), line.mid(colon + 1).trimmed());
    }

    return headers;
}
}

Ssdp::Ssdp(QObject *parent)
    : QObject(parent)
    , m_socket(new QUdpSocket(this))
    , m_timer(new QTimer(this))
{
    connect(m_timer, &QTimer::timeout, this, &Ssdp::sendDiscover);
    connect(m_socket, &QUdpSocket::readyRead, this, &Ssdp::receiveDatagrams);
}

Ssdp::~Ssdp()
{
    m_socket->leaveMulticastGroup(QHostAddress(QLatin1String(kMulticastAddress)));
    m_socket->disconnectFromHost();
}

QAbstractSocket::SocketError Ssdp::socketError() const
{
    return m_socket->error();
}

QString Ssdp::socketErrorString() const
{
    return m_socket->errorString();
}

namespace {
// Guessing the right adapter from interface flags/type is unreliable:
// machines commonly have several "Up + Running + CanMulticast" adapters
// (virtual switches from Hyper-V/WSL/Docker, VPN leftovers, etc.) that
// report as perfectly good Ethernet/Wifi interfaces but don't actually
// route anywhere near the real LAN. Instead, ask the OS which interface it
// would use to reach the multicast group: "connecting" a UDP socket doesn't
// send any packets (UDP has no handshake), it just makes the kernel resolve
// a route and bind an outbound address, which is exactly the adapter we want.
QHostAddress probeRoutedLocalAddress(const QHostAddress &target, quint16 port)
{
    QUdpSocket probe;
    probe.connectToHost(target, port);
    probe.waitForConnected(200);
    const QHostAddress local = probe.localAddress();
    probe.close();
    return local;
}

QNetworkInterface pickMulticastInterface()
{
    QHostAddress routedLocalAddress = probeRoutedLocalAddress(QHostAddress(QLatin1String(kMulticastAddress)), Ssdp::kMulticastPort);

    // On some Windows setups (observed with WiFi-only -- no wired adapter
    // present) the OS resolves the route to the *multicast group*
    // specifically via the loopback interface, which can never reach real
    // LAN devices. Retry the same "connect and see what routes" trick
    // against a plausible external unicast address instead: UDP connect()
    // never actually sends a packet, so the target doesn't need to be
    // reachable, only routable -- and no real route to the internet is
    // ever via loopback.
    if (routedLocalAddress.isLoopback() || routedLocalAddress.isNull())
        routedLocalAddress = probeRoutedLocalAddress(QHostAddress(QStringLiteral("1.1.1.1")), 80);

    if (!routedLocalAddress.isNull() && !routedLocalAddress.isLoopback()) {
        for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
            for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
                if (entry.ip() == routedLocalAddress)
                    return iface;
            }
        }
    }

    return QNetworkInterface();
}
}

bool Ssdp::listen(quint16 localPort)
{
    // Safe to call again on an already-bound socket -- see
    // ZoneDiscovery::restart(). A network change (Ethernet unplugged, now
    // on Wi-Fi) can leave this socket's outbound multicast interface
    // pointing at an adapter that no longer routes anywhere; every
    // writeDatagram() to the multicast group then fails silently forever
    // ("Unable to send a message"), since nothing else ever re-picks it.
    // Tearing the bind down and redoing it from scratch re-runs
    // pickMulticastInterface() against whatever the OS currently considers
    // the routed interface, which is the actual fix.
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->leaveMulticastGroup(QHostAddress(QLatin1String(kMulticastAddress)));
        m_socket->close();
    }

    // Binding is required: M-SEARCH discovery replies come back as unicast
    // to this socket's bound port. Joining the multicast group is only
    // needed to additionally receive other devices' unsolicited NOTIFY
    // announcements, so a failure to join is logged but not treated as
    // fatal. Must bind to AnyIPv4 specifically, not the dual-stack Any --
    // Qt refuses to join an IPv4 multicast group on a socket bound to Any.
    //
    // localPort is only a starting point, not a hard requirement -- see
    // kMaxBindAttempts. A single already-in-use port (or a transient OS-
    // level condition around it) shouldn't be a fatal discovery failure
    // when the next port over works just as well.
    bool bound = false;
    for (int attempt = 0; attempt < kMaxBindAttempts; ++attempt) {
        const quint16 tryPort = static_cast<quint16>(localPort + attempt);
        if (m_socket->bind(QHostAddress::AnyIPv4, tryPort, QUdpSocket::ShareAddress)) {
            bound = true;
            break;
        }
        QWARN() << "SSDP bind failed on port" << tryPort << ":" << m_socket->errorString()
                << (attempt + 1 < kMaxBindAttempts ? "-- trying next port" : "-- giving up");
    }

    if (!bound) {
        emit socketErrorOccurred(m_socket->error(), m_socket->errorString());
        return false;
    }

    const QHostAddress group{QLatin1String(kMulticastAddress)};
    const QNetworkInterface iface = pickMulticastInterface();

    const bool joined = iface.isValid() ? m_socket->joinMulticastGroup(group, iface) : m_socket->joinMulticastGroup(group);
    if (!joined)
        emit socketErrorOccurred(m_socket->error(), m_socket->errorString());

    if (iface.isValid())
        m_socket->setMulticastInterface(iface);

    m_socket->setSocketOption(QAbstractSocket::MulticastTtlOption, kMulticastTtl);

    QLOG() << "SSDP bound on" << m_socket->localAddress().toString() << ":" << m_socket->localPort()
           << "interface=" << (iface.isValid() ? iface.humanReadableName() : QStringLiteral("(default)"))
           << "multicastJoined=" << joined;

    return true;
}

void Ssdp::discover()
{
    m_messageCount = 0;
    sendDiscover();
    m_timer->start(kResendTimeoutMs);
}

void Ssdp::sendDiscover()
{
    const qint64 sent = m_socket->writeDatagram(kDiscoverMessage, sizeof(kDiscoverMessage) - 1,
                                                 QHostAddress(QLatin1String(kMulticastAddress)), kMulticastPort);
    QLOG() << "SSDP M-SEARCH sent," << sent << "bytes";

    if (sent < 0)
        emit socketErrorOccurred(m_socket->error(), m_socket->errorString());

    if (++m_messageCount >= kResendMax) {
        m_timer->stop();
        emit timeout();
    }
}

void Ssdp::receiveDatagrams()
{
    while (m_socket->hasPendingDatagrams()) {
        QByteArray packet;
        packet.resize(int(m_socket->pendingDatagramSize()));

        QHostAddress fromAddr;
        quint16 fromPort = 0;
        const qint64 len = m_socket->readDatagram(packet.data(), packet.size(), &fromAddr, &fromPort);
        if (len < 0)
            continue;

        const QString response = QString::fromLatin1(packet.constData(), int(len));
        const ScopedLogEndpoint logEndpoint(fromAddr.toString(), LogDirection::Inbound);
        //QLOG() << "SSDP received" << len << "bytes from port" << fromPort;
        emit discovered(fromAddr.toString(), parseHttpHeaders(response));
    }
}

}
