#include "ServiceLogoCatalog.h"

#include <QXmlStreamReader>

namespace RoomTunes {

namespace {

constexpr int kRhapsodyServiceId = 1;
constexpr int kRhapsodyTrialId = 2;
constexpr int kNapsterServiceId = 13;
constexpr int kNapsterTrialId = 14;

}

QHash<int, QString> ServiceLogoCatalog::parse(const QByteArray &xml)
{
    QHash<int, QString> icons;
    QXmlStreamReader reader(xml);

    while (!reader.atEnd()) {
        if (!reader.readNextStartElement())
            continue;
        if (reader.name() != QLatin1String("service")) {
            // Don't skip -- descends through the <images>/<sized> wrappers
            // the same way, rather than skipping past them.
            continue;
        }

        const int id = reader.attributes().value(QStringLiteral("id")).toInt();
        QString icon;

        while (reader.readNextStartElement()) {
            // bb10's parseMSLogo() matched a bare "x-large" placement --
            // the feed has since moved to a "square:*" naming scheme
            // ("square:x-small" through "square:x-large", plus separate
            // "BrandLogo-v2:*" and "Attribution*" variants); "square:x-large"
            // is the modern equivalent of what the original was after.
            if (reader.name() == QLatin1String("image")
                && reader.attributes().value(QStringLiteral("placement")) == QLatin1String("square:x-large")) {
                icon = reader.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
            } else {
                reader.skipCurrentElement();
            }
        }

        if (!icon.isEmpty())
            icons.insert(id, icon);
    }

    // Napster/Rhapsody trial tiers share their base service's icon --
    // mslogo.xml only has an entry for the paid tier.
    if (icons.contains(kNapsterServiceId))
        icons[kNapsterTrialId] = icons.value(kNapsterServiceId);
    if (icons.contains(kRhapsodyServiceId))
        icons[kRhapsodyTrialId] = icons.value(kRhapsodyServiceId);

    return icons;
}

}
