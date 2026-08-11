#include "ZonePlayer.h"

#include <algorithm>
#include <utility>

#include <QImage>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QUrl>

#include "../Logging.h"
#include "../control/SonosPlaybackPayload.h"
#include "../media/AlbumColorAnalyzer.h"
#include "../xml/XmlUtils.h"

#define QLOG_CATEGORY logZone

namespace RoomTunes
{

namespace
{

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

    bool      hoursOk = false, minutesOk = false, secondsOk = false;
    const int hours   = parts.at(0).toInt(&hoursOk);
    const int minutes = parts.at(1).toInt(&minutesOk);
    const int seconds = parts.at(2).toInt(&secondsOk);
    if (!hoursOk || !minutesOk || !secondsOk)
        return 0;

    return hours * 3600 + minutes * 60 + seconds;
}

QString formatUpnpTime(int totalSeconds)
{
    totalSeconds      = std::max(totalSeconds, 0);
    const int hours   = totalSeconds / 3600;
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

bool modelSupportsTvSource(const QString &modelName)
{
    // BB10 only checked for PLAYBAR because that was the only Sonos TV
    // source product at the time. Modern home-theater players use the same
    // x-sonos-htastream URI family, so keep this as a product capability
    // list until we parse a richer device-description service capability.
    return modelName.contains(QStringLiteral("PLAYBAR"), Qt::CaseInsensitive) ||
           modelName.contains(QStringLiteral("PLAYBASE"), Qt::CaseInsensitive) ||
           modelName.contains(QStringLiteral("Beam"), Qt::CaseInsensitive) ||
           modelName.contains(QStringLiteral("Arc"), Qt::CaseInsensitive) ||
           modelName.contains(QStringLiteral("Ray"), Qt::CaseInsensitive) ||
           modelName.compare(QStringLiteral("Amp"), Qt::CaseInsensitive) == 0 ||
           modelName.contains(QStringLiteral("Sonos Amp"), Qt::CaseInsensitive);
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
    bool      ok   = false;
    const int code = streamInfo.trimmed().toInt(&ok);
    if (!ok)
        return {};

    // Sonos exposes TV input details as a Rincon stream code in
    // CurrentTrackMetaData/r:streamInfo. The old Playbar capture has 19 for
    // the SPDIF input; newer HT players also report codec-style HT audio
    // codes here/status endpoints. Keep this mapping deliberately explicit so
    // unknown firmware values fall back to the URI input name instead.
    switch (code)
    {
    case 19:        return QStringLiteral("SPDIF");
    case 22:        return QStringLiteral("Silence");
    case 33554434:  return QStringLiteral("Stereo PCM 2.0");
    case 33554488:  return QStringLiteral("Dolby Digital 2.0");
    case 84934713:  return QStringLiteral("Dolby Digital 5.1");
    case 84934714:  return QStringLiteral("Dolby Digital Plus 5.1");
    case 32:
    case 84934721:  return QStringLiteral("DTS");
    case 59:
    case 63:        return QStringLiteral("Dolby Atmos");
    default:        return {};
    }
}

QString tvAudioInfoFromMetadata(const QString &trackMetaData, const QString &trackUri)
{
    const QList<DidlItem> items      = Didl::parseItems(trackMetaData.toUtf8());
    const QString         streamInfo = items.isEmpty() ? QString() : items.first().streamInfo;
    const QString         audioInfo  = tvAudioInfoFromStreamCode(streamInfo);

    return audioInfo.isEmpty() ? tvInputNameFromUri(trackUri) : audioInfo;
}

struct AvTransportTrackSnapshot
{
    QString transportState;
    QString playMode;
    QString crossfadeMode;
    QString uri;
    QString metadata;
};

AvTransportTrackSnapshot avTransportTrackSnapshot(const QByteArray &body)
{
    AvTransportTrackSnapshot snapshot;
    const QString            lastChange = genaProperty(body, QStringLiteral("LastChange"));
    if (lastChange.isEmpty())
        return snapshot;

    const XmlDoc doc        = XmlDoc::parse(lastChange.toUtf8());
    snapshot.transportState = doc.first("//TransportState").attr("val");
    snapshot.playMode       = doc.first("//CurrentPlayMode").attr("val");
    snapshot.crossfadeMode  = doc.first("//CurrentCrossfadeMode").attr("val");
    snapshot.uri            = doc.first("//CurrentTrackURI").attr("val");
    snapshot.metadata       = doc.first("//CurrentTrackMetaData").attr("val");

    return snapshot;
}

QString genaProperty(const QByteArray &body, const QString &propertyName)
{
    return XmlDoc::parse(body).firstText(propertyName);
}

QString playModeFor(bool shuffleEnabled, int repeatMode)
{
    if (shuffleEnabled)
    {
        if (repeatMode == 2)            return QStringLiteral("SHUFFLE_REPEAT_ONE");
        if (repeatMode == 1)            return QStringLiteral("SHUFFLE");
        return QStringLiteral("SHUFFLE_NOREPEAT");
    }

    if (repeatMode == 2)        return QStringLiteral("REPEAT_ONE");
    if (repeatMode == 1)        return QStringLiteral("REPEAT_ALL");
    return QStringLiteral("NORMAL");
}

} // namespace

ZonePlayer::ZonePlayer(QNetworkAccessManager *netMgr, const QString &deviceIp, const QString &udn, QObject *parent)
    : QObject(parent), m_netMgr(netMgr), m_deviceIp(deviceIp), m_udn(udn), m_coordinatorUdn(udn),
      m_avTransport(netMgr, deviceIp), m_renderingControl(netMgr, deviceIp), m_contentDirectory(netMgr, deviceIp),
      m_audioIn(netMgr, deviceIp), m_queue(netMgr, deviceIp), m_deviceProperties(netMgr, deviceIp),
      m_zoneGroupTopology(netMgr, deviceIp), m_musicServices(netMgr, deviceIp), m_systemProperties(netMgr, deviceIp),
      m_control(m_avTransport, m_renderingControl, m_contentDirectory, m_queue, [this]() {
          return m_roomName;
      })
{
}

void ZonePlayer::setRoomName(const QString &name)
{
    if (m_roomName != name)
    {
        m_roomName = name;
        emit roomNameChanged();
    }
}

void ZonePlayer::setModelName(const QString &name)
{
    if (m_modelName == name)
        return;

    const bool oldSupportsTvSource = supportsTvSource();
    m_modelName                    = name;

    if (oldSupportsTvSource != supportsTvSource())
        emit supportsTvSourceChanged();
}

bool ZonePlayer::supportsTvSource() const
{
    if (modelSupportsTvSource(m_modelName))
        return true;
    return m_currentTrack && isTvStreamUri(m_currentTrack->uri());
}

void ZonePlayer::setDeviceDescriptionDetails(const QString &displayName, const QString &displayVersion,
                                             const QString &softwareVersion, const QString &zoneType,
                                             const QStringList &features)
{
    m_displayName     = displayName;
    m_displayVersion  = displayVersion;
    m_softwareVersion = softwareVersion;
    m_zoneType        = zoneType;
    m_features        = features;
}

void ZonePlayer::setDeviceServices(const QSet<QString> &services)
{
    if (m_deviceServices == services)
        return;

    const bool oldSupportsLineInSource = supportsLineInSource();
    m_deviceServices                   = services;

    if (oldSupportsLineInSource != supportsLineInSource())
        emit supportsLineInSourceChanged();
}

bool ZonePlayer::hasDeviceService(const QString &serviceName) const
{
    return m_deviceServices.contains(serviceName);
}

QStringList ZonePlayer::deviceServices() const
{
    QStringList services;

    for (const QString &service : m_deviceServices)
        services.append(service);

    services.sort(Qt::CaseInsensitive);
    return services;
}

bool ZonePlayer::supportsLineInSource() const
{
    // Same capability test as roomtunes-bb10's SonosLibrary::browseLineIn():
    // only zones that expose AudioIn and can browse ContentDirectory's AI:
    // container should be offered as Line-In sources.
    return hasDeviceService(QStringLiteral("AudioIn")) && hasDeviceService(QStringLiteral("ContentDirectory"));
}

void ZonePlayer::setCoordinatorUdn(const QString &udn)
{
    if (m_coordinatorUdn != udn)
    {
        m_coordinatorUdn = udn;
        emit coordinatorChanged();
    }
}

void ZonePlayer::setInvisible(bool invisible)
{
    if (m_invisible != invisible)
    {
        m_invisible = invisible;
        emit invisibleChanged();
    }
}

void ZonePlayer::setReady(bool ready)
{
    if (m_ready != ready)
    {
        m_ready = ready;
        emit readyChanged();
    }
}

void ZonePlayer::setPlayState(PlayState state)
{
    if (m_playState != state)
    {
        m_playState = state;
        emit playStateChanged();
    }
}

QString ZonePlayer::playStateText() const
{
    switch (m_playState)
    {
    case PlayState::Playing:        return QStringLiteral("Playing");
    case PlayState::Paused:         return QStringLiteral("Paused");
    case PlayState::Transitioning:  return QStringLiteral("Buffering");
    case PlayState::Stopped:
    default:                        return QStringLiteral("Stopped");
    }
}

bool ZonePlayer::shuffleEnabled() const
{
    return m_playMode.startsWith(QStringLiteral("SHUFFLE"));
}

int ZonePlayer::repeatMode() const
{
    if (m_playMode.endsWith(QStringLiteral("REPEAT_ONE")))
        return 2;

    if (m_playMode == QStringLiteral("REPEAT_ALL") || m_playMode == QStringLiteral("SHUFFLE"))
        return 1;
    return 0;
}

void ZonePlayer::setPlayMode(const QString &playMode)
{
    if (playMode.isEmpty() || m_playMode == playMode)
        return;

    const bool wasShuffleEnabled = shuffleEnabled();
    const int  oldRepeatMode     = repeatMode();
    m_playMode                   = playMode;

    if (wasShuffleEnabled != shuffleEnabled() || oldRepeatMode != repeatMode())
    {
        QLOG() << m_roomName << "play mode:" << m_playMode;
        emit playModeChanged();
    }
}

void ZonePlayer::setCrossfadeState(bool enabled, bool known)
{
    if (m_crossfadeEnabled == enabled && m_crossfadeKnown == known)
        return;

    m_crossfadeEnabled = enabled;
    m_crossfadeKnown   = known;

    QLOG() << m_roomName << "crossfade:" << m_crossfadeEnabled;
    emit crossfadeChanged();
}

void ZonePlayer::setCurrentTrack(MediaItem *track)
{
    const bool oldSupportsTvSource = supportsTvSource();

    if (m_currentTrack)
        m_currentTrack->deleteLater();
    m_currentTrack = track;

    emit currentTrackChanged();

    if (oldSupportsTvSource != supportsTvSource())
        emit supportsTvSourceChanged();

    refreshAccentColor(track ? track->imageUrl() : QString());
    checkCurrentTrackFavouriteStatus();
}

void ZonePlayer::checkCurrentTrackFavouriteStatus()
{
    if (!m_currentTrack || m_currentTrack->uri().isEmpty())
    {
        emit sonosFavouriteStatus(false, QString());
        return;
    }

    const QString trackUri     = m_currentTrack->uri();
    const int     q            = trackUri.indexOf(QLatin1Char('?'));
    const QString baseTrackUri = q >= 0 ? trackUri.left(q) : trackUri;

    browse(
        QStringLiteral("FV:2"),
        [this, trackUri, baseTrackUri](bool ok, const QString &, const QList<DidlItem> &items) {
            if (!m_currentTrack || m_currentTrack->uri() != trackUri)
                return;

            if (!ok)
            {
                emit sonosFavouriteStatus(false, QString());
                return;
            }

            for (const DidlItem &item : items)
            {
                const int     fq         = item.res.indexOf(QLatin1Char('?'));
                const QString baseFavUri = fq >= 0 ? item.res.left(fq) : item.res;

                if (baseFavUri == baseTrackUri)
                {
                    emit sonosFavouriteStatus(true, item.id);
                    return;
                }
            }
            emit sonosFavouriteStatus(false, QString());
        },
        0, 400);
}

void ZonePlayer::refreshAccentColor(const QString &imageUrl)
{
    m_accentColorRequestUrl = imageUrl;

    if (imageUrl.isEmpty())
    {
        if (m_accentColor.isValid())
        {
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
        if (color != m_accentColor)
        {
            m_accentColor = color;
            emit accentColorChanged();
        }
    });
}

void ZonePlayer::play()
{
    m_control.play(this, [this](bool ok) {
        if (ok)
            setPlayState(PlayState::Playing);
    });
}

void ZonePlayer::pause()
{
    m_control.pause(this, [this](bool ok) {
        if (ok)
            setPlayState(PlayState::Paused);
    });
}

void ZonePlayer::next()
{
    m_control.next(this, [this](bool ok) {
        if (ok)
            refreshTransportState();
    });
}

void ZonePlayer::previous()
{
    m_control.previous(this, [this](bool ok) {
        if (ok)
            refreshTransportState();
    });
}

void ZonePlayer::setShuffleEnabled(bool enabled)
{
    const QString requestedPlayMode = playModeFor(enabled, repeatMode());
    const QString previousPlayMode  = m_playMode;
    setPlayMode(requestedPlayMode);

    m_control.setPlayMode(this, requestedPlayMode, [this, previousPlayMode](bool ok) {
        if (!ok)
        {
            setPlayMode(previousPlayMode);
            return;
        }

        refreshTransportState();
    });
}

void ZonePlayer::cycleRepeatMode()
{
    const int     requestedRepeatMode = (repeatMode() + 1) % 3;
    const QString requestedPlayMode   = playModeFor(shuffleEnabled(), requestedRepeatMode);
    const QString previousPlayMode    = m_playMode;
    setPlayMode(requestedPlayMode);

    m_control.setPlayMode(this, requestedPlayMode, [this, previousPlayMode](bool ok) {
        if (!ok)
        {
            setPlayMode(previousPlayMode);
            return;
        }

        refreshTransportState();
    });
}

void ZonePlayer::setCrossfadeEnabled(bool enabled)
{
    const bool previousEnabled = m_crossfadeEnabled;
    const bool previousKnown   = m_crossfadeKnown;
    setCrossfadeState(enabled);

    m_control.setCrossfadeEnabled(this, enabled, [this, previousEnabled, previousKnown](bool ok) {
        if (!ok)
        {
            setCrossfadeState(previousEnabled, previousKnown);
            return;
        }

        refreshTransportState();
    });
}

void ZonePlayer::joinGroup(ZonePlayer *targetCoordinator)
{
    if (!targetCoordinator)
    {
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
    m_control.becomeCoordinatorOfStandaloneGroup(this, {});
}

void ZonePlayer::setAVTransportUri(const QString &uri, const QString &metaData, std::function<void(bool)> callback)
{
    m_control.setAVTransportUri(this, uri, metaData, std::move(callback));
}

void ZonePlayer::addUriToQueue(const QString &uri, const QString &metaData, int desiredFirstTrackNumberEnqueued,
                               bool enqueueAsNext, std::function<void(bool ok, int firstTrackNumberEnqueued)> callback)
{
    m_control.addUriToQueue(this, uri, metaData, desiredFirstTrackNumberEnqueued, enqueueAsNext,
                            [this, callback](bool ok, int firstTrackNumberEnqueued) {
                                if (!ok)
                                {
                                    if (callback)
                                        callback(false, 0);
                                    return;
                                }
                                emit queueChanged();
                                if (callback)
                                    callback(true, firstTrackNumberEnqueued);
                            });
}

void ZonePlayer::removeAllTracksFromQueue(std::function<void(bool)> callback)
{
    m_control.removeAllTracksFromQueue(this, [this, callback](bool ok) {
        if (ok)
            emit queueChanged();
        if (callback)
            callback(ok);
    });
}

void ZonePlayer::playItem(const QVariantMap &item)
{
    const QString uri = item.value(QStringLiteral("uri")).toString();
    if (uri.isEmpty())
    {
        QWARN() << m_roomName << "playItem: no playable uri for" << item.value(QStringLiteral("title")).toString();
        return;
    }

    const QByteArray metaData = SonosPlaybackPayload::buildItemMetadata(item);

    if (SonosPlaybackPayload::isStreamItem(item))
    {
        // A stream isn't queueable -- swap the transport straight to it.
        setAVTransportUri(uri, QString::fromUtf8(metaData), [this, item](bool ok) {
            if (ok)
            {
                emit playbackItemSelected(item);
                play();
            }
        });
        return;
    }

    // Enqueue right after whatever's currently playing, point the
    // transport at this zone's own queue, seek to the newly-enqueued
    // position, then play -- roomtunes-bb10's play_track().
    addUriToQueue(uri, QString::fromUtf8(metaData), /*desiredFirstTrackNumberEnqueued=*/0, /*enqueueAsNext=*/true,
                  [this, item](bool ok, int firstTrackNumberEnqueued) {
                      if (ok)
                          playQueueTrackInternal(firstTrackNumberEnqueued, item);
                  });
}

void ZonePlayer::playItemNext(const QVariantMap &item)
{
    const QString uri = item.value(QStringLiteral("uri")).toString();
    if (!SonosPlaybackPayload::isQueueableItem(item))
        return;

    // roomtunes-bb10's play_next(): insert immediately after the current
    // queue track rather than just relying on EnqueueAsNext's default
    // placement.
    addUriToQueue(uri, QString::fromUtf8(SonosPlaybackPayload::buildItemMetadata(item)), m_currentTrackNumber + 1,
                  /*enqueueAsNext=*/true);
}

void ZonePlayer::addItemToQueue(const QVariantMap &item)
{
    const QString uri = item.value(QStringLiteral("uri")).toString();
    if (!SonosPlaybackPayload::isQueueableItem(item))
        return;

    addUriToQueue(uri, QString::fromUtf8(SonosPlaybackPayload::buildItemMetadata(item)),
                  /*desiredFirstTrackNumberEnqueued=*/0, /*enqueueAsNext=*/false);
}

void ZonePlayer::replaceQueueWithItem(const QVariantMap &item)
{
    removeAllTracksFromQueue([this, item](bool ok) {
        if (ok)
            playItem(item);
    });
}

void ZonePlayer::clearQueue()
{
    removeAllTracksFromQueue([this](bool ok) {
        if (ok)
            emit queueChanged();
    });
}

void ZonePlayer::saveQueueAsSonosPlaylist(const QString &title)
{
    const QString trimmedTitle = title.trimmed();
    if (trimmedTitle.isEmpty())
        return;

    m_control.saveQueueAsSonosPlaylist(this, trimmedTitle, [](bool) {
    });
}

void ZonePlayer::addCurrentTrackToSonosFavourites()
{
    if (!m_currentTrack || m_currentTrack->uri().isEmpty())
        return;

    DidlItem item;
    item.id           = m_currentTrack->id();
    item.parentId     = m_currentTrack->parentId();
    item.didlId       = item.id;
    item.didlParentId = item.parentId;
    item.title        = m_currentTrack->title();
    item.artist       = m_currentTrack->artist();
    item.album        = m_currentTrack->album();
    item.upnpClass    = m_currentTrack->upnpClass();
    item.res          = m_currentTrack->uri();
    item.protocolInfo = m_currentTrack->protocolInfo();
    item.desc         = m_currentTrack->desc();
    item.albumArtUri  = m_currentTrack->imageUrl();

    m_control.addToSonosFavourites(this, item, [this](bool ok, const QString &objectId) {
        if (ok)
            emit sonosFavouriteAdded(objectId);
    });
}

void ZonePlayer::removeCurrentTrackFromSonosFavourites(const QString &objectId)
{
    if (objectId.isEmpty())
        return;

    m_control.removeFromSonosFavourites(this, objectId, [](bool) {
    });
}

void ZonePlayer::removeQueueTrack(const QString &objectId)
{
    m_control.removeTrackFromQueue(this, objectId, [this](bool ok) {
        if (ok)
            emit queueChanged();
    });
}

void ZonePlayer::reorderQueueTrack(int fromIndex, int toIndex, int updateId)
{
    m_control.reorderTrackInQueue(this, fromIndex, toIndex, updateId, [this](bool ok) {
        // The QML queue editor moves rows locally while dragging for
        // immediate feedback. Refetch after the SOAP command completes so
        // the model reconciles with the real Sonos queue on success or
        // rolls back cleanly if the reorder failed.
        Q_UNUSED(ok)
        emit queueChanged();
    });
}

void ZonePlayer::playQueueTrack(int trackNumber)
{
    playQueueTrackInternal(trackNumber);
}

void ZonePlayer::playQueueItem(int trackNumber, const QVariantMap &item)
{
    playQueueTrackInternal(trackNumber, item);
}

void ZonePlayer::playQueueTrackInternal(int trackNumber, const QVariantMap &selectedItem)
{
    // Unconditionally pointed at the queue rather than first checking
    // whether it's already there (roomtunes-bb10's skipto_track() checks
    // first, purely to skip a redundant round trip) -- simpler, and
    // re-selecting the same source a zone is already on doesn't restart it.
    setAVTransportUri(queueUri(), QString(), [this, trackNumber, selectedItem](bool ok) {
        if (!ok)
            return;

        m_control.seek(this, QStringLiteral("TRACK_NR"), QString::number(trackNumber), [this, selectedItem](bool ok) {
            if (!ok)
                return;

            if (!selectedItem.isEmpty())
                emit playbackItemSelected(selectedItem);

            play();
        });
    });
}

void ZonePlayer::setVolume(int level)
{
    m_control.setVolume(this, level, [this, level](bool ok) {
        if (ok && (level != m_volume || !m_volumeKnown))
        {
            m_volume      = level;
            m_volumeKnown = true;
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
    if (m_muted != muted || !m_muteKnown)
    {
        m_muted     = muted;
        m_muteKnown = true;
        emit mutedChanged();
    }

    m_control.setMuted(this, muted, {});
}

void ZonePlayer::browse(const QString                                                      &objectId,
                        std::function<void(bool, const QString &, const QList<DidlItem> &)> callback, int startingIndex,
                        int requestedCount, const QString &browseFlag)
{
    m_control.browse(this, objectId, std::move(callback), startingIndex, requestedCount, browseFlag);
}

void ZonePlayer::browseQueue(std::function<void(bool, const QString &, const QList<DidlItem> &, int)> callback,
                             int startingIndex, int requestedCount)
{
    m_control.browseDetailed(
        this, QStringLiteral("Q:0"),
        [callback](bool ok, const SonosZoneControl::BrowseResult &result) {
            if (callback)
                callback(ok, result.errorMessage, result.items, result.updateIdKnown ? result.updateId : 0);
        },
        startingIndex, requestedCount, QStringLiteral("BrowseDirectChildren"));
}

void ZonePlayer::refreshVolume()
{
    m_control.getVolume(this, [this](bool ok, int level) {
        if (!ok)
            return;
        if (level != m_volume || !m_volumeKnown)
        {
            m_volume      = level;
            m_volumeKnown = true;
            emit volumeChanged();
        }
        QLOG() << m_roomName << "GetVolume OK:" << level;
    });
}

void ZonePlayer::refreshMute()
{
    m_control.getMute(this, [this](bool ok, bool muted) {
        if (!ok)
            return;
        if (muted != m_muted || !m_muteKnown)
        {
            m_muted     = muted;
            m_muteKnown = true;
            emit mutedChanged();
        }
        QLOG() << m_roomName << "GetMute OK:" << muted;
    });
}

void ZonePlayer::handleRenderingControlEvent(const QByteArray &body)
{
    const QString lastChange = genaProperty(body, QStringLiteral("LastChange"));
    if (lastChange.isEmpty())
        return;

    const XmlDoc doc = XmlDoc::parse(lastChange.toUtf8());

    for (const XmlNode &volume : doc.all("//Volume"))
    {
        if (volume.attr("channel") == QStringLiteral("Master"))
        {
            bool      ok    = false;
            const int level = volume.attr("val").toInt(&ok);

            if (ok && (level != m_volume || !m_volumeKnown))
            {
                m_volume      = level;
                m_volumeKnown = true;
                emit volumeChanged();
            }
        }
    }

    for (const XmlNode &muteNode : doc.all("//Mute"))
    {
        if (muteNode.attr("channel") == QStringLiteral("Master"))
        {
            const bool muted = muteNode.attr("val") == QStringLiteral("1");
            if (muted != m_muted || !m_muteKnown)
            {
                m_muted     = muted;
                m_muteKnown = true;
                emit mutedChanged();
            }
        }
    }
}

void ZonePlayer::handleAVTransportEvent(const QByteArray &body)
{
    const AvTransportTrackSnapshot snapshot = avTransportTrackSnapshot(body);

    if (snapshot.transportState == QStringLiteral("PLAYING"))               setPlayState(PlayState::Playing);
    else if (snapshot.transportState == QStringLiteral("PAUSED_PLAYBACK"))  setPlayState(PlayState::Paused);
    else if (snapshot.transportState == QStringLiteral("TRANSITIONING"))    setPlayState(PlayState::Transitioning);
    else if (!snapshot.transportState.isEmpty())                            setPlayState(PlayState::Stopped);

    setPlayMode(snapshot.playMode);

    if (!snapshot.crossfadeMode.isEmpty())
        setCrossfadeState(snapshot.crossfadeMode == QStringLiteral("1"));

    if (isTvStreamUri(snapshot.uri))
    {
        const QString audioInfo = tvAudioInfoFromMetadata(snapshot.metadata, snapshot.uri);

        if (!audioInfo.isEmpty() && m_tvAudioInfo != audioInfo)
            m_tvAudioInfo = audioInfo;
    }
    else if (!snapshot.uri.isEmpty() && !isTvStreamUri(snapshot.uri))
    {
        m_tvAudioInfo.clear();
    }

    refreshPositionInfo();
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
    m_control.getTransportInfo(this, [this](bool ok, const QString &state) {
        if (!ok)
            return;

        if (state == QStringLiteral("PLAYING"))              setPlayState(PlayState::Playing);
        else if (state == QStringLiteral("PAUSED_PLAYBACK")) setPlayState(PlayState::Paused);
        else if (state == QStringLiteral("TRANSITIONING"))   setPlayState(PlayState::Transitioning);
        else                                                 setPlayState(PlayState::Stopped);
    });

    m_control.getTransportSettings(this, [this](bool ok, const QString &playMode) {
        if (!ok)
            return;

        setPlayMode(playMode);
    });

    m_control.getCrossfadeMode(this, [this](bool ok, bool enabled) {
        if (!ok)
            return;

        setCrossfadeState(enabled);
    });

    refreshPositionInfo();
}

void ZonePlayer::refreshPositionInfo()
{
    m_control.getPositionInfo(this, [this](bool ok, const SonosZoneControl::PositionInfo &info) {
        if (!ok)
            return;

        if (info.trackNumberKnown)
            m_currentTrackNumber = info.trackNumber;

        QList<DidlItem> items = Didl::parseItems(info.trackMetaData.toUtf8());
        DidlItem        didl  = items.isEmpty() ? DidlItem{} : items.first();
        if (didl.res.isEmpty())
            didl.res = info.trackUri;

        // Local-library album art comes back as a path relative to the
        // zone itself (e.g. "/getaa?..."); streaming-service art is
        // already an absolute URL. Resolve the former against this zone's
        // own address so QML's Image can just load it directly.
        if (!didl.albumArtUri.isEmpty() && didl.albumArtUri.startsWith(QLatin1Char('/')))
            didl.albumArtUri = baseUrl().chopped(1) + didl.albumArtUri;

        const QString sourceUri = info.trackUri.isEmpty() ? didl.res : info.trackUri;
        QString       sourceTitle;
        QString       sourceArtist;
        QString       sourceImageUrl;

        if (isTvStreamUri(sourceUri))
        {
            // BB10 detected Playbar/Beam TV input from AVTransportURI and
            // displayed a synthetic "TV" track with the bundled TV icon. The
            // Qt 6 app polls TrackURI here, which carries the same Sonos URI.
            sourceTitle    = tr("TV");
            sourceArtist   = m_tvAudioInfo.isEmpty() ? tvInputNameFromUri(sourceUri) : m_tvAudioInfo;
            sourceImageUrl = QStringLiteral("qrc:/qt/qml/RoomTunes/resources/icons/tv.svg");
        }
        else if (isLineInStreamUri(sourceUri))
        {
            sourceTitle    = didl.title.isEmpty() ? tr("Line-In") : didl.title;
            sourceImageUrl = QStringLiteral("qrc:/qt/qml/RoomTunes/resources/icons/line_in.svg");
        }

        if (!sourceTitle.isEmpty())
        {
            if (!m_currentTrack || m_currentTrack->id() != sourceUri || m_currentTrack->uri() != sourceUri ||
                m_currentTrack->title() != sourceTitle || m_currentTrack->artist() != sourceArtist ||
                m_currentTrack->imageUrl() != sourceImageUrl)
            {
                setCurrentTrack(new MediaItem(sourceUri, QString(), sourceTitle, sourceArtist, QString(), QString(),
                                              sourceUri, QStringLiteral("object.item.audioItem"), sourceImageUrl, false,
                                              this));
            }
            setPosition(parseUpnpTime(info.relTime), parseUpnpTime(info.trackDuration));
            return;
        }

        // GetPositionInfo is polled every second while playing (see
        // NowPlayingPanel.qml) purely to track playback position --
        // rebuilding/reassigning currentTrack (and re-fetching its accent
        // color over the network) on every one of those ticks would be
        // wasteful and pointless when it's still the same track.
        if (!m_currentTrack || m_currentTrack->id() != didl.id || m_currentTrack->uri() != didl.res)
            setCurrentTrack(MediaItem::fromDidl(didl, this));

        setPosition(parseUpnpTime(info.relTime), parseUpnpTime(info.trackDuration));
    });
}

void ZonePlayer::advancePositionTick()
{
    if (m_playState != PlayState::Playing || m_durationSeconds <= 0)
        return;

    setPosition(std::min(m_positionSeconds + 1, m_durationSeconds), m_durationSeconds);
}

void ZonePlayer::setPosition(int positionSeconds, int durationSeconds)
{
    if (m_positionSeconds != positionSeconds || m_durationSeconds != durationSeconds)
    {
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
    m_control.seek(this, QStringLiteral("REL_TIME"), formatUpnpTime(targetSeconds), [this, targetSeconds](bool ok) {
        if (!ok)
            return;

        // Optimistic -- reflects the seek immediately rather than waiting
        // for the next once-a-second GetPositionInfo poll to catch up.
        setPosition(targetSeconds, m_durationSeconds);
    });
}

} // namespace RoomTunes
