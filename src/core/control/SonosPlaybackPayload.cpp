#include "SonosPlaybackPayload.h"

#include "Didl.h"

#include <QString>

namespace RoomTunes::SonosPlaybackPayload
{

namespace
{

QString protocolInfoForUri(const QString &uri)
{
    if (uri.startsWith(QStringLiteral("x-sonosapi-stream:")) ||
        uri.startsWith(QStringLiteral("x-rincon-mp3radio:")))
        return QStringLiteral("x-rincon-mp3radio:*:*:*");
    if (uri.startsWith(QStringLiteral("x-sonosapi-radio:")))
        return QStringLiteral("x-sonosapi-radio:*:audio/x-sonosapi-radio:*");
    if (uri.startsWith(QStringLiteral("x-sonosapi-hls:")))
        return QStringLiteral("x-sonosapi-hls:*:*:*");
    if (uri.startsWith(QStringLiteral("x-rincon-stream:")))
        return QStringLiteral("x-rincon-stream:*:*:*");
    if (uri.startsWith(QStringLiteral("x-sonos-htastream:")))
        return QStringLiteral("x-sonos-htastream:*:*:*");
    if (uri.startsWith(QStringLiteral("x-sonos-dock:")))
        return QStringLiteral("x-sonos-dock:*:*:*");
    return {};
}

} // namespace

bool isStreamItem(const QVariantMap &item)
{
    const QString uri       = item.value(QStringLiteral("uri")).toString();
    const QString upnpClass = item.value(QStringLiteral("upnpClass")).toString();

    return upnpClass == QStringLiteral("object.item.audioItem.audioBroadcast") ||
           upnpClass.endsWith(QStringLiteral(".audioBroadcast")) ||
           uri.startsWith(QStringLiteral("x-sonosapi-stream:")) ||
           uri.startsWith(QStringLiteral("x-sonosapi-radio:")) ||
           uri.startsWith(QStringLiteral("x-sonosapi-hls:")) ||
           uri.startsWith(QStringLiteral("x-rincon-mp3radio:")) ||
           uri.startsWith(QStringLiteral("x-rincon-stream:")) ||
           uri.startsWith(QStringLiteral("x-sonos-htastream:")) ||
           uri.startsWith(QStringLiteral("x-sonos-dock:")) ||
           uri.startsWith(QStringLiteral("pndrradio:")) ||
           uri.startsWith(QStringLiteral("rdradio:"));
}

bool isQueueableItem(const QVariantMap &item)
{
    return !item.value(QStringLiteral("uri")).toString().isEmpty() && !isStreamItem(item);
}

QByteArray buildItemMetadata(const QVariantMap &item)
{
    const QString upnpClass = item.value(QStringLiteral("upnpClass")).toString();
    const QString title     = item.value(QStringLiteral("title")).toString();
    QString       didlId    = item.value(QStringLiteral("didlId")).toString();
    if (didlId.isEmpty())
        didlId = item.value(QStringLiteral("id")).toString();
    const QString parentId     = item.value(QStringLiteral("parentId")).toString();
    const QString desc         = item.value(QStringLiteral("desc")).toString();
    const QString res          = item.value(QStringLiteral("uri")).toString();
    const QString albumArtUri  = item.value(QStringLiteral("imageUrl")).toString();
    QString       protocolInfo = item.value(QStringLiteral("protocolInfo")).toString();
    if (protocolInfo.isEmpty())
        protocolInfo = protocolInfoForUri(res);
    return Didl::buildItem(didlId, parentId, title, upnpClass,
                           desc.isEmpty() ? QStringLiteral("RINCON_AssociatedZPUDN") : desc, res, albumArtUri,
                           protocolInfo);
}

} // namespace RoomTunes::SonosPlaybackPayload
