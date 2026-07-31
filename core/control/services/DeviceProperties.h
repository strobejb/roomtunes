#pragma once

#include "../UpnpServiceBase.h"

namespace RoomTunes {

// Ported from upnp/Sonos_DeviceProperties.hpp, trimmed to zone
// naming/household/model info (LED, bonded-zone/stereo-pair/HT-satellite,
// and autoplay settings are out of scope for now -- see project plan).
class DeviceProperties : public UpnpServiceBase
{
public:
    DeviceProperties(QNetworkAccessManager *netMgr, const QString &device, int port = 1400)
        : UpnpServiceBase(netMgr, device, port,
                           QStringLiteral("/DeviceProperties/Control"),
                           QStringLiteral("/DeviceProperties/Event"),
                           QStringLiteral("urn:schemas-upnp-org:service:DeviceProperties:1"),
                           "DeviceProperties")
    {
    }

    QNetworkReply *SetZoneAttributes(const QString &desiredZoneName, const QString &desiredIcon,
                                      const QString &desiredConfiguration = QString())
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("SetZoneAttributes"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.writeStrParameter(QStringLiteral("DesiredZoneName"), desiredZoneName);
        request.writeStrParameter(QStringLiteral("DesiredIcon"), desiredIcon);
        request.writeStrParameter(QStringLiteral("DesiredConfiguration"), desiredConfiguration);
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

    QNetworkReply *GetZoneAttributes()
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("GetZoneAttributes"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

    QNetworkReply *GetHouseholdID()
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("GetHouseholdID"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

    QNetworkReply *GetZoneInfo()
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("GetZoneInfo"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }
};

}
