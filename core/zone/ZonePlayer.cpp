#include "ZonePlayer.h"

#include <algorithm>

#include <QImage>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QUrl>
#include <QXmlStreamReader>

#include "../Logging.h"
#include "../media/AlbumColorAnalyzer.h"
#include "../upnp/SoapResponse.h"

#define QLOG_CATEGORY logZone

namespace RoomTunes {

namespace {

QString genaProperty(const QByteArray &body, const QString &propertyName);

// UPnP RelTime/TrackDuration are "H+:MM:SS" (one or more hour digits) --
// parses to total seconds, or 0 for anything that doesn't fit that shape
// (e.g. the literal "NOT_IMPLEMENTED" some sources report when they don't
// track position, such as line-in or certain radio streams).
int parseUpnpTime(const QString &text)
{
    const QStringList parts = text.split(QLatin1Char(':'));
    if (parts.size() != 3)
        return 0;

    bool hoursOk = false, minutesOk = false, secondsOk = false;
    const int hours = parts.at(0).toInt(&hoursOk);
    const int minutes = parts.at(1).toInt(&minutesOk);
    const int seconds = parts.at(2).toInt(&secondsOk);
    if (!hoursOk || !minutesOk || !secondsOk)
        return 0;

    return hours * 3600 + minutes * 60 + seconds;
}

QString formatUpnpTime(int totalSeconds)
{
    totalSeconds = std::max(totalSeconds, 0);
    const int hours = totalSeconds / 3600;
    const int minutes = (totalSeconds % 3600) / 60;
    const int seconds = totalSeconds % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(hours)
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

bool isTvStreamUri(const QString &uri)
{
    return uri.startsWith(QStringLiteral("x-sonos-htastream:"));
}

bool isLineInStreamUri(const QString &uri)
{
    return uri.startsWith(QStringLiteral("x-rincon-stream:"));
}

QString tvInputNameFromUri(const QString &uri)
{
    const int separator = uri.lastIndexOf(QLatin1Char(':'));
    if (separator < 0 || separator == uri.size() - 1)
        return {};

    const QString input = uri.mid(separator + 1);
    if (input.compare(QStringLiteral("spdif"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("SPDIF");

    return input.toUpper();
}

QString tvAudioInfoFromStreamCode(const QString &streamInfo)
{
    bool ok = false;
    const int code = streamInfo.trimmed().toInt(&ok);
    if (!ok)
        return {};

    // Sonos exposes TV input details as a Rincon stream code in
    // CurrentTrackMetaData/r:streamInfo. The old Playbar capture has 19 for
    // the SPDIF input; newer HT players also report codec-style HT audio
    // codes here/status endpoints. Keep this mapping deliberately explicit so
    // unknown firmware values fall back to the URI input name instead.
    switch (code) {
    case 19:
        return QStringLiteral("SPDIF");
    case 22:
        return QStringLiteral("Silence");
    case 33554434:
        return QStringLiteral("Stereo PCM 2.0");
    case 33554488:
        return QStringLiteral("Dolby Digital 2.0");
    case 84934713:
        return QStringLiteral("Dolby Digital 5.1");
    case 84934714:
        return QStringLiteral("Dolby Digital Plus 5.1");
    case 32:
    case 84934721:
        return QStringLiteral("DTS");
    case 59:
    case 63:
        return QStringLiteral("Dolby Atmos");
    default:
        return {};
    }
}

QString tvAudioInfoFromMetadata(const QString &trackMetaData, const QString &trackUri)
{
    const QList<DidlItem> items = Didl::parseItems(trackMetaData.toUtf8());
    const QString streamInfo = items.isEmpty() ? QString() : items.first().streamInfo;
    const QString audioInfo = tvAudioInfoFromStreamCode(streamInfo);
    return audioInfo.isEmpty() ? tvInputNameFromUri(trackUri) : audioInfo;
}

struct AvTransportTrackSnapshot
{
    QString uri;
    QString metadata;
};

AvTransportTrackSnapshot avTransportTrackSnapshot(const QByteArray &body)
{
    AvTransportTrackSnapshot snapshot;
    const QString lastChange = genaProperty(body, QStringLiteral("LastChange"));
    if (lastChange.isEmpty())
        return snapshot;

    QXmlStreamReader xml(lastChange);
    while (!xml.atEnd()) {
        if (!xml.readNextStartElement())
            continue;

        if (xml.name() == QLatin1String("CurrentTrackURI"))
            snapshot.uri = xml.attributes().value(QStringLiteral("val")).toString();
        else if (xml.name() == QLatin1String("CurrentTrackMetaData"))
            snapshot.metadata = xml.attributes().value(QStringLiteral("val")).toString();
    }

    return snapshot;
}

// Shared by playItem()/playItemNext()/addItemToQueue()/replaceQueueWithItem()
// -- builds the DIDL <item> metadata for a browse/search result item (see
// MusicService subclasses for the id/title/upnpClass/didlId/parentId/desc
// shape).
QByteArray buildItemMetadata(const QVariantMap &item)
{
    const QString upnpClass = item.value(QStringLiteral("upnpClass")).toString();
    const QString title = item.value(QStringLiteral("title")).toString();
    QString didlId = item.value(QStringLiteral("didlId")).toString();
    if (didlId.isEmpty())
        didlId = item.value(QStringLiteral("id")).toString();
    const QString parentId = item.value(QStringLiteral("parentId")).toString();
    const QString desc = item.value(QStringLiteral("desc")).toString();
    return desc.isEmpty() ? Didl::buildItem(didlId, parentId, title, upnpClass)
                           : Didl::buildItem(didlId, parentId, title, upnpClass, desc);
}

QString soapErrorDetail(const SoapResponse &response)
{
    return response.upnpErrorCode().isEmpty()
        ? response.faultString()
        : response.upnpErrorCode() + QStringLiteral(" ") + response.upnpErrorDescription();
}

QString genaProperty(const QByteArray &body, const QString &propertyName)
{
    QXmlStreamReader xml(body);

    while (!xml.atEnd()) {
        if (!xml.readNextStartElement())
            continue;
        if (xml.name() != QLatin1String("property"))
            continue;
        if (xml.readNextStartElement() && xml.name() == propertyName)
            return xml.readElementText(QXmlStreamReader::SkipChildElements);
    }

    return {};
}

}

ZonePlayer::ZonePlayer(QNetworkAccessManager *netMgr, const QString &deviceIp, const QString &udn, QObject *parent)
    : QObject(parent)
    , m_netMgr(netMgr)
    , m_deviceIp(deviceIp)
    , m_udn(udn)
    , m_coordinatorUdn(udn)
    , m_avTransport(netMgr, deviceIp)
    , m_renderingControl(netMgr, deviceIp)
    , m_contentDirectory(netMgr, deviceIp)
    , m_audioIn(netMgr, deviceIp)
    , m_deviceProperties(netMgr, deviceIp)
    , m_zoneGroupTopology(netMgr, deviceIp)
    , m_musicServices(netMgr, deviceIp)
    , m_systemProperties(netMgr, deviceIp)
{
}

void ZonePlayer::setRoomName(const QString &name)
{
    if (m_roomName != name) {
        m_roomName = name;
        emit roomNameChanged();
    }
}

void ZonePlayer::setCoordinatorUdn(const QString &udn)
{
    if (m_coordinatorUdn != udn) {
        m_coordinatorUdn = udn;
        emit coordinatorChanged();
    }
}

void ZonePlayer::setInvisible(bool invisible)
{
    if (m_invisible != invisible) {
        m_invisible = invisible;
        emit invisibleChanged();
    }
}

void ZonePlayer::setReady(bool ready)
{
    if (m_ready != ready) {
        m_ready = ready;
        emit readyChanged();
    }
}

void ZonePlayer::setPlayState(PlayState state)
{
    if (m_playState != state) {
        m_playState = state;
        emit playStateChanged();
    }
}

QString ZonePlayer::playStateText() const
{
    switch (m_playState) {
    case PlayState::Playing:
        return QStringLiteral("Playing");
    case PlayState::Paused:
        return QStringLiteral("Paused");
    case PlayState::Transitioning:
        return QStringLiteral("Buffering");
    case PlayState::Stopped:
    default:
        return QStringLiteral("Stopped");
    }
}

void ZonePlayer::setCurrentTrack(MediaItem *track)
{
    if (m_currentTrack)
        m_currentTrack->deleteLater();
    m_currentTrack = track;
    emit currentTrackChanged();
    refreshAccentColor(track ? track->imageUrl() : QString());
}

void ZonePlayer::refreshAccentColor(const QString &imageUrl)
{
    m_accentColorRequestUrl = imageUrl;

    if (imageUrl.isEmpty()) {
        if (m_accentColor.isValid()) {
            m_accentColor = QColor();
            emit accentColorChanged();
        }
        return;
    }

    QNetworkReply *reply = m_netMgr->get(QNetworkRequest(QUrl(imageUrl)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, imageUrl]() {
        reply->deleteLater();

        // The selected track (and thus its art) may have changed again
        // while this request was in flight -- a stale reply landing after
        // that shouldn't override the color for the new track.
        if (m_accentColorRequestUrl != imageUrl)
            return;

        if (reply->error() != QNetworkReply::NoError)
            return;

        QImage image;
        if (!image.loadFromData(reply->readAll()))
            return;

        const QColor color = AlbumColorAnalyzer::pickAccentColor(image);
        if (color != m_accentColor) {
            m_accentColor = color;
            emit accentColorChanged();
        }
    });
}

void ZonePlayer::play()
{
    QNetworkReply *reply = m_avTransport.Play(0);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        SoapResponse response(reply);
        reply->deleteLater();

        if (!response.error())
            setPlayState(PlayState::Playing);
        else
            QWARN() << m_roomName << "Play failed:" << response.faultString();
    });
}

void ZonePlayer::pause()
{
    QNetworkReply *reply = m_avTransport.Pause(0);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        SoapResponse response(reply);
        reply->deleteLater();

        if (!response.error())
            setPlayState(PlayState::Paused);
        else
            QWARN() << m_roomName << "Pause failed:" << response.faultString();
    });
}

void ZonePlayer::next()
{
    QNetworkReply *reply = m_avTransport.Next(0);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        SoapResponse response(reply);
        reply->deleteLater();

        if (!response.error())
            refreshTransportState();
        else
            QWARN() << m_roomName << "Next failed:" << response.faultString();
    });
}

