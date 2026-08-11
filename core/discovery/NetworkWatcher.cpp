#include "NetworkWatcher.h"

#include <QHostAddress>
#include <QNetworkInterface>

namespace RoomTunes
{

namespace
{
// A physical unplug, a Wi-Fi handover, or a full reconnect all settle
// within a second or two -- polling every 5s (matching this codebase's
// other retry/poll cadences, e.g. Household's kServiceFetchRetrySeconds)
// reacts quickly enough to feel immediate for a background discovery
// service without burning CPU checking constantly.
constexpr int kPollIntervalMs = 5000;
} // namespace

NetworkWatcher::NetworkWatcher(QObject *parent) : QObject(parent)
{
    m_lastAddresses = currentAddresses();

    m_pollTimer.setInterval(kPollIntervalMs);
    connect(&m_pollTimer, &QTimer::timeout, this, &NetworkWatcher::poll);
    m_pollTimer.start();
}

QSet<QString> NetworkWatcher::currentAddresses()
{
    QSet<QString> addresses;

    for (const QHostAddress &addr : QNetworkInterface::allAddresses())
    {
        // IPv4 only, and not loopback -- link-local (169.254.x.x, DHCP
        // failure/APIPA) addresses are deliberately still counted, since a
        // real interface dropping into that range instead of cleanly
        // disappearing is itself a network change worth reacting to.
        if (addr.protocol() != QAbstractSocket::IPv4Protocol)
            continue;
        if (addr.isLoopback())
            continue;
        addresses.insert(addr.toString());
    }

    return addresses;
}

void NetworkWatcher::poll()
{
    const QSet<QString> current = currentAddresses();
    if (current == m_lastAddresses)
        return;

    m_lastAddresses = current;
    emit networkChanged();
}

} // namespace RoomTunes
