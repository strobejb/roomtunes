#include "SoapResponse.h"

#include "../xml/XmlUtils.h"

namespace RoomTunes {

SoapResponse::SoapResponse(QObject *senderObject)
    : m_reply(qobject_cast<QNetworkReply *>(senderObject))
{
    if (m_reply) {
        m_rawBody = m_reply->readAll();
        parse(m_rawBody);

        // A network-level failure (timeout, connection refused, ...) never
        // has a SOAP Fault body to parse faultString() out of, which left
        // every "<action> failed: " warning in the app printing with no
        // actual error text for exactly this case -- fall back to Qt's own
        // description of what went wrong.
        if (!m_hasFault && m_reply->error() != QNetworkReply::NoError)
            m_faultString = m_reply->errorString();
    }
}

bool SoapResponse::error() const
{
    return !m_reply || m_reply->error() != QNetworkReply::NoError || m_hasFault;
}

int SoapResponse::httpStatusCode() const
{
    return m_reply ? m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() : 0;
}

void SoapResponse::parse(const QByteArray &body)
{
    QXmlStreamReader xml(body);

    while (!xml.atEnd()) {
        if (!xml.readNextStartElement())
            continue;

        if (!xml.name().endsWith(QLatin1String("Body")))
            continue;

        if (xml.readNextStartElement()) {
            if (xml.name().endsWith(QLatin1String("Fault")))
                parseFault(xml);
            else
                m_values = flattenElement(xml);
        }
        break;
    }
}

void SoapResponse::parseFault(QXmlStreamReader &xml)
{
    m_hasFault = true;

    while (xml.readNextStartElement()) {
        const QString name = xml.name().toString();

        if (name == QStringLiteral("faultcode")) {
            m_faultCode = xml.readElementText();
            const int colon = m_faultCode.indexOf(QLatin1Char(':'));
            if (colon >= 0)
                m_faultCode = m_faultCode.mid(colon + 1);
        } else if (name == QStringLiteral("faultstring")) {
            m_faultString = xml.readElementText();
        } else if (name == QStringLiteral("detail")) {
            while (xml.readNextStartElement()) {
                if (xml.name() == QLatin1String("UPnPError")) {
                    const QMap<QString, QString> upnpError = flattenElement(xml);
                    m_upnpErrorCode = upnpError.value(QStringLiteral("errorCode"));
                    m_upnpErrorDescription = upnpError.value(QStringLiteral("errorDescription"));
                } else if (xml.name() == QLatin1String("refreshAuthTokenResult")) {
                    const QMap<QString, QString> refreshed = flattenElement(xml);
                    m_refreshedAuthToken = refreshed.value(QStringLiteral("authToken"));
                    m_refreshedPrivateKey = refreshed.value(QStringLiteral("privateKey"));
                } else {
                    xml.skipCurrentElement();
                }
            }
        } else {
            xml.skipCurrentElement();
        }
    }
}

}
