#pragma once

#include <QMap>
#include <QString>
#include <QXmlStreamReader>

namespace RoomTunes
{

inline QString xmlEscape(const QString &text)
{
    return text.toHtmlEscaped();
}

// Reads the immediate children of the element the reader is currently
// positioned on (a StartElement) into a flat name -> text map. Leaves the
// reader positioned just after the matching EndElement. Nested markup
// inside a child (e.g. escaped DIDL-Lite payloads) is preserved as text
// since it arrives XML-entity-escaped, not as real child elements.
inline QMap<QString, QString> flattenElement(QXmlStreamReader &xml)
{
    QMap<QString, QString> values;

    while (xml.readNextStartElement())
        values.insert(xml.name().toString(), xml.readElementText(QXmlStreamReader::SkipChildElements));

    return values;
}

} // namespace RoomTunes
