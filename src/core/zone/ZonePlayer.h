#pragma once

#include <functional>

#include <QColor>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "../control/Didl.h"
#include "../control/SonosZoneControl.h"
#include "../control/services/AVTransport.h"
#include "../control/services/AudioIn.h"
#include "../control/services/ContentDirectory.h"
#include "../control/services/DeviceProperties.h"
#include "../control/services/MusicServices.h"
#include "../control/services/Queue.h"
#include "../control/services/RenderingControl.h"
#include "../control/services/SystemProperties.h"
#include "../control/services/ZoneGroupTopology.h"
#include "../media/MediaItem.h"

namespace RoomTunes
{

enum class PlayState
{
    Stopped,
    Playing,
    Paused,
    Transitioning
};

// One Sonos zone player. Protocol/state only -- no UI-baked members (icon
// paths etc; that mapping belongs in the GUI layer). Owns its own
// AVTransport/RenderingControl/ContentDirectory/DeviceProperties/
// ZoneGroupTopology instances, matching the original ZonePlayer design.
// Out of scope for this pass: alarms, EQ, TV/HTControl, LED/IR,
// stereo-pair/SUB/surround config.
class ZonePlayer : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString udn CONSTANT READ udn)
    Q_PROPERTY(QString roomName READ roomName NOTIFY roomNameChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(bool coordinator READ isCoordinator NOTIFY coordinatorChanged)
    Q_PROPERTY(bool invisible READ invisible NOTIFY invisibleChanged)
    Q_PROPERTY(bool supportsTvSource READ supportsTvSource NOTIFY supportsTvSourceChanged)
    Q_PROPERTY(bool supportsLineInSource READ supportsLineInSource NOTIFY supportsLineInSourceChanged)
    Q_PROPERTY(QVariantList sourceItems READ sourceItems NOTIFY sourceItemsChanged)
    Q_PROPERTY(int volume READ volume NOTIFY volumeChanged)
    Q_PROPERTY(bool volumeKnown READ volumeKnown NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY mutedChanged)
    Q_PROPERTY(bool muteKnown READ muteKnown NOTIFY mutedChanged)
    Q_PROPERTY(QString playStateText READ playStateText NOTIFY playStateChanged)
    Q_PROPERTY(bool shuffleEnabled READ shuffleEnabled NOTIFY playModeChanged)
    Q_PROPERTY(int repeatMode READ repeatMode NOTIFY playModeChanged)
    Q_PROPERTY(bool crossfadeEnabled READ crossfadeEnabled NOTIFY crossfadeChanged)
    Q_PROPERTY(MediaItem *currentTrack READ currentTrack NOTIFY currentTrackChanged)
    Q_PROPERTY(QColor accentColor READ accentColor NOTIFY accentColorChanged)
    Q_PROPERTY(int positionSeconds READ positionSeconds NOTIFY positionChanged)
    Q_PROPERTY(int durationSeconds READ durationSeconds NOTIFY positionChanged)

  public:
    ZonePlayer(QNetworkAccessManager *netMgr, const QString &deviceIp, const QString &udn, QObject *parent = nullptr);

    const QString &udn() const
    {
        return m_udn;
    }

    const QString &deviceIp() const
    {
        return m_deviceIp;
    }

    QString baseUrl() const
    {
        return QStringLiteral("http://%1:1400/").arg(m_deviceIp);
    }

    // AVTransportURI value that means "this zone's own queue" -- see
    // playItem()/playQueueTrack().
    QString queueUri() const
    {
        return QStringLiteral("x-rincon-queue:%1#0").arg(m_udn);
    }

    const QString &householdId() const
    {
        return m_householdId;
    }

    void setHouseholdId(const QString &id)
    {
        m_householdId = id;
    }

    const QString &roomName() const
    {
        return m_roomName;
    }

    void setRoomName(const QString &name);

    const QString &modelName() const
    {
        return m_modelName;
    }

    void setModelName(const QString &name);
    bool supportsTvSource() const;

    const QString &displayName() const
    {
        return m_displayName;
    }

    const QString &displayVersion() const
    {
        return m_displayVersion;
    }

    const QString &softwareVersion() const
    {
        return m_softwareVersion;
    }

    const QString &zoneType() const
    {
        return m_zoneType;
    }

    const QStringList &features() const
    {
        return m_features;
    }

    void        setDeviceDescriptionDetails(const QString &displayName, const QString &displayVersion,
                                            const QString &softwareVersion, const QString &zoneType,
                                            const QStringList &features);
    void        setDeviceServices(const QSet<QString> &services);
    bool        hasDeviceService(const QString &serviceName) const;
    QStringList deviceServices() const;
    bool        supportsLineInSource() const;
    QVariantList sourceItems() const;

    const QString &serialNumber() const
    {
        return m_serialNumber;
    }

    void setSerialNumber(const QString &serial)
    {
        m_serialNumber = serial;
    }

    const QString &coordinatorUdn() const
    {
        return m_coordinatorUdn;
    }

    void setCoordinatorUdn(const QString &udn);

    bool isCoordinator() const
    {
        return m_coordinatorUdn == m_udn;
    }

    // true for a bonded satellite (SUB, surround L/R, stereo-pair slave)
    // that isn't independently controllable and shouldn't be shown as its
    // own room -- set from the topology's ZoneGroupMember Invisible flag.
    bool invisible() const
    {
        return m_invisible;
    }

    void setInvisible(bool invisible);

    // true once at least one ZoneGroupTopology message has processed this
    // zone's group membership -- a zone isn't 'ready' until this is true
    // too, not just HHID + device_description (see Household's discovery
    // model comment).
    bool hasValidTopology() const
    {
        return m_validZoneTopology;
    }

    void setHasValidTopology(bool valid)
    {
        m_validZoneTopology = valid;
    }

    bool ready() const
    {
        return m_ready;
    }

    void setReady(bool ready);

    PlayState playState() const
    {
        return m_playState;
    }

    QString playStateText() const;
    bool    shuffleEnabled() const;
    int     repeatMode() const;

    bool crossfadeEnabled() const
    {
        return m_crossfadeEnabled;
    }

    int volume() const
    {
        return m_volume;
    }

    bool volumeKnown() const
    {
        return m_volumeKnown;
    }

    bool muted() const
    {
        return m_muted;
    }

    bool muteKnown() const
    {
        return m_muteKnown;
    }

    MediaItem *currentTrack() const
    {
        return m_currentTrack;
    }

    // A prominent color picked from the current track's album art, for the
    // Now Playing panel's "color" render mode (see AlbumColorAnalyzer).
    // Invalid (QColor()) while there's no track or its art hasn't loaded yet.
    QColor accentColor() const
    {
        return m_accentColor;
    }

    // Elapsed/total time of the current track, from AVTransport's
    // GetPositionInfo (RelTime/TrackDuration) -- 0/0 until the first poll
    // completes or when the source doesn't report a duration (e.g. radio).
    int positionSeconds() const
    {
        return m_positionSeconds;
    }

    int durationSeconds() const
    {
        return m_durationSeconds;
    }

    AVTransport &avTransport()
    {
        return m_avTransport;
    }

    RenderingControl &renderingControl()
    {
        return m_renderingControl;
    }

    ContentDirectory &contentDirectory()
    {
        return m_contentDirectory;
    }

    AudioIn &audioIn()
    {
        return m_audioIn;
    }

    Queue &queue()
    {
        return m_queue;
    }

    DeviceProperties &deviceProperties()
    {
        return m_deviceProperties;
    }

    ZoneGroupTopology &zoneGroupTopology()
    {
        return m_zoneGroupTopology;
    }

    MusicServices &musicServices()
    {
        return m_musicServices;
    }

    SystemProperties &systemProperties()
    {
        return m_systemProperties;
    }

    // transport / queue
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void setShuffleEnabled(bool enabled);
    Q_INVOKABLE void cycleRepeatMode();
    Q_INVOKABLE void setCrossfadeEnabled(bool enabled);
    // fraction is 0..1 -- the scrub bar naturally deals in ratios of its
    // own width, so it converts to/from durationSeconds here rather than
    // making QML do that arithmetic.
    Q_INVOKABLE void seek(qreal fraction);
    // Joins this zone into targetCoordinator's group (SetAVTransportURI to
    // "x-rincon:" + its UDN) -- the Zones panel's drag-to-group gesture.
    Q_INVOKABLE void joinGroup(ZonePlayer *targetCoordinator);
    // Leaves whatever group this zone is currently in (including if it's
    // that group's own coordinator), becoming its own standalone
    // single-zone group again -- the Zones panel's per-member unlink
    // action.
    Q_INVOKABLE void leaveGroup();
    void setAVTransportUri(const QString &uri, const QString &metaData, std::function<void(bool)> callback = {});
    void addUriToQueue(const QString &uri, const QString &metaData, int desiredFirstTrackNumberEnqueued,
                       bool enqueueAsNext, std::function<void(bool ok, int firstTrackNumberEnqueued)> callback = {});
    void removeAllTracksFromQueue(std::function<void(bool)> callback = {});
    Q_INVOKABLE void clearQueue();
    Q_INVOKABLE void saveQueueAsSonosPlaylist(const QString &title);
    Q_INVOKABLE void addCurrentTrackToSonosFavourites();
    Q_INVOKABLE void removeCurrentTrackFromSonosFavourites(const QString &objectId);

    // Plays a browse/search result item (a QVariantMap in the shape
    // MusicService subclasses produce -- id/title/uri/upnpClass/didlId/
    // parentId/desc/container, see SonosLibraryService/SmapiService) right
    // now: a stream/audio source replaces the
    // transport URI directly and plays; anything else is enqueued right
    // after whatever's currently playing, then sought to and played --
    // mirrors roomtunes-bb10's SonosApp::play_track()/play_stream().
    Q_INVOKABLE void playItem(const QVariantMap &item);
    // Inserted right after the track that's currently playing, without
    // touching what's playing now -- roomtunes-bb10's play_next(). A no-op
    // for a stream (not queueable).
    Q_INVOKABLE void playItemNext(const QVariantMap &item);
    // Appended to the end of the queue, nothing else -- roomtunes-bb10's
    // plain add_to_queue(). A no-op for a stream (not queueable).
    Q_INVOKABLE void addItemToQueue(const QVariantMap &item);
    // Clears the queue first, then plays the item -- roomtunes-bb10's
    // replace_queue().
    Q_INVOKABLE void replaceQueueWithItem(const QVariantMap &item);
    // Plays an already-queued track by its 1-based position -- the Queue
    // panel's click-to-play (roomtunes-bb10's skipto_track()); no
    // AddURIToQueue involved since the item's already in the queue.
    Q_INVOKABLE void playQueueTrack(int trackNumber);
    Q_INVOKABLE void playQueueItem(int trackNumber, const QVariantMap &item);
    // Removes one already-queued track by its own DIDL object id (e.g.
    // "Q:0/5", as browsed -- see QueueModel's IdRole), matching
    // roomtunes-bb10's remove_track(), which likewise always passes
    // UpdateID 0 (Sonos treats 0 as "don't care" for this action).
    Q_INVOKABLE void removeQueueTrack(const QString &objectId);
    Q_INVOKABLE void reorderQueueTrack(int fromIndex, int toIndex, int updateId);

    // volume / mute
    Q_INVOKABLE void setVolume(int level);
    Q_INVOKABLE void setMuted(bool muted);

    // browse the content directory (a music service or the local queue/library).
    // errorMessage is empty on success; on failure it's the UPnP error
    // code/description if the fault included one (e.g. "701 No such
    // object"), else the raw SOAP fault string -- see the QWARN() this
    // logs internally for the objectId this failure was against too.
    void browse(const QString                                                                          &objectId,
                std::function<void(bool ok, const QString &errorMessage, const QList<DidlItem> &items)> callback,
                int startingIndex = 0, int requestedCount = 100,
                const QString &browseFlag = QStringLiteral("BrowseDirectChildren"));
    void browseQueue(
        std::function<void(bool ok, const QString &errorMessage, const QList<DidlItem> &items, int updateId)> callback,
        int startingIndex = 0, int requestedCount = 100);

    // pull current volume/mute/transport/track state from the zone
    Q_INVOKABLE void refreshVolume();
    Q_INVOKABLE void refreshMute();
    Q_INVOKABLE void refreshTransportState();
    Q_INVOKABLE void advancePositionTick();
    void             handleRenderingControlEvent(const QByteArray &body);
    void             handleAVTransportEvent(const QByteArray &body);
    void             handleContentDirectoryEvent(const QByteArray &body);
    void             handleAudioInEvent(const QByteArray &body);

  signals:
    void roomNameChanged();
    void readyChanged();
    void coordinatorChanged();
    void invisibleChanged();
    void supportsTvSourceChanged();
    void supportsLineInSourceChanged();
    void sourceItemsChanged();
    void playStateChanged();
    void playModeChanged();
    void crossfadeChanged();
    void volumeChanged();
    void mutedChanged();
    void currentTrackChanged();
    void accentColorChanged();
    void positionChanged();
    void playbackItemSelected(const QVariantMap &item);
    // Emitted after a queue-mutating action (add/remove/clear) actually
    // succeeds -- QueueModel listens to keep the "Up Next" list in sync
    // without polling.
    void queueChanged();
    void sonosFavouriteAdded(const QString &objectId);
    void sonosFavouriteStatus(bool isFavourite, const QString &objectId);

  private:
    void setPlayState(PlayState state);
    void setPlayMode(const QString &playMode);
    void setCrossfadeState(bool enabled, bool known = true);
    void setCurrentTrack(MediaItem *track);
    void checkCurrentTrackFavouriteStatus();
    void refreshAccentColor(const QString &imageUrl);
    void refreshPositionInfo();
    void setPosition(int positionSeconds, int durationSeconds);
    void playQueueTrackInternal(int trackNumber, const QVariantMap &selectedItem = {});

  private:
    QNetworkAccessManager *m_netMgr;

    QString       m_deviceIp;
    QString       m_udn;
    QString       m_householdId;
    QString       m_roomName;
    QString       m_modelName;
    QString       m_displayName;
    QString       m_displayVersion;
    QString       m_softwareVersion;
    QString       m_zoneType;
    QStringList   m_features;
    QSet<QString> m_deviceServices;
    QString       m_serialNumber;
    QString       m_coordinatorUdn;
    bool          m_invisible         = false;
    bool          m_validZoneTopology = false;
    bool          m_ready             = false;

    PlayState  m_playState        = PlayState::Stopped;
    QString    m_playMode         = QStringLiteral("NORMAL");
    bool       m_crossfadeEnabled = false;
    bool       m_crossfadeKnown   = false;
    int        m_volume           = 0;
    bool       m_volumeKnown      = false;
    bool       m_muted            = false;
    bool       m_muteKnown        = false;
    MediaItem  *m_currentTrack = nullptr;
    QColor      m_accentColor;
    QString     m_accentColorRequestUrl; // guards against a stale reply landing after currentTrack changed again
    QString     m_tvAudioInfo;
    int        m_positionSeconds    = 0;
    int        m_durationSeconds    = 0;
    int        m_currentTrackNumber = 0;

    AVTransport       m_avTransport;
    RenderingControl  m_renderingControl;
    ContentDirectory  m_contentDirectory;
    AudioIn           m_audioIn;
    Queue             m_queue;
    DeviceProperties  m_deviceProperties;
    ZoneGroupTopology m_zoneGroupTopology;
    MusicServices     m_musicServices;
    SystemProperties  m_systemProperties;
    SonosZoneControl  m_control;
};

} // namespace RoomTunes
