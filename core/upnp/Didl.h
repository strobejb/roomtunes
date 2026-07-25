#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QXmlStreamReader>

namespace RoomTunes {

// One parsed <item> or <container> from a DIDL-Lite document.
struct DidlItem
{
    QString id;
    QString parentId;
    QString title;
    QString artist;
    QString album;
    QString upnpClass;
    QString res;          // playable URI (the <res> element text)
    QString albumArtUri;
    QString trackNumber;
    bool container = false;
};

// DIDL-Lite building (QXmlStreamWriter) and parsing (QXmlStreamReader).
// Replaces upnp/didl.hpp's manual byte-buffer concatenation and Qt::escape().
class Didl
{
public:
    // Builds a minimal single-<item> DIDL-Lite document, e.g. for
    // SetAVTransportURI's CurrentURIMetaData parameter.
    static QByteArray buildItem(const QString &itemId, const QString &parentId, const QString &title,
                                 const QString &upnpClass,
                                 const QString &desc = QStringLiteral("RINCON_AssociatedZPUDN"));

    // Parses a <DIDL-Lite> document (e.g. a ContentDirectory::Browse Result)
    // into its top-level <item>/<container> entries.
    static QList<DidlItem> parseItems(const QByteArray &didlXml);

private:
    static DidlItem parseOneItem(QXmlStreamReader &xml, bool isContainer);
};

}
