#pragma once

#include <QByteArray>
#include <QVariantMap>

namespace RoomTunes::SonosPlaybackPayload
{

bool       isQueueableItem(const QVariantMap &item);
bool       isStreamItem(const QVariantMap &item);
QByteArray buildItemMetadata(const QVariantMap &item);

} // namespace RoomTunes::SonosPlaybackPayload
