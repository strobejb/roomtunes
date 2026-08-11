#include "MusicServiceCatalog.h"

#include <algorithm>

#include <QMap>
#include <QSet>
#include <QStringList>
#include <QXmlStreamReader>

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
    QXmlStreamReader   reader(xml);

    while (!reader.atEnd())
    {
        if (!reader.readNextStartElement())
            continue;
        if (reader.name() != QLatin1String("Service"))
        {
            // Don't skip -- this also matches the root <Services> wrapper,
            // which needs descending into, not past.
            continue;
        }

        ParsedEntry                parsed;
        const QXmlStreamAttributes attrs = reader.attributes();
        parsed.entry.smapiId             = attrs.value(QStringLiteral("Id")).toInt();
        parsed.entry.title               = attrs.value(QStringLiteral("Name")).toString();
        parsed.entry.uri                 = attrs.value(QStringLiteral("Uri")).toString();
        parsed.entry.secureUri           = attrs.value(QStringLiteral("SecureUri")).toString();
        parsed.entry.containerType       = attrs.value(QStringLiteral("ContainerType")).toString();
        parsed.entry.capabilities        = attrs.value(QStringLiteral("Capabilities")).toString();

        while (reader.readNextStartElement())
        {
            if (reader.name() == QLatin1String("Policy"))
            {
                parsed.entry.auth         = reader.attributes().value(QStringLiteral("Auth")).toString();
                parsed.entry.pollInterval = reader.attributes().value(QStringLiteral("PollInterval")).toString();
                reader.skipCurrentElement();
            }
            else if (reader.name() == QLatin1String("Manifest"))
            {
                parsed.entry.manifestUri = reader.attributes().value(QStringLiteral("Uri")).toString();
                reader.skipCurrentElement();
            }
            else if (reader.name() == QLatin1String("Presentation"))
            {
                while (reader.readNextStartElement())
                {
                    if (reader.name() == QLatin1String("Strings"))
                        parsed.strVersion = reader.attributes().value(QStringLiteral("Version")).toInt();
                    else if (reader.name() == QLatin1String("PresentationMap"))
                        parsed.presVersion = reader.attributes().value(QStringLiteral("Version")).toInt();
                    reader.skipCurrentElement();
                }
            }
            else
            {
                reader.skipCurrentElement();
            }
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
    case kRhapsodyTrialId:
        return QStringLiteral("Rhapsody Trial");
    case kRhapsodyServiceId:
        return QStringLiteral("Rhapsody");
    case kNapsterTrialId:
        return QStringLiteral("Napster Trial");
    case kNapsterServiceId:
        return QStringLiteral("Napster");
    case kPandoraServiceId:
        return QStringLiteral("Pandora");
    case kLastFmServiceId:
        return QStringLiteral("Last.fm");
    default:
        return {};
    }
}

} // namespace RoomTunes
