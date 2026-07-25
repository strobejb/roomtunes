#pragma once

#include <QList>
#include <QString>

namespace RoomTunes {

struct InstalledService
{
    int serviceId = 0;
    QString username;
    QString password; // meaning depends on the service's own auth policy -- see Smapi.h

    // AccountToken/AccountKey (Token0/Key0), present only once this service
    // has actually been linked via the official Sonos app's DeviceLink/
    // AppLink OAuth flow. Sonos stores these under a *separate* <Service>
    // XML element (UDN "SA_RINCON<serviceId>_X_#Svc<serviceId>-0-Token",
    // sharing the same serviceId prefix but with no Username0/Password0 of
    // its own) -- ThirdPartyMediaServers::parse() merges that element's
    // Token0/Key0 into the same InstalledService as the regular one. See
    // ServiceBrowser::browse(), which reuses this token/key exactly the way
    // roomtunes-bb10's SmapiService::updateToken() did, rather than
    // attempting a fresh OAuth handshake (RoomTunes has no deviceId of its
    // own that Sonos would recognize for that).
    QString token;
    QString key;

    QString nickname; // multi-account label (Sonos 5.2+), e.g. a second Spotify login

    // Resolved separately (see MusicServiceCatalog/ServiceLogoCatalog) --
    // empty until Household cross-references this serviceId against the
    // SMAPI catalog / legacy-service name table and the mslogo.xml icon
    // feed. title always ends up non-empty (falls back to the username or
    // a placeholder); iconUrl may stay empty if no logo was found.
    QString title;
    QString iconUrl;

    // SMAPI endpoint + auth policy ("Anonymous"/"Stateless"/"UserId"/
    // "DeviceLink"/"AppLink") for actually browsing this service -- empty
    // for legacy (pre-SMAPI) services, which can't be browsed this way.
    QString serviceUri;
    QString authPolicy;

    // The id MusicServices:1 GetSessionId (a UPnP call to the Sonos speaker
    // itself, not the SMAPI endpoint) expects -- resolved from the SMAPI
    // catalog alongside title/serviceUri/authPolicy; falls back to serviceId
    // itself for legacy services, where the two ids are the same scheme.
    int smapiId = 0;
};

// Decrypts and parses Sonos' ThirdPartyMediaServersX blob (delivered as a
// GENA property on the same ZoneGroupTopology event channel Household
// already subscribes to) into the music services actually configured/
// logged in on this household -- not the global service catalog (that's
// MusicServices::ListAvailableServices()). Ported from
// ThirdPartyMediaServersX.cpp (decrypt_worker/calc_hhid_digest/
// calc_cipher_key) and ServiceDiscovery.cpp's detectInstalledServices().
class ThirdPartyMediaServers
{
public:
    // householdId is the Sonos household ID (e.g. "Sonos_XXXXXXXX...") used
    // to derive the decryption key; encoded is the raw GENA property value,
    // "2:<base64...>". Returns an empty list if decryption/parsing fails
    // (wrong household id, corrupt data, unexpected format, ...).
    static QList<InstalledService> parse(const QString &householdId, const QString &encoded);
};

}
