#include "Didl.h"

#include <QRegularExpression>
#include <QUrl>
#include <QXmlStreamWriter>

namespace RoomTunes
{

namespace
{
constexpr char kDidlNsDc[]      = "http://purl.org/dc/elements/1.1/";
constexpr char kDidlNsUpnp[]    = "urn:schemas-upnp-org:metadata-1-0/upnp/";
constexpr char kDidlNsR[]       = "urn:schemas-rinconnetworks-com:metadata-1-0/";
constexpr char kDidlNsDefault[] = "urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/";

QString decodeFavoriteMetadataId(QString id)
{
    id = QUrl::fromPercentEncoding(id.toUtf8());

    // BB10 stripped the Sonos enqueue flags prefix from favourite resMD ids
    // before rebuilding DIDL for AddURIToQueue. Example:
    // "1006286cspotify%3Aplaylist%3A..." -> "spotify:playlist:...".
    static const QRegularExpression flagsPrefix(QStringLiteral("^[0-9A-Fa-f]{8,9}"));
    return id.remove(flagsPrefix);
}

void applyResourceMetadata(DidlItem *item, const QString &resourceMetadata)
{
    if (!item || resourceMetadata.isEmpty())
        return;

    const QList<DidlItem> nestedItems = Didl::parseItems(resourceMetadata.toUtf8());
    if (nestedItems.isEmpty())
        return;

    const DidlItem &nested = nestedItems.first();
    item->didlId           = decodeFavoriteMetadataId(nested.id);
    item->didlParentId     = QUrl::fromPercentEncoding(nested.parentId.toUtf8());
    item->desc             = nested.desc;

    // BB10 used this same desc value to discover the owning SMAPI service
    // for a favourite. That matters for Spotify playlist favourites: their
    // children are browsed with getMetadata() on the Spotify service, not
    // with ContentDirectory::Browse on the Sonos Library.
    static const QRegularExpression serviceIdPattern(QStringLiteral("^SA_RINCON([0-9]+)_"));
    const QRegularExpressionMatch   match = serviceIdPattern.match(item->desc);
    if (match.hasMatch())
        item->serviceId = match.captured(1).toInt();

    if (!nested.upnpClass.isEmpty())
    {
        item->upnpClass = nested.upnpClass;
        item->container = nested.upnpClass.contains(QStringLiteral(".container"));
    }
}
} // namespace

QByteArray Didl::buildItem(const QString &itemId, const QString &parentId, const QString &title,
                           const QString &upnpClass, const QString &desc, const QString &res,
                           const QString &albumArtUri, const QString &protocolInfo)
{
    QByteArray       out;
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
    if (!albumArtUri.isEmpty())
        xml.writeTextElement(QLatin1String(kDidlNsUpnp), QStringLiteral("albumArtURI"), albumArtUri);
    if (!res.isEmpty())
    {
        xml.writeStartElement(QStringLiteral("res"));
        if (!protocolInfo.isEmpty())
            xml.writeAttribute(QStringLiteral("protocolInfo"), protocolInfo);
        xml.writeCharacters(res);
        xml.writeEndElement(); // res
    }

    xml.writeStartElement(QStringLiteral("desc"));
    xml.writeAttribute(QStringLiteral("id"), QStringLiteral("cdudn"));
    xml.writeAttribute(QStringLiteral("nameSpace"), QLatin1String(kDidlNsR));
    xml.writeCharacters(desc);
    xml.writeEndElement(); // desc

    xml.writeEndElement(); // item
    xml.writeEndElement(); // DIDL-Lite

    return out;
}

QByteArray Didl::buildFavoriteItem(const DidlItem &item)
{
    const QByteArray innerItem = buildItem(
        item.didlId.isEmpty() ? item.id : item.didlId, item.didlParentId.isEmpty() ? item.parentId : item.didlParentId,
        item.title, item.upnpClass, item.desc.isEmpty() ? QStringLiteral("RINCON_AssociatedZPUDN") : item.desc,
        item.res, item.albumArtUri, item.protocolInfo);

    QByteArray       out;
    QXmlStreamWriter xml(&out);
    xml.writeStartElement(QStringLiteral("DIDL-Lite"));
    xml.writeDefaultNamespace(QLatin1String(kDidlNsDefault));
    xml.writeNamespace(QLatin1String(kDidlNsDc), QStringLiteral("dc"));
    xml.writeNamespace(QLatin1String(kDidlNsUpnp), QStringLiteral("upnp"));
    xml.writeNamespace(QLatin1String(kDidlNsR), QStringLiteral("r"));

    xml.writeStartElement(QStringLiteral("item"));
    xml.writeTextElement(QLatin1String(kDidlNsDc), QStringLiteral("title"), item.title);
    xml.writeTextElement(QLatin1String(kDidlNsR), QStringLiteral("type"), QStringLiteral("instantPlay"));
    if (!item.albumArtUri.isEmpty())
        xml.writeTextElement(QLatin1String(kDidlNsUpnp), QStringLiteral("albumArtURI"), item.albumArtUri);
    if (!item.res.isEmpty())
    {
        xml.writeStartElement(QStringLiteral("res"));
        if (!item.protocolInfo.isEmpty())
            xml.writeAttribute(QStringLiteral("protocolInfo"), item.protocolInfo);
        xml.writeCharacters(item.res);
        xml.writeEndElement(); // res
    }
    xml.writeTextElement(QLatin1String(kDidlNsR), QStringLiteral("description"), item.title);
    xml.writeTextElement(QLatin1String(kDidlNsR), QStringLiteral("resMD"), QString::fromUtf8(innerItem));
    xml.writeEndElement(); // item
    xml.writeEndElement(); // DIDL-Lite

    return out;
}

QList<DidlItem> Didl::parseItems(const QByteArray &didlXml)
{
    QList<DidlItem>  items;
    QXmlStreamReader xml(didlXml);

    while (!xml.atEnd())
    {
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
    item.container    = isContainer;
    item.id           = xml.attributes().value(QStringLiteral("id")).toString();
    item.parentId     = xml.attributes().value(QStringLiteral("parentID")).toString();
    item.didlId       = item.id;
    item.didlParentId = item.parentId;
    QString resourceMetadata;

    while (xml.readNextStartElement())
    {
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
        else if (name == QStringLiteral("streamInfo"))
            item.streamInfo = xml.readElementText(QXmlStreamReader::SkipChildElements);
        else if (name == QStringLiteral("originalTrackNumber"))
            item.trackNumber = xml.readElementText(QXmlStreamReader::SkipChildElements);
        else if (name == QStringLiteral("res"))
        {
            item.protocolInfo = xml.attributes().value(QStringLiteral("protocolInfo")).toString();
            item.res          = xml.readElementText(QXmlStreamReader::SkipChildElements);
        }
        else if (name == QStringLiteral("desc"))
            item.desc = xml.readElementText(QXmlStreamReader::SkipChildElements);
        else if (name == QStringLiteral("resMD"))
            resourceMetadata = xml.readElementText(QXmlStreamReader::SkipChildElements);
        else
            xml.skipCurrentElement();
    }

    // Sonos Favourites wrap the real playable item in r:resMD while the
    // outer item's own id/class remain the favourite entry itself. Apply
    // this after the full outer item has been parsed so the inner playable
    // metadata wins regardless of the XML element order Sonos returned.
    applyResourceMetadata(&item, resourceMetadata);

    return item;
}

} // namespace RoomTunes
