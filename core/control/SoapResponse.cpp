#include "SoapResponse.h"

#include "../Logging.h"
#include "../xml/XmlUtils.h"

#include <QRegularExpression>

namespace RoomTunes {

namespace {

QString directedHost(const QNetworkReply *reply)
{
    const QString host = reply ? reply->property("destHost").toString() : QString();
    return QLatin1Char('<') + (host.isEmpty() ? QStringLiteral("?") : host);
}

QString compactXmlForLog(QString xml)
{
    xml.replace(QRegularExpression(QStringLiteral("<((?:[A-Za-z_][\\w.-]*:)?(?:authToken|privateKey|sessionId|password|token|key))([^>]*)>.*?</\\1>"),
                                   QRegularExpression::CaseInsensitiveOption),
                QStringLiteral("<\\1\\2><redacted></\\1>"));
    xml.replace(QLatin1Char('\r'), QLatin1Char(' '));
    xml.replace(QLatin1Char('\n'), QLatin1Char(' '));
    xml.replace(QLatin1Char('\t'), QLatin1Char(' '));
    xml = xml.simplified();

    constexpr qsizetype maxLength = 2000;
    if (xml.size() > maxLength)
        xml = xml.left(maxLength - 3) + QStringLiteral("...");
    return xml;
}

}

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

        if (error()) {
            qCWarning(logSoap).noquote()
                << directedHost(m_reply) << QStringLiteral("SOAPERR:") << diagnosticText();
            const QString requestXml = m_reply->property("soapBody").toString();
            if (!requestXml.isEmpty())
                qCWarning(logSoap).noquote()
                    << directedHost(m_reply) << QStringLiteral("SOAPENV:") << compactXmlForLog(requestXml);
            if (!m_rawBody.isEmpty())
                qCWarning(logSoap).noquote()
                    << directedHost(m_reply) << QStringLiteral("SOAPXML:") << compactXmlForLog(QString::fromUtf8(m_rawBody));
        }
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

QString SoapResponse::diagnosticText() const
{
    if (!m_reply)
        return QStringLiteral("no QNetworkReply");

    QStringList parts;
    const QString method = m_reply->property("soapMethod").toString();
    const QString action = m_reply->property("soapAction").toString();
    const QString url = m_reply->url().toString();
    const QString httpReason = m_reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();

    if (!method.isEmpty())
        parts << QStringLiteral("method=%1").arg(method);
    if (!action.isEmpty())
        parts << QStringLiteral("action=%1").arg(action);
    if (!url.isEmpty())
        parts << QStringLiteral("url=%1").arg(url);

    parts << QStringLiteral("network=%1 %2").arg(int(m_reply->error())).arg(m_reply->errorString());
    parts << QStringLiteral("http=%1%2")
                 .arg(httpStatusCode())
                 .arg(httpReason.isEmpty() ? QString() : QStringLiteral(" ") + httpReason);

    if (!m_faultCode.isEmpty())
        parts << QStringLiteral("faultCode=%1").arg(m_faultCode);
    if (!m_faultString.isEmpty())
        parts << QStringLiteral("faultString=%1").arg(m_faultString);
    if (!m_upnpErrorCode.isEmpty())
        parts << QStringLiteral("upnp=%1 %2").arg(m_upnpErrorCode, m_upnpErrorDescription);

    return parts.join(QStringLiteral("; "));
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
