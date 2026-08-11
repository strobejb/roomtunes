#pragma once

#include "../UpnpServiceBase.h"

namespace RoomTunes
{

// Sonos line-in service. Currently only needed for GENA subscription
// coverage; action methods can be added when UI needs line-in controls.
class AudioIn : public UpnpServiceBase
{
  public:
    AudioIn(QNetworkAccessManager *netMgr, const QString &device, int port = 1400)
        : UpnpServiceBase(netMgr, device, port, QStringLiteral("/AudioIn/Control"), QStringLiteral("/AudioIn/Event"),
                          QStringLiteral("urn:schemas-upnp-org:service:AudioIn:1"), "AudioIn")
    {
    }
};

} // namespace RoomTunes
