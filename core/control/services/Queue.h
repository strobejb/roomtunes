#pragma once

#include "../UpnpServiceBase.h"

namespace RoomTunes {

class Queue : public UpnpServiceBase
{
public:
    Queue(QNetworkAccessManager *netMgr, const QString &device, int port = 1400)
        : UpnpServiceBase(netMgr, device, port,
                          QStringLiteral("/MediaRenderer/Queue/Control"),
                          QStringLiteral("/MediaRenderer/Queue/Event"),
                          QStringLiteral("urn:schemas-upnp-org:service:Queue:1"),
                          "Queue")
    {
    }

    QNetworkReply *ReorderTracks(int queueId, const QString &startingIndex, int numberOfTracks,
                                 const QString &insertBefore, int updateId)
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("ReorderTracks"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.writeIntParameter(QStringLiteral("QueueID"), queueId);
        request.writeStrParameter(QStringLiteral("StartingIndex"), startingIndex);
        request.writeIntParameter(QStringLiteral("NumberOfTracks"), numberOfTracks);
        request.writeStrParameter(QStringLiteral("InsertBefore"), insertBefore);
        request.writeIntParameter(QStringLiteral("UpdateID"), updateId);
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }
};

}
