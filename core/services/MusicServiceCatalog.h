#pragma once

#include <QHash>
#include <QString>

namespace RoomTunes {

// One resolved entry from Sonos' global SMAPI service catalog
// (MusicServices::ListAvailableServices()) -- the ported equivalent of
// bb10's SmapiMetric, trimmed to what's needed to display/browse a service.
struct SmapiCatalogEntry
{
    int smapiId = 0;
    QString title;
    QString uri;
    QString auth;          // raw Policy/@Auth text: "Anonymous"/"Stateless"/"UserId"/"DeviceLink"
    QString containerType; // "MService"/"SoundLab"
};

// Resolves a bare serviceId (from ThirdPartyMediaServersX, see
// ThirdPartyMediaServers.h) to a usable title/uri/auth via Sonos' global
// SMAPI catalog. Ported from ServiceDiscovery.cpp's buildSmapiMap()/
// availableServiceName() -- the serviceId->smapiId pairing isn't given
// directly by the SOAP response; it has to be reconstructed by sorting
// both lists and pairing them in ascending order (undocumented but
// load-bearing quirk of the original protocol reverse-engineering).
class MusicServiceCatalog
{
public:
    // descriptorListXml is the <AvailableServiceDescriptorList> SOAP
    // response body (a <Services><Service .../>...</Services> document);
    // availableServiceTypeList is the raw comma-separated
    // <AvailableServiceTypeList> text (includes legacy ids < 16).
    static QHash<int, SmapiCatalogEntry> build(const QByteArray &descriptorListXml,
                                                const QString &availableServiceTypeList);

    // Human name for a legacy (serviceId < 16) service that never appears
    // in the SMAPI catalog at all. Empty if serviceId isn't a known legacy id.
    static QString legacyServiceName(int serviceId);
};

}
