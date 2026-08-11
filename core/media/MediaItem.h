#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

#include "../control/Didl.h"

namespace RoomTunes
{

// Replaces SonosTrack: a plain browsable/playable media entry (track,
// album, playlist, ...). No UI-list-model inheritance or image-cache
// coupling -- image loading/caching is a GUI-layer concern. Fields are set
// once at construction and treated as immutable; a track change produces a
// new MediaItem rather than mutating one in place (matching how the
// original SonosTrack was used), except for rating which can be updated
// after the fact.
class MediaItem : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString id CONSTANT READ id)
    Q_PROPERTY(QString parentId CONSTANT READ parentId)
    Q_PROPERTY(QString title CONSTANT READ title)
    Q_PROPERTY(QString artist CONSTANT READ artist)
    Q_PROPERTY(QString album CONSTANT READ album)
    Q_PROPERTY(QString duration CONSTANT READ duration)
    Q_PROPERTY(QString uri CONSTANT READ uri)
    Q_PROPERTY(QString protocolInfo CONSTANT READ protocolInfo)
    Q_PROPERTY(QString upnpClass CONSTANT READ upnpClass)
    Q_PROPERTY(QString desc CONSTANT READ desc)
    Q_PROPERTY(QString imageUrl CONSTANT READ imageUrl)
    Q_PROPERTY(bool container CONSTANT READ isContainer)
    Q_PROPERTY(bool playable CONSTANT READ isPlayable)
    Q_PROPERTY(QString rating READ rating NOTIFY ratingChanged)

  public:
    explicit MediaItem(QObject *parent = nullptr) : QObject(parent)
    {
    }

    MediaItem(const QString &id, const QString &parentId, const QString &title, const QString &artist,
              const QString &album, const QString &duration, const QString &uri, const QString &upnpClass,
              const QString &imageUrl, bool container, QObject *parent = nullptr)
        : MediaItem(id, parentId, title, artist, album, duration, uri, QString(), upnpClass, QString(), imageUrl,
                    container, parent)
    {
    }

    MediaItem(const QString &id, const QString &parentId, const QString &title, const QString &artist,
              const QString &album, const QString &duration, const QString &uri, const QString &protocolInfo,
              const QString &upnpClass, const QString &desc, const QString &imageUrl, bool container,
              QObject *parent = nullptr)
        : QObject(parent), m_id(id), m_parentId(parentId), m_title(title), m_artist(artist), m_album(album),
          m_duration(duration), m_uri(uri), m_protocolInfo(protocolInfo), m_upnpClass(upnpClass), m_desc(desc),
          m_imageUrl(imageUrl), m_container(container)
    {
    }

    static MediaItem *fromDidl(const DidlItem &item, QObject *parent = nullptr)
    {
        return new MediaItem(item.id, item.parentId, item.title, item.artist, item.album, QString(), item.res,
                             item.protocolInfo, item.upnpClass, item.desc, item.albumArtUri, item.container, parent);
    }

    QString id() const
    {
        return m_id;
    }

    QString parentId() const
    {
        return m_parentId;
    }

    QString title() const
    {
        return m_title;
    }

    QString artist() const
    {
        return m_artist;
    }

    QString album() const
    {
        return m_album;
    }

    QString duration() const
    {
        return m_duration;
    }

    QString uri() const
    {
        return m_uri;
    }

    QString protocolInfo() const
    {
        return m_protocolInfo;
    }

    QString upnpClass() const
    {
        return m_upnpClass;
    }

    QString desc() const
    {
        return m_desc;
    }

    QString imageUrl() const
    {
        return m_imageUrl;
    }

    bool isContainer() const
    {
        return m_container;
    }

    bool isPlayable() const
    {
        return !m_container;
    }

    bool isStreamable() const
    {
        return m_upnpClass.endsWith(QStringLiteral(".audioBroadcast"));
    }

    QVariantMap toVariantMap() const
    {
        return {
            {QStringLiteral("id"), m_id},
            {QStringLiteral("parentId"), m_parentId},
            {QStringLiteral("title"), m_title},
            {QStringLiteral("artist"), m_artist},
            {QStringLiteral("album"), m_album},
            {QStringLiteral("imageUrl"), m_imageUrl},
            {QStringLiteral("uri"), m_uri},
            {QStringLiteral("protocolInfo"), m_protocolInfo},
            {QStringLiteral("upnpClass"), m_upnpClass},
            {QStringLiteral("desc"), m_desc},
            {QStringLiteral("container"), m_container},
        };
    }

    QString rating() const
    {
        return m_rating;
    }

    void setRating(const QString &rating)
    {
        if (m_rating != rating)
        {
            m_rating = rating;
            emit ratingChanged();
        }
    }

  signals:
    void ratingChanged();

  private:
    QString m_id;
    QString m_parentId;
    QString m_title;
    QString m_artist;
    QString m_album;
    QString m_duration;
    QString m_uri;
    QString m_protocolInfo;
    QString m_upnpClass;
    QString m_desc;
    QString m_imageUrl;
    bool    m_container = false;
    QString m_rating;
};

} // namespace RoomTunes
