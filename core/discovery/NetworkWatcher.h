#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

namespace RoomTunes
{

// Detects local network changes that Sonos discovery needs to react to --
// not just full disconnect/reconnect, but also silently switching from one
// interface to another on the same LAN (e.g. unplugging Ethernet and
// falling back to Wi-Fi on the same subnet), which changes which local
// address is actually reachable from each zone without the machine ever
// looking "offline" in between.
//
// Ported conceptually from SonosApp::onNetworkStatusUpdated() (Sonos.cpp)
// -- the original's triggers were BlackBerry 10's bps/netstatus interface
// events, which have no portable Qt6 equivalent. Qt6's own
// QNetworkInformation::reachability is the obvious first choice, but it
// doesn't fit here: switching interfaces on the same LAN legitimately never
// changes overall reachability (the machine is "Online" the entire time),
// so it would miss exactly the case this exists for. Polling the actual
// set of local IPv4 addresses and comparing it to the last-seen snapshot
// is simpler and catches every case that actually matters: an address
// disappearing (disconnect), appearing (reconnect), or being swapped for a
// different one (interface handover) all show up as the set changing.
class NetworkWatcher : public QObject
{
    Q_OBJECT

  public:
    explicit NetworkWatcher(QObject *parent = nullptr);

  signals:
    // Fired whenever the local machine's set of usable IPv4 addresses
    // changes from what it was at the last poll.
    void networkChanged();

  private:
    void                 poll();
    static QSet<QString> currentAddresses();

  private:
    QTimer        m_pollTimer;
    QSet<QString> m_lastAddresses;
};

} // namespace RoomTunes
