#include "SonosPlaybackPayload.h"

#include "Didl.h"

#include <QString>

namespace RoomTunes::SonosPlaybackPayload {

bool isStreamItem(const QVariantMap &item)
{
    return item.value(QStringLiteral("upnpClass")).toString().endsWith(QStringLiteral(".audioBroadcast"));
}

bool isQueueableItem(const QVariantMap &item)
{
    return !item.value(QStringLiteral("uri")).toString().isEmpty() && !isStreamItem(item);
}

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

}
