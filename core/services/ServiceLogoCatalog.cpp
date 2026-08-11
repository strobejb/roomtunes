#include "ServiceLogoCatalog.h"

#include "../xml/XmlUtils.h"

namespace RoomTunes
{

namespace
{

constexpr int kRhapsodyServiceId = 1;
constexpr int kRhapsodyTrialId   = 2;
constexpr int kNapsterServiceId  = 13;
constexpr int kNapsterTrialId    = 14;

} // namespace

QHash<int, QString> ServiceLogoCatalog::parse(const QByteArray &xml)
{
    QHash<int, QString> icons;
    const XmlDoc        doc = XmlDoc::parse(xml);

    for (const XmlNode &service : doc.all("//service"))
    {
        const int id = service.attrInt("id");
        QString   icon;

        // roomtunes-bb10's parseMSLogo() matched a bare "x-large" placement --
        // the feed has since moved to a "square:*" naming scheme
        // ("square:x-small" through "square:x-large", plus separate
        // "BrandLogo-v2:*" and "Attribution*" variants); "square:x-large"
        // is the modern equivalent of what the original code was after.
        for (const XmlNode &image : service.all(".//image"))
            if (image.attr("placement") == QStringLiteral("square:x-large"))
                icon = image.text().trimmed();

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

} // namespace RoomTunes
