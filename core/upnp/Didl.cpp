#include "Didl.h"

#include <QXmlStreamWriter>

namespace RoomTunes {

namespace {
constexpr char kDidlNsDc[] = "http://purl.org/dc/elements/1.1/";
constexpr char kDidlNsUpnp[] = "urn:schemas-upnp-org:metadata-1-0/upnp/";
constexpr char kDidlNsR[] = "urn:schemas-rinconnetworks-com:metadata-1-0/";
constexpr char kDidlNsDefault[] = "urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/";
}

QByteArray Didl::buildItem(const QString &itemId, const QString &parentId, const QString &title,
                            const QString &upnpClass, const QString &desc)
{
    QByteArray out;
    QXmlStreamWriter xml(&out);

    xml.writeStartElement(QStringLiteral("DIDL-Lite"));
    xml.writeDefaultNamespace(QLatin1String(kDidlNsDefault));
    xml.writeNamespace(QLatin1String(kDidlNsDc), QStringLiteral("dc"));
    xml.writeNamespace(QLatin1String(kDidlNsUpnp), QStringLiteral("upnp"));
    xml.writeNamespace(QLatin1String(kDidlNsR), QStringLiteral("r"));

    xml.writeStartElement(QStringLiteral("item"));
    xml.writeAttribute(QStringLiteral("id"), itemId);
    xml.writeAttribute(QStringLiteral("parentID"), parentId);
    xml.writeAttribute(QStringLiteral("restricted"), QStringLiteral("true"));

    xml.writeTextElement(QLatin1String(kDidlNsDc), QStringLiteral("title"), title);
    xml.writeTextElement(QLatin1String(kDidlNsUpnp), QStringLiteral("class"), upnpClass);

    xml.writeStartElement(QStringLiteral("desc"));
    xml.writeAttribute(QStringLiteral("id"), QStringLiteral("cdudn"));
    xml.writeAttribute(QStringLiteral("nameSpace"), QLatin1String(kDidlNsR));
    xml.writeCharacters(desc);
    xml.writeEndElement(); // desc

    xml.writeEndElement(); // item
    xml.writeEndElement(); // DIDL-Lite

    return out;
}

QList<DidlItem> Didl::parseItems(const QByteArray &didlXml)
{
    QList<DidlItem> items;
    QXmlStreamReader xml(didlXml);

    while (!xml.atEnd()) {
        if (!xml.readNextStartElement())
            continue;

        if (xml.name() == QLatin1String("item"))
            items.append(parseOneItem(xml, false));
        else if (xml.name() == QLatin1String("container"))
            items.append(parseOneItem(xml, true));
        else if (xml.name() != QLatin1String("DIDL-Lite"))
            xml.skipCurrentElement();
    }

    return items;
}

DidlItem Didl::parseOneItem(QXmlStreamReader &xml, bool isContainer)
{
    DidlItem item;
    item.container = isContainer;
    item.id = xml.attributes().value(QStringLiteral("id")).toString();
    item.parentId = xml.attributes().value(QStringLiteral("parentID")).toString();

    while (xml.readNextStartElement()) {
        const QString name = xml.name().toString();

        if (name == QStringLiteral("title"))
            item.title = xml.readElementText(QXmlStreamReader::SkipChildElements);
        else if (name == QStringLiteral("class"))
            item.upnpClass = xml.readElementText(QXmlStreamReader::SkipChildElements);
        else if (name == QStringLiteral("creator"))
            item.artist = xml.readElementText(QXmlStreamReader::SkipChildElements);
        else if (name == QStringLiteral("album"))
            item.album = xml.readElementText(QXmlStreamReader::SkipChildElements);
        else if (name == QStringLiteral("albumArtURI"))
            item.albumArtUri = xml.readElementText(QXmlStreamReader::SkipChildElements);
        else if (name == QStringLiteral("originalTrackNumber"))
            item.trackNumber = xml.readElementText(QXmlStreamReader::SkipChildElements);
        else if (name == QStringLiteral("res"))
            item.res = xml.readElementText(QXmlStreamReader::SkipChildElements);
        else
            xml.skipCurrentElement();
    }

    return item;
}

}
