#pragma once

#include "../UpnpServiceBase.h"

namespace RoomTunes {

// Ported from upnp/Sonos_ZoneGroupTopology.hpp, trimmed to the group-state
// query Household needs to build the zone map (software-update/diagnostics
// actions are out of scope).
class ZoneGroupTopology : public UpnpServiceBase
{
public:
    ZoneGroupTopology(QNetworkAccessManager *netMgr, const QString &device, int port = 1400)
        : UpnpServiceBase(netMgr, device, port,
                           QStringLiteral("/ZoneGroupTopology/Control"),
                           QStringLiteral("/ZoneGroupTopology/Event"),
                           QStringLiteral("urn:schemas-upnp-org:service:ZoneGroupTopology:1"),
                           "ZoneGroupTopology")
    {
    }

    QNetworkReply *GetZoneGroupState()
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("GetZoneGroupState"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

    QNetworkReply *GetZoneGroupAttributes()
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("GetZoneGroupAttributes"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }
};

}