void ZonePlayer::previous()
{
    QNetworkReply *reply = m_avTransport.Previous(0);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        SoapResponse response(reply);
        reply->deleteLater();

        if (!response.error())
            refreshTransportState();
        else
            QWARN() << m_roomName << "Previous failed:" << response.faultString();
    });
}

void ZonePlayer::joinGroup(ZonePlayer *targetCoordinator)
{
    if (!targetCoordinator) {
        QWARN() << m_roomName << "joinGroup: null target";
        return;
    }

    const QString uri = QStringLiteral("x-rincon:%1").arg(targetCoordinator->udn());
    setAVTransportUri(uri, QString(), [this, targetCoordinatorRoomName = targetCoordinator->roomName()](bool ok) {
        if (!ok)
            QWARN() << m_roomName << "joinGroup: SetAVTransportURI to" << targetCoordinatorRoomName << "failed";
    });
}

void ZonePlayer::leaveGroup()
{
    QNetworkReply *reply = m_avTransport.BecomeCoordinatorOfStandaloneGroup(0);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        SoapResponse response(reply);
        reply->deleteLater();

        if (response.error())
            QWARN() << m_roomName << "BecomeCoordinatorOfStandaloneGroup failed:" << response.faultString();
    });
}

void ZonePlayer::setAVTransportUri(const QString &uri, const QString &metaData, std::function<void(bool)> callback)
{
    QNetworkReply *reply = m_avTransport.SetAVTransportURI(0, uri, metaData);
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        SoapResponse response(reply);
        reply->deleteLater();

        const bool ok = !response.error();
        if (!ok)
            QWARN() << m_roomName << "SetAVTransportURI failed:" << soapErrorDetail(response);
        if (callback)
            callback(ok);
    });
}

