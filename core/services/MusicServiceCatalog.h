#pragma once

#include <QHash>
#include <QString>

namespace RoomTunes
{

// One resolved entry from Sonos' global SMAPI service catalog
// (MusicServices::ListAvailableServices()) -- the ported equivalent of
// bb10's SmapiMetric, trimmed to what's needed to display/browse a service.
struct SmapiCatalogEntry
{
    int     smapiId = 0;
    QString title;
    QString uri;
    QString secureUri;
    QString auth;          // raw Policy/@Auth text: "Anonymous"/"Stateless"/"UserId"/"DeviceLink"
    QString pollInterval;  // raw Policy/@PollInterval, useful for AppLink/DeviceLink diagnostics
    QString containerType; // "MService"/"SoundLab"
    QString capabilities;  // raw Service/@Capabilities bitfield from ListAvailableServices
    QString manifestUri;   // modern AppLink services can advertise app-link metadata here
};

// Resolves a bare serviceId (from ThirdPartyMediaServersX, see
// ThirdPartyMediaServers.h) to a usable title/uri/auth via Sonos' global
// SMAPI catalog. Sonos exposes two ids for the same modern service:
// AvailableServiceDescriptorList/@Id is the compact SMAPI id used by
// getSessionId/URI metadata, while ThirdPartyMediaServersX and favourites
// use the Sonos service type id. The legacy BB10 code noted the stable
// encoding: serviceTypeId = smapiId * 256 + 7. Prefer that direct mapping;
// keep sorted-list pairing only as a compatibility fallback for any service
// whose ids do not follow the normal encoding.
class MusicServiceCatalog
{
  public:
    // descriptorListXml is the <AvailableServiceDescriptorList> SOAP
    // response body (a <Services><Service .../>...</Services> document);
    // availableServiceTypeList is the raw comma-separated
    // <AvailableServiceTypeList> text (includes legacy ids < 16).
    static QHash<int, SmapiCatalogEntry> build(const QByteArray &descriptorListXml,
                                               const QString    &availableServiceTypeList);

    // Human name for a legacy (serviceId < 16) service that never appears
    // in the SMAPI catalog at all. Empty if serviceId isn't a known legacy id.
    static QString legacyServiceName(int serviceId);
};

} // namespace RoomTunes
