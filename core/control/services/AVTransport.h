#pragma once

#include "../UpnpServiceBase.h"

namespace RoomTunes {

// Ported from upnp/Sonos_AVTransport.hpp, trimmed to the play/pause/queue
// actions this pass needs (alarms, sleep timer, and group-coordination
// actions are out of scope for now -- see the project plan).
class AVTransport : public UpnpServiceBase
{
public:
    AVTransport(QNetworkAccessManager *netMgr, const QString &device, int port = 1400)
        : UpnpServiceBase(netMgr, device, port,
                           QStringLiteral("/MediaRenderer/AVTransport/Control"),
                           QStringLiteral("/MediaRenderer/AVTransport/Event"),
                           QStringLiteral("urn:schemas-upnp-org:service:AVTransport:1"),
                           "AVTransport")
    {
    }

    QNetworkReply *SetAVTransportURI(int instanceId, const QString &currentUri, const QString &currentUriMetaData)
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("SetAVTransportURI"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.writeIntParameter(QStringLiteral("InstanceID"), instanceId);
        request.writeStrParameter(QStringLiteral("CurrentURI"), currentUri);
        request.writeStrParameter(QStringLiteral("CurrentURIMetaData"), currentUriMetaData);
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

    QNetworkReply *SetNextAVTransportURI(int instanceId, const QString &nextUri, const QString &nextUriMetaData)
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("SetNextAVTransportURI"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.writeIntParameter(QStringLiteral("InstanceID"), instanceId);
        request.writeStrParameter(QStringLiteral("NextURI"), nextUri);
        request.writeStrParameter(QStringLiteral("NextURIMetaData"), nextUriMetaData);
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

    QNetworkReply *AddURIToQueue(int instanceId, const QString &enqueuedUri, const QString &enqueuedUriMetaData,
                                  int desiredFirstTrackNumberEnqueued, bool enqueueAsNext)
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("AddURIToQueue"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.writeIntParameter(QStringLiteral("InstanceID"), instanceId);
        request.writeStrParameter(QStringLiteral("EnqueuedURI"), enqueuedUri);
        request.writeStrParameter(QStringLiteral("EnqueuedURIMetaData"), enqueuedUriMetaData);
        request.writeIntParameter(QStringLiteral("DesiredFirstTrackNumberEnqueued"), desiredFirstTrackNumberEnqueued);
        request.writeStrParameter(QStringLiteral("EnqueueAsNext"), enqueueAsNext ? QStringLiteral("1") : QStringLiteral("0"));
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

    QNetworkReply *RemoveTrackFromQueue(int instanceId, const QString &objectId, int updateId)
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("RemoveTrackFromQueue"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.writeIntParameter(QStringLiteral("InstanceID"), instanceId);
        request.writeStrParameter(QStringLiteral("ObjectID"), objectId);
        request.writeIntParameter(QStringLiteral("UpdateID"), updateId);
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

    QNetworkReply *RemoveTrackRangeFromQueue(int instanceId, int updateId, int startingIndex, int numberOfTracks)
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("RemoveTrackRangeFromQueue"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.writeIntParameter(QStringLiteral("InstanceID"), instanceId);
        request.writeIntParameter(QStringLiteral("UpdateID"), updateId);
        request.writeIntParameter(QStringLiteral("StartingIndex"), startingIndex);
        request.writeIntParameter(QStringLiteral("NumberOfTracks"), numberOfTracks);
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

    QNetworkReply *ReorderTracksInQueue(int instanceId, const QString &startingIndex, int numberOfTracks,
                                        const QString &insertBefore,
                                        int updateId)
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("ReorderTracksInQueue"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.writeIntParameter(QStringLiteral("InstanceID"), instanceId);
        request.writeStrParameter(QStringLiteral("StartingIndex"), startingIndex);
        request.writeIntParameter(QStringLiteral("NumberOfTracks"), numberOfTracks);
        request.writeStrParameter(QStringLiteral("InsertBefore"), insertBefore);
        request.writeIntParameter(QStringLiteral("UpdateID"), updateId);
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

    QNetworkReply *RemoveAllTracksFromQueue(int instanceId)
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("RemoveAllTracksFromQueue"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.writeIntParameter(QStringLiteral("InstanceID"), instanceId);
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

    QNetworkReply *SaveQueue(int instanceId, const QString &title, const QString &objectId)
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("SaveQueue"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.writeIntParameter(QStringLiteral("InstanceID"), instanceId);
        request.writeStrParameter(QStringLiteral("Title"), title);
        request.writeStrParameter(QStringLiteral("ObjectID"), objectId);
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

    QNetworkReply *GetMediaInfo(int instanceId)
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("GetMediaInfo"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.writeIntParameter(QStringLiteral("InstanceID"), instanceId);
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

    QNetworkReply *GetTransportInfo(int instanceId)
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("GetTransportInfo"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.writeIntParameter(QStringLiteral("InstanceID"), instanceId);
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

    QNetworkReply *GetTransportSettings(int instanceId)
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("GetTransportSettings"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.writeIntParameter(QStringLiteral("InstanceID"), instanceId);
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

    QNetworkReply *GetPositionInfo(int instanceId)
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("GetPositionInfo"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.writeIntParameter(QStringLiteral("InstanceID"), instanceId);
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

    QNetworkReply *Stop(int instanceId)
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("Stop"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.writeIntParameter(QStringLiteral("InstanceID"), instanceId);
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

    QNetworkReply *Play(int instanceId, const QString &speed = QStringLiteral("1"))
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("Play"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.writeIntParameter(QStringLiteral("InstanceID"), instanceId);
        request.writeStrParameter(QStringLiteral("Speed"), speed);
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

    QNetworkReply *Pause(int instanceId)
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("Pause"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.writeIntParameter(QStringLiteral("InstanceID"), instanceId);
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

    QNetworkReply *Seek(int instanceId, const QString &unit, const QString &target)
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("Seek"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.writeIntParameter(QStringLiteral("InstanceID"), instanceId);
        request.writeStrParameter(QStringLiteral("Unit"), unit);
        request.writeStrParameter(QStringLiteral("Target"), target);
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

    QNetworkReply *Next(int instanceId)
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("Next"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.writeIntParameter(QStringLiteral("InstanceID"), instanceId);
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

    QNetworkReply *Previous(int instanceId)
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("Previous"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.writeIntParameter(QStringLiteral("InstanceID"), instanceId);
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

    QNetworkReply *SetPlayMode(int instanceId, const QString &newPlayMode)
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("SetPlayMode"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.writeIntParameter(QStringLiteral("InstanceID"), instanceId);
        request.writeStrParameter(QStringLiteral("NewPlayMode"), newPlayMode);
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

    QNetworkReply *GetCurrentTransportActions(int instanceId)
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("GetCurrentTransportActions"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.writeIntParameter(QStringLiteral("InstanceID"), instanceId);
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

    // Detaches this zone from whatever group it's currently in, making it
    // its own standalone single-zone group -- the "unlink" action. Sonos
    // handles coordinator hand-off within the old group automatically
    // (including when this was the coordinator being unlinked).
    QNetworkReply *BecomeCoordinatorOfStandaloneGroup(int instanceId)
    {
        SoapRequest request(m_netMgr, m_action, QStringLiteral("BecomeCoordinatorOfStandaloneGroup"));
        request.openEnvelope();
        request.openCommand(QStringLiteral("u"));
        request.writeIntParameter(QStringLiteral("InstanceID"), instanceId);
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }
};

}