void ZonePlayer::addUriToQueue(const QString &uri, const QString &metaData, int desiredFirstTrackNumberEnqueued, bool enqueueAsNext,
                                std::function<void(bool ok, int firstTrackNumberEnqueued)> callback)
{
    QNetworkReply *reply = m_avTransport.AddURIToQueue(0, uri, metaData, desiredFirstTrackNumberEnqueued, enqueueAsNext);
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        SoapResponse response(reply);
        reply->deleteLater();

        if (response.error()) {
            QWARN() << m_roomName << "AddURIToQueue failed:" << soapErrorDetail(response);
            if (callback)
                callback(false, 0);
            return;
        }

        emit queueChanged();
        if (callback)
            callback(true, response.value(QStringLiteral("FirstTrackNumberEnqueued")).toInt());
    });
}

void ZonePlayer::removeAllTracksFromQueue(std::function<void(bool)> callback)
{
    QNetworkReply *reply = m_avTransport.RemoveAllTracksFromQueue(0);
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        SoapResponse response(reply);
        reply->deleteLater();

        const bool ok = !response.error();
        if (!ok)
            QWARN() << m_roomName << "RemoveAllTracksFromQueue failed:" << soapErrorDetail(response);
        else
            emit queueChanged();
        if (callback)
            callback(ok);
    });
}

