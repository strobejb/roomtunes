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
    QString protocolInfo; // <res protocolInfo="...">, needed when saving favourites
    // Usually these match id/parentId/desc. Sonos Favourites are the
    // important exception: the outer favourite item is only a saved
    // wrapper (id like "FV:2/233", class "object.item.sonos-favorite"),
    // while r:resMD contains the real service/library item metadata Sonos
    // expects back in AddURIToQueue/SetAVTransportURI. Keep both: id stays
    // stable for browsing/navigation, didl* is used for playback metadata.
    QString didlId;       // metadata <item id> to use when replaying/enqueueing
    QString didlParentId; // metadata <item parentID> to use when replaying/enqueueing
    QString desc;         // Sonos desc/cdudn metadata to use when replaying/enqueueing
    int serviceId = -1;   // SA_RINCON service id parsed from desc, if present
    QString albumArtUri;
    QString streamInfo;   // Sonos r:streamInfo, used by TV/line-in sources
    QString trackNumber;
    bool container = false;
};

// DIDL-Lite building (QXmlStreamWriter) and parsing (QXmlStreamReader).
// Replaces control/didl.hpp's manual byte-buffer concatenation and Qt::escape().
class Didl
{
public:
    // Builds a minimal single-<item> DIDL-Lite document, e.g. for
    // SetAVTransportURI's CurrentURIMetaData parameter.
    static QByteArray buildItem(const QString &itemId, const QString &parentId, const QString &title,
                                 const QString &upnpClass,
                                 const QString &desc = QStringLiteral("RINCON_AssociatedZPUDN"),
                                 const QString &res = QString(),
                                 const QString &albumArtUri = QString(),
                                 const QString &protocolInfo = QString());
    static QByteArray buildFavoriteItem(const DidlItem &item);

    // Parses a <DIDL-Lite> document (e.g. a ContentDirectory::Browse Result)
    // into its top-level <item>/<container> entries.
    static QList<DidlItem> parseItems(const QByteArray &didlXml);

private:
    static DidlItem parseOneItem(QXmlStreamReader &xml, bool isContainer);
};

}
