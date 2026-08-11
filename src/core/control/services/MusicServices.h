#pragma once

#include "../UpnpServiceBase.h"

namespace RoomTunes
{

// Ported from upnp/Sonos_MusicServices.hpp near-verbatim.
class MusicServices : public UpnpServiceBase
{
  public:
    MusicServices(QNetworkAccessManager *netMgr, const QString &device, int port = 1400)
        : UpnpServiceBase(netMgr, device, port, QStringLiteral("/MusicServices/Control"),
                          QStringLiteral("/MusicServices/Event"),
                          QStringLiteral("urn:schemas-upnp-org:service:MusicServices:1"), "MusicServices")
    {
    }

    QNetworkReply *GetSessionId(int serviceId, const QString &username)
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("GetSessionId"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.writeIntParameter(QStringLiteral("ServiceId"), serviceId);
        request.writeStrParameter(QStringLiteral("Username"), username);
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

    QNetworkReply *ListAvailableServices()
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("ListAvailableServices"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }
};

} // namespace RoomTunes