void ZonePlayer::playItem(const QVariantMap &item)
{
    const QString uri = item.value(QStringLiteral("uri")).toString();
    if (uri.isEmpty()) {
        QWARN() << m_roomName << "playItem: no playable uri for" << item.value(QStringLiteral("title")).toString();
        return;
    }

    const QString upnpClass = item.value(QStringLiteral("upnpClass")).toString();
    const QByteArray metaData = buildItemMetadata(item);

    if (upnpClass.endsWith(QStringLiteral(".audioBroadcast"))) {
        // A stream isn't queueable -- swap the transport straight to it.
        setAVTransportUri(uri, QString::fromUtf8(metaData), [this](bool ok) {
            if (ok)
                play();
        });
        return;
    }

    // Enqueue right after whatever's currently playing, point the
    // transport at this zone's own queue, seek to the newly-enqueued
    // position, then play -- roomtunes-bb10's play_track().
    addUriToQueue(uri, QString::fromUtf8(metaData), /*desiredFirstTrackNumberEnqueued=*/0, /*enqueueAsNext=*/true,
                  [this](bool ok, int firstTrackNumberEnqueued) {
        if (ok)
            playQueueTrack(firstTrackNumberEnqueued);
    });
}

void ZonePlayer::playItemNext(const QVariantMap &item)
{
    const QString uri = item.value(QStringLiteral("uri")).toString();
    const QString upnpClass = item.value(QStringLiteral("upnpClass")).toString();
    if (uri.isEmpty() || upnpClass.endsWith(QStringLiteral(".audioBroadcast")))
        return;

    // roomtunes-bb10's play_next(): insert immediately after the current
    // queue track rather than just relying on EnqueueAsNext's default
    // placement.
    addUriToQueue(uri, QString::fromUtf8(buildItemMetadata(item)), m_currentTrackNumber + 1, /*enqueueAsNext=*/true);
}

void ZonePlayer::addItemToQueue(const QVariantMap &item)
{
    const QString uri = item.value(QStringLiteral("uri")).toString();
    const QString upnpClass = item.value(QStringLiteral("upnpClass")).toString();
    if (uri.isEmpty() || upnpClass.endsWith(QStringLiteral(".audioBroadcast")))
        return;

    addUriToQueue(uri, QString::fromUtf8(buildItemMetadata(item)), /*desiredFirstTrackNumberEnqueued=*/0,
                  /*enqueueAsNext=*/false);
}

void ZonePlayer::replaceQueueWithItem(const QVariantMap &item)
{
    removeAllTracksFromQueue([this, item](bool ok) {
        if (ok)
            playItem(item);
    });
}

