#pragma once

#include "../UpnpServiceBase.h"

namespace RoomTunes
{

// Ported from upnp/Sonos_SystemProperties.hpp, trimmed to just GetString --
// the only action RoomTunes needs (to read R_TrialZPSerial, see
// Household::fetchServiceDeviceSerial()).
class SystemProperties : public UpnpServiceBase
{
  public:
    SystemProperties(QNetworkAccessManager *netMgr, const QString &device, int port = 1400)
        : UpnpServiceBase(netMgr, device, port, QStringLiteral("/SystemProperties/Control"),
                          QStringLiteral("/SystemProperties/Event"),
                          QStringLiteral("urn:schemas-upnp-org:service:SystemProperties:1"), "SystemProperties")
    {
    }

    QNetworkReply *GetString(const QString &variableName)
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("GetString"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.writeStrParameter(QStringLiteral("VariableName"), variableName);
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }
};

} // namespace RoomTunes
