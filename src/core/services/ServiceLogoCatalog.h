#pragma once

#include <QHash>
#include <QString>

namespace RoomTunes
{

// Parses Sonos' mslogo.xml feed (a static, unauthenticated resource) into a
// smapiId/legacy-id -> icon URL map. Ported from ServiceDiscovery.cpp's
// parseMSLogo(). Keyed by *smapiId* for SMAPI services (the same numbering
// as MusicServiceCatalog's SmapiCatalogEntry::smapiId, not the household's
// own serviceId) but by the raw legacy id directly for the small number of
// pre-SMAPI services (Pandora/Rhapsody/Napster/Last.fm).
class ServiceLogoCatalog
{
  public:
    static constexpr const char *kUrl = "http://update-services.sonos.com/services/mslogo.xml";

    static QHash<int, QString> parse(const QByteArray &xml);
};

} // namespace RoomTunes