void ZonePlayer::removeQueueTrack(const QString &objectId)
{
    // UpdateID 0 -- Sonos treats it as "don't care", same as
    // roomtunes-bb10's remove_track() always passed.
    QNetworkReply *reply = m_avTransport.RemoveTrackFromQueue(0, objectId, 0);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        SoapResponse response(reply);
        reply->deleteLater();

        if (response.error()) {
            QWARN() << m_roomName << "RemoveTrackFromQueue failed:" << soapErrorDetail(response);
            return;
        }
        emit queueChanged();
    });
}

void ZonePlayer::playQueueTrack(int trackNumber)
{
    // Unconditionally pointed at the queue rather than first checking
    // whether it's already there (roomtunes-bb10's skipto_track() checks
    // first, purely to skip a redundant round trip) -- simpler, and
    // re-selecting the same source a zone is already on doesn't restart it.
    setAVTransportUri(queueUri(), QString(), [this, trackNumber](bool ok) {
        if (!ok)
            return;

        QNetworkReply *reply = m_avTransport.Seek(0, QStringLiteral("TRACK_NR"), QString::number(trackNumber));
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            SoapResponse response(reply);
            reply->deleteLater();

            if (response.error()) {
                QWARN() << m_roomName << "Seek failed:" << soapErrorDetail(response);
                return;
            }
            play();
        });
    });
}

void ZonePlayer::setVolume(int level)
{
    QNetworkReply *reply = m_renderingControl.SetVolume(0, QStringLiteral("Master"), level);
    connect(reply, &QNetworkReply::finished, this, [this, reply, level]() {
        SoapResponse response(reply);
        reply->deleteLater();

        if (!response.error() && level != m_volume) {
            m_volume = level;
            emit volumeChanged();
        }
    });
}

void ZonePlayer::setMuted(bool muted)
{
    // Optimistic: the mute icon should flip the instant it's clicked, not
    // after a round trip to the zone -- SetMute on a healthy LAN connection
    // essentially never fails, so there's little practical downside, and
    // the failure path below still logs (and leaves the optimistic state in
    // place rather than snapping back, which would be a worse experience
    // for the common case to guard against a rare one).
    if (m_muted != muted) {
        m_muted = muted;
        emit mutedChanged();
    }

    QNetworkReply *reply = m_renderingControl.SetMute(0, QStringLiteral("Master"), muted);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        SoapResponse response(reply);
        reply->deleteLater();

        if (response.error())
            QWARN() << m_roomName << "SetMute failed:" << response.faultString();
    });
}

void ZonePlayer::browse(const QString &objectId, std::function<void(bool, const QString &, const QList<DidlItem> &)> callback,
                         int startingIndex, int requestedCount, const QString &browseFlag)
{
    QNetworkReply *reply = m_contentDirectory.Browse(objectId, browseFlag, QStringLiteral("*"), startingIndex, requestedCount);
    connect(reply, &QNetworkReply::finished, this, [this, reply, objectId, callback]() {
        SoapResponse response(reply);
        reply->deleteLater();

        if (response.error()) {
            // upnpErrorCode/Description (e.g. "701"/"No such object") is
            // the actionable detail when present -- faultString alone is
            // usually just the generic "UPnPError" SOAP fault string.
            const QString detail = response.upnpErrorCode().isEmpty()
                ? response.faultString()
                : response.upnpErrorCode() + QStringLiteral(" ") + response.upnpErrorDescription();
            QWARN() << m_roomName << "Browse failed: objectId=" << objectId << "httpStatus=" << response.httpStatusCode()
                    << "error=" << detail;
            if (callback)
                callback(false, detail, {});
            return;
        }

        const QList<DidlItem> items = Didl::parseItems(response.value(QStringLiteral("Result")).toUtf8());
        if (callback)
            callback(true, QString(), items);
    });
}

void ZonePlayer::refreshVolume()
{
    QNetworkReply *reply = m_renderingControl.GetVolume(0);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        SoapResponse response(reply);
        reply->deleteLater();

        if (response.error())
            return;

        bool ok = false;
        const int level = response.value(QStringLiteral("CurrentVolume")).toInt(&ok);
        if (ok && level != m_volume) {
            m_volume = level;
            emit volumeChanged();
        }
    });
}

