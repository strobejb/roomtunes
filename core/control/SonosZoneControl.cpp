#include "SonosZoneControl.h"

#include "SoapResponse.h"
#include "services/AVTransport.h"
#include "services/ContentDirectory.h"
#include "services/Queue.h"
#include "services/RenderingControl.h"
#include "../Logging.h"

#include <QNetworkReply>

#include <utility>

#define QLOG_CATEGORY logZone

namespace RoomTunes {

namespace {

QString soapErrorDetail(const SoapResponse &response)
{
    return response.upnpErrorCode().isEmpty()
        ? response.faultString()
        : response.upnpErrorCode() + QStringLiteral(" ") + response.upnpErrorDescription();
}

}

SonosZoneControl::SonosZoneControl(AVTransport &avTransport, RenderingControl &renderingControl,
                                   ContentDirectory &contentDirectory, Queue &queue,
                                   std::function<QString()> roomNameProvider)
    : m_avTransport(avTransport)
    , m_renderingControl(renderingControl)
    , m_contentDirectory(contentDirectory)
    , m_queue(queue)
    , m_roomNameProvider(std::move(roomNameProvider))
{
}

QString SonosZoneControl::roomName() const
{
    return m_roomNameProvider ? m_roomNameProvider() : QString();
}

void SonosZoneControl::play(QObject *context, std::function<void(bool)> callback)
{
    QNetworkReply *reply = m_avTransport.Play(0);
    QObject::connect(reply, &QNetworkReply::finished, context, [this, reply, callback]() {
        SoapResponse response(reply);
        reply->deleteLater();

        const bool ok = !response.error();
        if (!ok)
            QWARN() << roomName() << "Play failed:" << response.faultString();
        if (callback)
            callback(ok);
    });
}

void SonosZoneControl::pause(QObject *context, std::function<void(bool)> callback)
{
    QNetworkReply *reply = m_avTransport.Pause(0);
    QObject::connect(reply, &QNetworkReply::finished, context, [this, reply, callback]() {
        SoapResponse response(reply);
        reply->deleteLater();

        const bool ok = !response.error();
        if (!ok)
            QWARN() << roomName() << "Pause failed:" << response.faultString();
        if (callback)
            callback(ok);
    });
}

void SonosZoneControl::next(QObject *context, std::function<void(bool)> callback)
{
    QNetworkReply *reply = m_avTransport.Next(0);
    QObject::connect(reply, &QNetworkReply::finished, context, [this, reply, callback]() {
        SoapResponse response(reply);
        reply->deleteLater();

        const bool ok = !response.error();
        if (!ok)
            QWARN() << roomName() << "Next failed:" << response.faultString();
        if (callback)
            callback(ok);
    });
}

void SonosZoneControl::previous(QObject *context, std::function<void(bool)> callback)
{
    QNetworkReply *reply = m_avTransport.Previous(0);
    QObject::connect(reply, &QNetworkReply::finished, context, [this, reply, callback]() {
        SoapResponse response(reply);
        reply->deleteLater();

        const bool ok = !response.error();
        if (!ok)
            QWARN() << roomName() << "Previous failed:" << response.faultString();
        if (callback)
            callback(ok);
    });
}

void SonosZoneControl::becomeCoordinatorOfStandaloneGroup(QObject *context, std::function<void(bool)> callback)
{
    QNetworkReply *reply = m_avTransport.BecomeCoordinatorOfStandaloneGroup(0);
    QObject::connect(reply, &QNetworkReply::finished, context, [this, reply, callback]() {
        SoapResponse response(reply);
        reply->deleteLater();

        const bool ok = !response.error();
        if (!ok)
            QWARN() << roomName() << "BecomeCoordinatorOfStandaloneGroup failed:" << response.faultString();
        if (callback)
            callback(ok);
    });
}

void SonosZoneControl::setPlayMode(QObject *context, const QString &playMode, std::function<void(bool)> callback)
{
    QNetworkReply *reply = m_avTransport.SetPlayMode(0, playMode);
    QObject::connect(reply, &QNetworkReply::finished, context, [this, reply, callback]() {
        SoapResponse response(reply);
        reply->deleteLater();

        const bool ok = !response.error();
        if (!ok)
            QWARN() << roomName() << "SetPlayMode failed:" << soapErrorDetail(response);
        if (callback)
            callback(ok);
    });
}

void SonosZoneControl::setCrossfadeEnabled(QObject *context, bool enabled, std::function<void(bool)> callback)
{
    QNetworkReply *reply = m_avTransport.SetCrossfadeMode(0, enabled);
    QObject::connect(reply, &QNetworkReply::finished, context, [this, reply, callback]() {
        SoapResponse response(reply);
        reply->deleteLater();

        const bool ok = !response.error();
        if (!ok)
            QWARN() << roomName() << "SetCrossfadeMode failed:" << soapErrorDetail(response);
        if (callback)
            callback(ok);
    });
}

void SonosZoneControl::setAVTransportUri(QObject *context, const QString &uri, const QString &metaData,
                                         std::function<void(bool)> callback)
{
    QNetworkReply *reply = m_avTransport.SetAVTransportURI(0, uri, metaData);
    QObject::connect(reply, &QNetworkReply::finished, context, [this, reply, callback]() {
        SoapResponse response(reply);
        reply->deleteLater();

        const bool ok = !response.error();
        if (!ok)
            QWARN() << roomName() << "SetAVTransportURI failed:" << soapErrorDetail(response);
        if (callback)
            callback(ok);
    });
}

void SonosZoneControl::addUriToQueue(QObject *context, const QString &uri, const QString &metaData,
                                     int desiredFirstTrackNumberEnqueued, bool enqueueAsNext,
                                     std::function<void(bool, int)> callback)
{
    QNetworkReply *reply = m_avTransport.AddURIToQueue(0, uri, metaData, desiredFirstTrackNumberEnqueued, enqueueAsNext);
    QObject::connect(reply, &QNetworkReply::finished, context, [this, reply, callback]() {
        SoapResponse response(reply);
        reply->deleteLater();

        if (response.error()) {
            QWARN() << roomName() << "AddURIToQueue failed:" << soapErrorDetail(response);
            if (callback)
                callback(false, 0);
            return;
        }

        if (callback)
            callback(true, response.value(QStringLiteral("FirstTrackNumberEnqueued")).toInt());
    });
}

void SonosZoneControl::removeAllTracksFromQueue(QObject *context, std::function<void(bool)> callback)
{
    QNetworkReply *reply = m_avTransport.RemoveAllTracksFromQueue(0);
    QObject::connect(reply, &QNetworkReply::finished, context, [this, reply, callback]() {
        SoapResponse response(reply);
        reply->deleteLater();

        const bool ok = !response.error();
        if (!ok)
            QWARN() << roomName() << "RemoveAllTracksFromQueue failed:" << soapErrorDetail(response);
        if (callback)
            callback(ok);
    });
}

void SonosZoneControl::saveQueueAsSonosPlaylist(QObject *context, const QString &title, std::function<void(bool)> callback)
{
    // Empty ObjectID means "create a new Sonos playlist". Supplying an
    // existing saved-queue object id would overwrite it, which this UI
    // deliberately does not expose.
    QNetworkReply *reply = m_queue.SaveAsSonosPlaylist(/*queueId=*/0, title, QString());
    QObject::connect(reply, &QNetworkReply::finished, context, [this, reply, callback]() {
        SoapResponse response(reply);
        reply->deleteLater();

        const bool ok = !response.error();
        if (!ok)
            QWARN() << roomName() << "Queue.SaveAsSonosPlaylist failed:" << soapErrorDetail(response);
        if (callback)
            callback(ok);
    });
}

void SonosZoneControl::addToSonosFavourites(QObject *context, const DidlItem &item, std::function<void(bool)> callback)
{
    QNetworkReply *reply = m_contentDirectory.CreateObject(QStringLiteral("FV:2"), Didl::buildFavoriteItem(item));
    QObject::connect(reply, &QNetworkReply::finished, context, [this, reply, callback]() {
        SoapResponse response(reply);
        reply->deleteLater();

        const bool ok = !response.error();
        if (!ok)
            QWARN() << roomName() << "CreateObject(FV:2) failed:" << soapErrorDetail(response);
        if (callback)
            callback(ok);
    });
}

void SonosZoneControl::removeTrackFromQueue(QObject *context, const QString &objectId, std::function<void(bool)> callback)
{
    // UpdateID 0 -- Sonos treats it as "don't care", matching roomtunes-bb10.
    QNetworkReply *reply = m_avTransport.RemoveTrackFromQueue(0, objectId, 0);
    QObject::connect(reply, &QNetworkReply::finished, context, [this, reply, callback]() {
        SoapResponse response(reply);
        reply->deleteLater();

        const bool ok = !response.error();
        if (!ok)
            QWARN() << roomName() << "RemoveTrackFromQueue failed:" << soapErrorDetail(response);
        if (callback)
            callback(ok);
    });
}

void SonosZoneControl::reorderTrackInQueue(QObject *context, int fromIndex, int toIndex, int updateId,
                                           std::function<void(bool)> callback)
{
    if (fromIndex == toIndex) {
        if (callback)
            callback(true);
        return;
    }

    // Queue rows in QML/C++ are zero-based, but Sonos queue commands use
    // one-based track numbers. InsertBefore is evaluated in the original
    // queue coordinate space, so a downward move inserts before the item
    // after the desired final slot.
    const int startingIndex = fromIndex + 1;
    const int insertBefore = fromIndex < toIndex ? toIndex + 2 : toIndex + 1;
    QNetworkReply *reply = m_queue.ReorderTracks(/*queueId=*/0, QString::number(startingIndex), /*numberOfTracks=*/1,
                                                  QString::number(insertBefore), updateId);
    QObject::connect(reply, &QNetworkReply::finished, context, [this, reply, callback]() {
        SoapResponse response(reply);
        reply->deleteLater();

        const bool ok = !response.error();
        if (!ok)
            QWARN() << roomName() << "Queue.ReorderTracks failed:" << soapErrorDetail(response);
        if (callback)
            callback(ok);
    });
}

void SonosZoneControl::seek(QObject *context, const QString &unit, const QString &target, std::function<void(bool)> callback)
{
    QNetworkReply *reply = m_avTransport.Seek(0, unit, target);
    QObject::connect(reply, &QNetworkReply::finished, context, [this, reply, callback]() {
        SoapResponse response(reply);
        reply->deleteLater();

        const bool ok = !response.error();
        if (!ok)
            QWARN() << roomName() << "Seek failed:" << soapErrorDetail(response);
        if (callback)
            callback(ok);
    });
}

void SonosZoneControl::setVolume(QObject *context, int level, std::function<void(bool)> callback)
{
    QNetworkReply *reply = m_renderingControl.SetVolume(0, QStringLiteral("Master"), level);
    QObject::connect(reply, &QNetworkReply::finished, context, [reply, callback]() {
        SoapResponse response(reply);
        reply->deleteLater();
        if (callback)
            callback(!response.error());
    });
}

void SonosZoneControl::setMuted(QObject *context, bool muted, std::function<void(bool)> callback)
{
    QNetworkReply *reply = m_renderingControl.SetMute(0, QStringLiteral("Master"), muted);
    QObject::connect(reply, &QNetworkReply::finished, context, [this, reply, callback]() {
        SoapResponse response(reply);
        reply->deleteLater();

        const bool ok = !response.error();
        if (!ok)
            QWARN() << roomName() << "SetMute failed:" << response.faultString();
        if (callback)
            callback(ok);
    });
}

void SonosZoneControl::getVolume(QObject *context, std::function<void(bool, int)> callback)
{
    QNetworkReply *reply = m_renderingControl.GetVolume(0);
    QObject::connect(reply, &QNetworkReply::finished, context, [this, reply, callback]() {
        SoapResponse response(reply);
        reply->deleteLater();

        if (response.error()) {
            QWARN() << roomName() << "GetVolume failed:" << response.faultString();
            if (callback)
                callback(false, 0);
            return;
        }

        bool ok = false;
        const int level = response.value(QStringLiteral("CurrentVolume")).toInt(&ok);
        if (!ok)
            QWARN() << roomName() << "GetVolume returned invalid CurrentVolume:"
                    << response.value(QStringLiteral("CurrentVolume"));
        if (callback)
            callback(ok, level);
    });
}

void SonosZoneControl::getMute(QObject *context, std::function<void(bool, bool)> callback)
{
    QNetworkReply *reply = m_renderingControl.GetMute(0);
    QObject::connect(reply, &QNetworkReply::finished, context, [this, reply, callback]() {
        SoapResponse response(reply);
        reply->deleteLater();

        if (response.error()) {
            QWARN() << roomName() << "GetMute failed:" << response.faultString();
            if (callback)
                callback(false, false);
            return;
        }

        const QString currentMute = response.value(QStringLiteral("CurrentMute"));
        const bool valid = currentMute == QStringLiteral("0") || currentMute == QStringLiteral("1");
        if (!valid)
            QWARN() << roomName() << "GetMute returned invalid CurrentMute:" << currentMute;
        if (callback)
            callback(valid, currentMute == QStringLiteral("1"));
    });
}

void SonosZoneControl::browse(QObject *context, const QString &objectId,
                              std::function<void(bool, const QString &, const QList<DidlItem> &)> callback,
                              int startingIndex, int requestedCount, const QString &browseFlag)
{
    browseDetailed(context, objectId, [callback](bool ok, const BrowseResult &result) {
        if (callback)
            callback(ok, result.errorMessage, result.items);
    }, startingIndex, requestedCount, browseFlag);
}

void SonosZoneControl::browseDetailed(QObject *context, const QString &objectId,
                                      std::function<void(bool, const BrowseResult &)> callback,
                                      int startingIndex, int requestedCount, const QString &browseFlag)
{
    QNetworkReply *reply = m_contentDirectory.Browse(objectId, browseFlag, QStringLiteral("*"), startingIndex, requestedCount);
    QObject::connect(reply, &QNetworkReply::finished, context, [this, reply, objectId, callback]() {
        SoapResponse response(reply);
        reply->deleteLater();

        BrowseResult result;
        if (response.error()) {
            result.errorMessage = soapErrorDetail(response);
            QWARN() << roomName() << "Browse failed: objectId=" << objectId << "httpStatus=" << response.httpStatusCode()
                    << "error=" << result.errorMessage;
            if (callback)
                callback(false, result);
            return;
        }

        result.items = Didl::parseItems(response.value(QStringLiteral("Result")).toUtf8());
        bool updateIdOk = false;
        result.updateId = response.value(QStringLiteral("UpdateID")).toInt(&updateIdOk);
        result.updateIdKnown = updateIdOk;
        if (callback)
            callback(true, result);
    });
}

void SonosZoneControl::getTransportInfo(QObject *context, std::function<void(bool, const QString &)> callback)
{
    QNetworkReply *reply = m_avTransport.GetTransportInfo(0);
    QObject::connect(reply, &QNetworkReply::finished, context, [this, reply, callback]() {
        SoapResponse response(reply);
        reply->deleteLater();

        if (response.error()) {
            QWARN() << roomName() << "GetTransportInfo failed:" << response.faultString();
            if (callback)
                callback(false, QString());
            return;
        }

        if (callback)
            callback(true, response.value(QStringLiteral("CurrentTransportState")));
    });
}

void SonosZoneControl::getTransportSettings(QObject *context, std::function<void(bool, const QString &)> callback)
{
    QNetworkReply *reply = m_avTransport.GetTransportSettings(0);
    QObject::connect(reply, &QNetworkReply::finished, context, [this, reply, callback]() {
        SoapResponse response(reply);
        reply->deleteLater();

        if (response.error()) {
            QWARN() << roomName() << "GetTransportSettings failed:" << response.faultString();
            if (callback)
                callback(false, QString());
            return;
        }

        if (callback)
            callback(true, response.value(QStringLiteral("CurrentPlayMode")));
    });
}

void SonosZoneControl::getCrossfadeMode(QObject *context, std::function<void(bool, bool)> callback)
{
    QNetworkReply *reply = m_avTransport.GetCrossfadeMode(0);
    QObject::connect(reply, &QNetworkReply::finished, context, [this, reply, callback]() {
        SoapResponse response(reply);
        reply->deleteLater();

        if (response.error()) {
            QWARN() << roomName() << "GetCrossfadeMode failed:" << response.faultString();
            if (callback)
                callback(false, false);
            return;
        }

        if (callback)
            callback(true, response.value(QStringLiteral("CurrentCrossfadeMode")) == QStringLiteral("1"));
    });
}

void SonosZoneControl::getPositionInfo(QObject *context, std::function<void(bool, const PositionInfo &)> callback)
{
    QNetworkReply *reply = m_avTransport.GetPositionInfo(0);
    QObject::connect(reply, &QNetworkReply::finished, context, [reply, callback]() {
        SoapResponse response(reply);
        reply->deleteLater();

        PositionInfo info;
        if (response.error()) {
            if (callback)
                callback(false, info);
            return;
        }

        info.trackMetaData = response.value(QStringLiteral("TrackMetaData"));
        info.trackUri = response.value(QStringLiteral("TrackURI"));
        info.relTime = response.value(QStringLiteral("RelTime"));
        info.trackDuration = response.value(QStringLiteral("TrackDuration"));
        info.trackNumber = response.value(QStringLiteral("Track")).toInt(&info.trackNumberKnown);
        if (callback)
            callback(true, info);
    });
}

}
