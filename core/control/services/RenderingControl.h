#pragma once

#include "../UpnpServiceBase.h"

namespace RoomTunes {

// Ported from upnp/Sonos_RenderingControl.hpp, trimmed to volume/mute
// (bass/treble/loudness/EQ are out of scope for now -- see project plan).
class RenderingControl : public UpnpServiceBase
{
public:
    RenderingControl(QNetworkAccessManager *netMgr, const QString &device, int port = 1400)
        : UpnpServiceBase(netMgr, device, port,
                           QStringLiteral("/MediaRenderer/RenderingControl/Control"),
                           QStringLiteral("/MediaRenderer/RenderingControl/Event"),
                           QStringLiteral("urn:schemas-upnp-org:service:RenderingControl:1"),
                           "RenderingControl")
    {
    }

    QNetworkReply *GetMute(int instanceId, const QString &channel = QStringLiteral("Master"))
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("GetMute"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.writeIntParameter(QStringLiteral("InstanceID"), instanceId);
        request.writeStrParameter(QStringLiteral("Channel"), channel);
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

    QNetworkReply *SetMute(int instanceId, const QString &channel, bool desiredMute)
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("SetMute"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.writeIntParameter(QStringLiteral("InstanceID"), instanceId);
        request.writeStrParameter(QStringLiteral("Channel"), channel);
        request.writeStrParameter(QStringLiteral("DesiredMute"), desiredMute ? QStringLiteral("1") : QStringLiteral("0"));
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

    QNetworkReply *GetVolume(int instanceId, const QString &channel = QStringLiteral("Master"))
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("GetVolume"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.writeIntParameter(QStringLiteral("InstanceID"), instanceId);
        request.writeStrParameter(QStringLiteral("Channel"), channel);
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

    QNetworkReply *SetVolume(int instanceId, const QString &channel, int desiredVolume)
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("SetVolume"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.writeIntParameter(QStringLiteral("InstanceID"), instanceId);
        request.writeStrParameter(QStringLiteral("Channel"), channel);
        request.writeIntParameter(QStringLiteral("DesiredVolume"), desiredVolume);
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }
};

}