void ZonePlayer::refreshMute()
{
    QNetworkReply *reply = m_renderingControl.GetMute(0);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        SoapResponse response(reply);
        reply->deleteLater();

        if (response.error())
            return;

        const bool muted = response.value(QStringLiteral("CurrentMute")) == QStringLiteral("1");
        if (muted != m_muted) {
            m_muted = muted;
            emit mutedChanged();
        }
    });
}

void ZonePlayer::handleRenderingControlEvent(const QByteArray &body)
{
    const QString lastChange = genaProperty(body, QStringLiteral("LastChange"));
    if (lastChange.isEmpty())
        return;

    QXmlStreamReader xml(lastChange);
    while (!xml.atEnd()) {
        if (!xml.readNextStartElement())
            continue;

        if (xml.name() == QLatin1String("Volume")
            && xml.attributes().value(QStringLiteral("channel")) == QLatin1String("Master")) {
            bool ok = false;
            const int level = xml.attributes().value(QStringLiteral("val")).toInt(&ok);
            if (ok && level != m_volume) {
                m_volume = level;
                emit volumeChanged();
            }
        } else if (xml.name() == QLatin1String("Mute")
                   && xml.attributes().value(QStringLiteral("channel")) == QLatin1String("Master")) {
            const bool muted = xml.attributes().value(QStringLiteral("val")) == QLatin1String("1");
            if (muted != m_muted) {
                m_muted = muted;
                emit mutedChanged();
            }
        }
    }
}

void ZonePlayer::handleAVTransportEvent(const QByteArray &body)
{
    const AvTransportTrackSnapshot snapshot = avTransportTrackSnapshot(body);
    if (isTvStreamUri(snapshot.uri)) {
        const QString audioInfo = tvAudioInfoFromMetadata(snapshot.metadata, snapshot.uri);
        if (!audioInfo.isEmpty() && m_tvAudioInfo != audioInfo)
            m_tvAudioInfo = audioInfo;
    } else if (!snapshot.uri.isEmpty() && !isTvStreamUri(snapshot.uri)) {
        m_tvAudioInfo.clear();
    }

    refreshTransportState();
}

void ZonePlayer::handleContentDirectoryEvent(const QByteArray &)
{
    emit queueChanged();
}

void ZonePlayer::handleAudioInEvent(const QByteArray &)
{
}

