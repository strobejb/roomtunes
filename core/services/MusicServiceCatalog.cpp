#include "MusicServiceCatalog.h"

#include <algorithm>

#include <QMap>
#include <QStringList>
#include <QXmlStreamReader>

namespace RoomTunes {

namespace {

constexpr int kRhapsodyServiceId = 1;
constexpr int kRhapsodyTrialId = 2;
constexpr int kPandoraServiceId = 3;
constexpr int kLastFmServiceId = 11;
constexpr int kNapsterServiceId = 13;
constexpr int kNapsterTrialId = 14;
constexpr int kRadioServiceId = 65031; // TuneIn

struct ParsedEntry
{
    SmapiCatalogEntry entry;
    int strVersion = 0;
    int presVersion = 0;
};

QList<ParsedEntry> parseDescriptors(const QByteArray &xml)
{
    QList<ParsedEntry> result;
    QXmlStreamReader reader(xml);

    while (!reader.atEnd()) {
        if (!reader.readNextStartElement())
            continue;
        if (reader.name() != QLatin1String("Service")) {
            // Don't skip -- this also matches the root <Services> wrapper,
            // which needs descending into, not past.
            continue;
        }

        ParsedEntry parsed;
        const QXmlStreamAttributes attrs = reader.attributes();
        parsed.entry.smapiId = attrs.value(QStringLiteral("Id")).toInt();
        parsed.entry.title = attrs.value(QStringLiteral("Name")).toString();
        parsed.entry.uri = attrs.value(QStringLiteral("Uri")).toString();
        parsed.entry.containerType = attrs.value(QStringLiteral("ContainerType")).toString();

        while (reader.readNextStartElement()) {
            if (reader.name() == QLatin1String("Policy")) {
                parsed.entry.auth = reader.attributes().value(QStringLiteral("Auth")).toString();
                reader.skipCurrentElement();
            } else if (reader.name() == QLatin1String("Presentation")) {
                while (reader.readNextStartElement()) {
                    if (reader.name() == QLatin1String("Strings"))
                        parsed.strVersion = reader.attributes().value(QStringLiteral("Version")).toInt();
                    else if (reader.name() == QLatin1String("PresentationMap"))
                        parsed.presVersion = reader.attributes().value(QStringLiteral("Version")).toInt();
                    reader.skipCurrentElement();
                }
            } else {
                reader.skipCurrentElement();
            }
        }

        result.append(parsed);
    }

    return result;
}

}

QHash<int, SmapiCatalogEntry> MusicServiceCatalog::build(const QByteArray &descriptorListXml,
                                                          const QString &availableServiceTypeList)
{
    QHash<int, SmapiCatalogEntry> result;

    // Sonos apparently sometimes lists the same smapiId twice (a newer and
    // an older revision of the same service's string/presentation maps) --
    // keep whichever has the higher version numbers, matching buildSmapiMap.
    QMap<int, ParsedEntry> bySmapiId;
    for (const ParsedEntry &parsed : parseDescriptors(descriptorListXml)) {
        auto it = bySmapiId.find(parsed.entry.smapiId);
        if (it == bySmapiId.end() || parsed.strVersion > it->strVersion || parsed.presVersion > it->presVersion)
            bySmapiId[parsed.entry.smapiId] = parsed;
    }

    // TuneIn never appears in AvailableServiceTypeList at all -- Sonos
    // expects the client to know about it out of band.
    QStringList available = availableServiceTypeList.split(QLatin1Char(','), Qt::SkipEmptyParts);
    available.append(QString::number(kRadioServiceId));
    std::sort(available.begin(), available.end(),
              [](const QString &a, const QString &b) { return a.toInt() < b.toInt(); });

    // The two lists aren't otherwise correlated by anything but sort order
    // (see class comment): walk both in ascending order, pairing every
    // serviceId >= 16 with the next not-yet-consumed smapiId in turn, and
    // skipping (without consuming a smapiId) every legacy id < 16.
    auto smapiIt = bySmapiId.constBegin();
    for (const QString &idText : std::as_const(available)) {
        if (smapiIt == bySmapiId.constEnd())
            break;

        const int serviceId = idText.toInt();
        if (serviceId < 16)
            continue;

        result.insert(serviceId, smapiIt->entry);
        ++smapiIt;
    }

    return result;
}

QString MusicServiceCatalog::legacyServiceName(int serviceId)
{
    switch (serviceId) {
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

}
