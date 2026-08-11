#include "MusicServiceCatalog.h"

#include <algorithm>

#include <QMap>
#include <QSet>
#include <QStringList>

#include "../xml/XmlUtils.h"

namespace RoomTunes
{

namespace
{

constexpr int kRhapsodyServiceId       = 1;
constexpr int kRhapsodyTrialId         = 2;
constexpr int kPandoraServiceId        = 3;
constexpr int kLastFmServiceId         = 11;
constexpr int kNapsterServiceId        = 13;
constexpr int kNapsterTrialId          = 14;
constexpr int kRadioServiceId          = 65031; // TuneIn
constexpr int kServiceTypeIdMultiplier = 256;
constexpr int kServiceTypeIdOffset     = 7;

struct ParsedEntry
{
    SmapiCatalogEntry entry;
    int               strVersion  = 0;
    int               presVersion = 0;
};

QList<ParsedEntry> parseDescriptors(const QByteArray &xml)
{
    QList<ParsedEntry> result;
    const XmlDoc       doc = XmlDoc::parse(xml);

    for (const XmlNode &service : doc.all("//Service"))
    {
        ParsedEntry parsed;
        parsed.entry.smapiId       = service.attrInt("Id");
        parsed.entry.title         = service.attr("Name");
        parsed.entry.uri           = service.attr("Uri");
        parsed.entry.secureUri     = service.attr("SecureUri");
        parsed.entry.containerType = service.attr("ContainerType");
        parsed.entry.capabilities  = service.attr("Capabilities");

        const XmlNode policy = service.child("Policy");
        if (policy)
        {
            parsed.entry.auth         = policy.attr("Auth");
            parsed.entry.pollInterval = policy.attr("PollInterval");
        }

        const XmlNode manifest = service.child("Manifest");
        if (manifest)
            parsed.entry.manifestUri = manifest.attr("Uri");

        const XmlNode presentation = service.child("Presentation");
        if (presentation)
        {
            parsed.strVersion  = presentation.child("Strings").attrInt("Version");
            parsed.presVersion = presentation.child("PresentationMap").attrInt("Version");
        }

        result.append(parsed);
    }

    return result;
}

} // namespace

QHash<int, SmapiCatalogEntry> MusicServiceCatalog::build(const QByteArray &descriptorListXml,
                                                         const QString    &availableServiceTypeList)
{
    QHash<int, SmapiCatalogEntry> result;

    // Sonos apparently sometimes lists the same smapiId twice (a newer and
    // an older revision of the same service's string/presentation maps) --
    // keep whichever has the higher version numbers, matching buildSmapiMap.
    QMap<int, ParsedEntry> bySmapiId;
    for (const ParsedEntry &parsed : parseDescriptors(descriptorListXml))
    {
        auto it = bySmapiId.find(parsed.entry.smapiId);
        if (it == bySmapiId.end() || parsed.strVersion > it->strVersion || parsed.presVersion > it->presVersion)
            bySmapiId[parsed.entry.smapiId] = parsed;
    }

    QSet<int> availableIds;
    for (const QString &idText : availableServiceTypeList.split(QLatin1Char(','), Qt::SkipEmptyParts))
        availableIds.insert(idText.toInt());
    // TuneIn never appears in AvailableServiceTypeList at all -- Sonos
    // expects the client to know about it out of band.
    availableIds.insert(kRadioServiceId);

    QSet<int> mappedSmapiIds;
    for (auto it = bySmapiId.constBegin(); it != bySmapiId.constEnd(); ++it)
    {
        // BB10's reverse-engineering notes call out the real relation here:
        // serviceTypeId = smapiId * 256 + 7. Use it as the primary key so a
        // modern catalog entry can't be paired with the wrong household
        // service just because ListAvailableServices ordering changed.
        const int serviceId = it.key() * kServiceTypeIdMultiplier + kServiceTypeIdOffset;
        if (!availableIds.contains(serviceId))
            continue;

        result.insert(serviceId, it->entry);
        mappedSmapiIds.insert(it.key());
    }

    // Compatibility fallback for any currently-available service that did
    // not match the normal encoding. This preserves the old behavior for
    // unexpected/legacy catalog rows without letting it override the direct
    // id mapping above.
    QStringList available;
    for (int serviceId : std::as_const(availableIds))
    {
        if (serviceId >= 16 && !result.contains(serviceId))
            available.append(QString::number(serviceId));
    }
    std::sort(available.begin(), available.end(),
              [](const QString &a, const QString &b)
              {
                  return a.toInt() < b.toInt();
              });

    auto smapiIt = bySmapiId.constBegin();
    for (const QString &idText : std::as_const(available))
    {
        while (smapiIt != bySmapiId.constEnd() && mappedSmapiIds.contains(smapiIt.key()))
            ++smapiIt;

        if (smapiIt == bySmapiId.constEnd())
            break;

        result.insert(idText.toInt(), smapiIt->entry);
        mappedSmapiIds.insert(smapiIt.key());
        ++smapiIt;
    }

    return result;
}

QString MusicServiceCatalog::legacyServiceName(int serviceId)
{
    switch (serviceId)
    {
    case kRhapsodyTrialId:      return QStringLiteral("Rhapsody Trial");
    case kRhapsodyServiceId:    return QStringLiteral("Rhapsody");
    case kNapsterTrialId:       return QStringLiteral("Napster Trial");
    case kNapsterServiceId:     return QStringLiteral("Napster");
    case kPandoraServiceId:     return QStringLiteral("Pandora");
    case kLastFmServiceId:      return QStringLiteral("Last.fm");
    default:                    return {};
    }
}

} // namespace RoomTunes
