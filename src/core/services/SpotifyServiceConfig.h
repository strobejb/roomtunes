#pragma once

#include <QString>

namespace RoomTunes
{

// Spotify is delivered through Sonos' generic SMAPI protocol (confirmed
// from the original BB10 source: SmapiService + DeviceLink auth, same as
// any other SMAPI partner) -- there is no fixed, publicly-documented SMAPI
// endpoint URL for Spotify that's safe to hardcode here. Sonos assigns/lists
// each partner's real endpoint per-region through a household's own
// available-services catalog (MusicServices::ListAvailableServices(), see
// control/services/MusicServices.h), not a stable public constant -- so rather
// than guessing a URL, the endpoint must be supplied by whoever wires this
// up (e.g. from your own Sonos system's available-services list, or a
// packet capture of the official app). Fetching and parsing that catalog
// automatically is the natural follow-up once this config is threaded
// through, and is intentionally not implemented in this pass.
struct SpotifyServiceConfig
{
    QString serviceUrl; // SMAPI SOAP endpoint -- must be supplied, see note above
    QString title = QStringLiteral("Spotify");
    QString imageSource;
};

} // namespace RoomTunes