void ZonePlayer::refreshTransportState()
{
    QNetworkReply *transportReply = m_avTransport.GetTransportInfo(0);
    connect(transportReply, &QNetworkReply::finished, this, [this, transportReply]() {
        SoapResponse response(transportReply);
        transportReply->deleteLater();

        if (response.error()) {
            QWARN() << m_roomName << "GetTransportInfo failed:" << response.faultString();
            return;
        }

        const QString state = response.value(QStringLiteral("CurrentTransportState"));
        if (state == QStringLiteral("PLAYING"))
            setPlayState(PlayState::Playing);
        else if (state == QStringLiteral("PAUSED_PLAYBACK"))
            setPlayState(PlayState::Paused);
        else if (state == QStringLiteral("TRANSITIONING"))
            setPlayState(PlayState::Transitioning);
        else
            setPlayState(PlayState::Stopped);
    });

    QNetworkReply *positionReply = m_avTransport.GetPositionInfo(0);
    connect(positionReply, &QNetworkReply::finished, this, [this, positionReply]() {
        SoapResponse response(positionReply);
        positionReply->deleteLater();

        if (response.error())
            return;

        const QString trackMetaData = response.value(QStringLiteral("TrackMetaData"));
        const QString trackUri = response.value(QStringLiteral("TrackURI"));
        bool trackNumberOk = false;
        const int trackNumber = response.value(QStringLiteral("Track")).toInt(&trackNumberOk);
        if (trackNumberOk)
            m_currentTrackNumber = trackNumber;

        QList<DidlItem> items = Didl::parseItems(trackMetaData.toUtf8());
        DidlItem didl = items.isEmpty() ? DidlItem{} : items.first();
        if (didl.res.isEmpty())
            didl.res = trackUri;

        // Local-library album art comes back as a path relative to the
        // zone itself (e.g. "/getaa?..."); streaming-service art is
        // already an absolute URL. Resolve the former against this zone's
        // own address so QML's Image can just load it directly.
        if (!didl.albumArtUri.isEmpty() && didl.albumArtUri.startsWith(QLatin1Char('/')))
            didl.albumArtUri = baseUrl().chopped(1) + didl.albumArtUri;

        const QString sourceUri = trackUri.isEmpty() ? didl.res : trackUri;
        QString sourceTitle;
        QString sourceArtist;
        QString sourceImageUrl;
        if (isTvStreamUri(sourceUri)) {
            // BB10 detected Playbar/Beam TV input from AVTransportURI and
            // displayed a synthetic "TV" track with the bundled TV icon. The
            // Qt 6 app polls TrackURI here, which carries the same Sonos URI.
            sourceTitle = tr("TV");
            sourceArtist = m_tvAudioInfo.isEmpty() ? tvInputNameFromUri(sourceUri) : m_tvAudioInfo;
            sourceImageUrl = QStringLiteral("qrc:/qt/qml/RoomTunes/resources/icons/tv.svg");
        } else if (isLineInStreamUri(sourceUri)) {
            sourceTitle = didl.title.isEmpty() ? tr("Line-In") : didl.title;
            sourceImageUrl = QStringLiteral("qrc:/qt/qml/RoomTunes/resources/icons/line_in.svg");
        }

        if (!sourceTitle.isEmpty()) {
            if (!m_currentTrack || m_currentTrack->id() != sourceUri || m_currentTrack->uri() != sourceUri
                || m_currentTrack->title() != sourceTitle || m_currentTrack->artist() != sourceArtist
                || m_currentTrack->imageUrl() != sourceImageUrl) {
                setCurrentTrack(new MediaItem(sourceUri, QString(), sourceTitle, sourceArtist, QString(), QString(),
                                              sourceUri, QStringLiteral("object.item.audioItem"), sourceImageUrl,
                                              false, this));
            }
            setPosition(parseUpnpTime(response.value(QStringLiteral("RelTime"))),
                        parseUpnpTime(response.value(QStringLiteral("TrackDuration"))));
            return;
        }

        // GetPositionInfo is polled every second while playing (see
        // NowPlayingPanel.qml) purely to track playback position --
        // rebuilding/reassigning currentTrack (and re-fetching its accent
        // color over the network) on every one of those ticks would be
        // wasteful and pointless when it's still the same track.
        if (!m_currentTrack || m_currentTrack->id() != didl.id || m_currentTrack->uri() != didl.res)
            setCurrentTrack(MediaItem::fromDidl(didl, this));

        setPosition(parseUpnpTime(response.value(QStringLiteral("RelTime"))),
                    parseUpnpTime(response.value(QStringLiteral("TrackDuration"))));
    });
}

void ZonePlayer::setPosition(int positionSeconds, int durationSeconds)
{
    if (m_positionSeconds != positionSeconds || m_durationSeconds != durationSeconds) {
        m_positionSeconds = positionSeconds;
        m_durationSeconds = durationSeconds;
        emit positionChanged();
    }
}

void ZonePlayer::seek(qreal fraction)
{
    if (m_durationSeconds <= 0)
        return;

    const int targetSeconds = qBound(0, int(fraction * m_durationSeconds), m_durationSeconds);
    QNetworkReply *reply = m_avTransport.Seek(0, QStringLiteral("REL_TIME"), formatUpnpTime(targetSeconds));
    connect(reply, &QNetworkReply::finished, this, [this, reply, targetSeconds]() {
        SoapResponse response(reply);
        reply->deleteLater();

        if (response.error()) {
            QWARN() << m_roomName << "Seek failed:" << response.faultString();
            return;
        }

        // Optimistic -- reflects the seek immediately rather than waiting
        // for the next once-a-second GetPositionInfo poll to catch up.
        setPosition(targetSeconds, m_durationSeconds);
    });
}

}
