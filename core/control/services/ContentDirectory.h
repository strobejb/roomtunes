#pragma once

#include "../UpnpServiceBase.h"

namespace RoomTunes {

// Ported from upnp/Sonos_ContentDirectory.hpp, trimmed to Browse (share
// indexing / object management actions are out of scope for now).
class ContentDirectory : public UpnpServiceBase
{
public:
    ContentDirectory(QNetworkAccessManager *netMgr, const QString &device, int port = 1400)
        : UpnpServiceBase(netMgr, device, port,
                           QStringLiteral("/MediaServer/ContentDirectory/Control"),
                           QStringLiteral("/MediaServer/ContentDirectory/Event"),
                           QStringLiteral("urn:schemas-upnp-org:service:ContentDirectory:1"),
                           "ContentDirectory")
    {
    }

    QNetworkReply *Browse(const QString &objectId, const QString &browseFlag, const QString &filter,
                           int startingIndex, int requestedCount, const QString &sortCriteria = QString())
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("Browse"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.writeStrParameter(QStringLiteral("ObjectID"), objectId);
        request.writeStrParameter(QStringLiteral("BrowseFlag"), browseFlag);
        request.writeStrParameter(QStringLiteral("Filter"), filter);
        request.writeIntParameter(QStringLiteral("StartingIndex"), startingIndex);
        request.writeIntParameter(QStringLiteral("RequestedCount"), requestedCount);
        request.writeStrParameter(QStringLiteral("SortCriteria"), sortCriteria);
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

    QNetworkReply *CreateObject(const QString &containerId, const QByteArray &elements)
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("CreateObject"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.writeStrParameter(QStringLiteral("ContainerID"), containerId);
        request.writeStrParameter(QStringLiteral("Elements"), QString::fromUtf8(elements));
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }
};

}
