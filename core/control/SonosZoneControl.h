#pragma once

#include <functional>

#include <QList>
#include <QObject>
#include <QString>

#include "Didl.h"

namespace RoomTunes {

class AVTransport;
class ContentDirectory;
class Queue;
class RenderingControl;

class SonosZoneControl
{
public:
    struct PositionInfo
    {
        QString trackMetaData;
        QString trackUri;
        QString relTime;
        QString trackDuration;
        int trackNumber = 0;
        bool trackNumberKnown = false;
    };

    struct BrowseResult
    {
        QString errorMessage;
        QList<DidlItem> items;
        int updateId = 0;
        bool updateIdKnown = false;
    };

    SonosZoneControl(AVTransport &avTransport, RenderingControl &renderingControl, ContentDirectory &contentDirectory,
                     Queue &queue,
                     std::function<QString()> roomNameProvider);

    void play(QObject *context, std::function<void(bool)> callback);
    void pause(QObject *context, std::function<void(bool)> callback);
    void next(QObject *context, std::function<void(bool)> callback);
    void previous(QObject *context, std::function<void(bool)> callback);
    void becomeCoordinatorOfStandaloneGroup(QObject *context, std::function<void(bool)> callback);

    void setPlayMode(QObject *context, const QString &playMode, std::function<void(bool)> callback);
    void setAVTransportUri(QObject *context, const QString &uri, const QString &metaData, std::function<void(bool)> callback);
    void addUriToQueue(QObject *context, const QString &uri, const QString &metaData, int desiredFirstTrackNumberEnqueued,
                       bool enqueueAsNext, std::function<void(bool ok, int firstTrackNumberEnqueued)> callback);
    void removeAllTracksFromQueue(QObject *context, std::function<void(bool)> callback);
    void removeTrackFromQueue(QObject *context, const QString &objectId, std::function<void(bool)> callback);
    void reorderTrackInQueue(QObject *context, int fromIndex, int toIndex, int updateId,
                             std::function<void(bool)> callback);
    void seek(QObject *context, const QString &unit, const QString &target, std::function<void(bool)> callback);

    void setVolume(QObject *context, int level, std::function<void(bool)> callback);
    void setMuted(QObject *context, bool muted, std::function<void(bool)> callback);
    void getVolume(QObject *context, std::function<void(bool ok, int level)> callback);
    void getMute(QObject *context, std::function<void(bool ok, bool muted)> callback);

    void browse(QObject *context, const QString &objectId,
                std::function<void(bool ok, const QString &errorMessage, const QList<DidlItem> &items)> callback,
                int startingIndex, int requestedCount, const QString &browseFlag);
    void browseDetailed(QObject *context, const QString &objectId,
                        std::function<void(bool ok, const BrowseResult &result)> callback,
                        int startingIndex, int requestedCount, const QString &browseFlag);
    void getTransportInfo(QObject *context, std::function<void(bool ok, const QString &state)> callback);
    void getTransportSettings(QObject *context, std::function<void(bool ok, const QString &playMode)> callback);
    void getPositionInfo(QObject *context, std::function<void(bool ok, const PositionInfo &info)> callback);

private:
    QString roomName() const;

    AVTransport &m_avTransport;
    RenderingControl &m_renderingControl;
    ContentDirectory &m_contentDirectory;
    Queue &m_queue;
    std::function<QString()> m_roomNameProvider;
};

}
