#pragma once

#include <QByteArray>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslConfiguration>
#include <QUrl>
#include <QXmlStreamWriter>

#include "../Logging.h"

namespace RoomTunes {

// Builds and sends a single SOAP request. Ported from the original
// soapy.hpp::Soap, modernized (toAscii() -> toUtf8(), no Cascades types).
class SoapRequest
{
public:
    SoapRequest(QNetworkAccessManager *netMgr, const QString &action, const QString &method,
                const QString &language = QString(), QSslConfiguration *sslConfig = nullptr)
        : m_netMgr(netMgr)
        , m_action(action)
        , m_method(method)
        , m_xml(&m_envelope)
        , m_language(language)
        , m_sslConfig(sslConfig)
    {
        m_xml.setAutoFormatting(true);
        m_xml.setAutoFormattingIndent(2);
    }

    QXmlStreamWriter &xmlWriter() { return m_xml; }

    void openEnvelope()
    {
        m_xml.writeStartElement(QStringLiteral("s:Envelope"));
        m_xml.writeNamespace(QStringLiteral("http://schemas.xmlsoap.org/soap/envelope/"), QStringLiteral("s"));
        m_xml.writeAttribute(QStringLiteral("s:encodingStyle"), QStringLiteral("http://schemas.xmlsoap.org/soap/encoding/"));
    }

    void openCommand(const QString &xmlns = QString())
    {
        m_xml.writeStartElement(QStringLiteral("s:Body"));

        if (xmlns.isEmpty()) {
            m_xml.writeStartElement(m_method);
            m_xml.writeDefaultNamespace(m_action);
        } else {
            m_xml.writeStartElement(xmlns + QLatin1Char(':') + m_method);
            m_xml.writeNamespace(m_action, xmlns);
        }
    }

    void closeCommand()
    {
        m_xml.writeEndElement();
        m_xml.writeEndElement();
    }

    void closeEnvelope() { m_xml.writeEndElement(); }

    void writeIntParameter(const QString &name, qint64 value) { m_xml.writeTextElement(name, QString::number(value)); }
    void writeStrParameter(const QString &name, const QString &value) { m_xml.writeTextElement(name, value); }

    QNetworkReply *subscribe(const QString &url, const QString &notifyUrl, int timeoutSeconds)
    {
        qCDebug(logSoap) << QUrl(url).host() << "SUBSCRIBE" << QUrl(url).path() << "callback=" << notifyUrl
                          << "timeout=" << timeoutSeconds;
        QNetworkRequest request{QUrl(url)};
        request.setRawHeader("CALLBACK", notifyUrl.toUtf8());
        request.setRawHeader("NT", "upnp:event");
        request.setRawHeader("TIMEOUT", QStringLiteral("Second-%1").arg(timeoutSeconds).toUtf8());
        return m_netMgr->sendCustomRequest(request, "SUBSCRIBE");
    }

    QNetworkReply *resubscribe(const QString &url, const QString &sid, int timeoutSeconds)
    {
        qCDebug(logSoap) << QUrl(url).host() << "SUBSCRIBE (renew)" << QUrl(url).path() << "sid=" << sid
                          << "timeout=" << timeoutSeconds;
        QNetworkRequest request{QUrl(url)};
        request.setRawHeader("SID", sid.toUtf8());
        request.setRawHeader("TIMEOUT", QStringLiteral("Second-%1").arg(timeoutSeconds).toUtf8());
        return m_netMgr->sendCustomRequest(request, "SUBSCRIBE");
    }

    QNetworkReply *unsubscribe(const QString &url, const QString &sid)
    {
        qCDebug(logSoap) << QUrl(url).host() << "UNSUBSCRIBE" << QUrl(url).path() << "sid=" << sid;
        QNetworkRequest request{QUrl(url)};
        request.setRawHeader("SID", sid.toUtf8());
        return m_netMgr->sendCustomRequest(request, "UNSUBSCRIBE");
    }

    QNetworkReply *send(const QString &url)
    {
        if (!m_netMgr)
            return nullptr;

        QNetworkRequest request{QUrl(url)};

        // Without this, a zone that's slow to respond (common during the
        // startup SSDP flood, when a dozen+ zones are all being hit with
        // device_description/GetHouseholdID/etc. requests at once) leaves
        // the request simply hanging -- QNetworkReply::finished never
        // fires, so callers relying on it (e.g. Household's music-service
        // catalog fetch, which retries on a *completed* failure) never even
        // get a chance to retry. A bounded timeout turns that hang into a
        // real, retryable failure instead.
        request.setTransferTimeout(10000);

        if (m_sslConfig)
            request.setSslConfiguration(*m_sslConfig);

        const QString action = QLatin1Char('"') + m_action + QStringLiteral("#") + m_method + QLatin1Char('"');

        request.setRawHeader("SOAPACTION", action.toUtf8());
        request.setRawHeader("Content-Type", "text/xml; charset=\"utf-8\"");
        request.setRawHeader("Connection", "close");

        // Sonos SOAP endpoints only reply in some locales unless we always
        // append 'en' as a fallback.
        QString lang = m_language;
        if (lang.isEmpty()) {
            const QString locale = QLocale::system().name().replace(QLatin1Char('_'), QLatin1Char('-'));
            if (locale == QStringLiteral("C") || locale.startsWith(QStringLiteral("en-")))
                lang = QStringLiteral("en-US,*");
            else
                lang = QStringLiteral("%1, en-US;q=0.9").arg(locale);
        }
        request.setRawHeader("Accept-Language", lang.toUtf8());

        // The single choke point every UPnP action (AVTransport,
        // ContentDirectory, MusicServices, ...) and SMAPI call (Smapi.h
        // also builds/sends its requests through SoapRequest) passes
        // through, so this is where "which zone player / host a command
        // is directed to" is logged from -- QUrl(url).host() is the
        // ZonePlayer's own IP for a UPnP action (ZonePlayer::baseUrl() is
        // built directly as "http://<ip>:1400/", no DNS involved) or the
        // SMAPI partner's hostname for a music-service call.
        qCDebug(logSoap) << QUrl(url).host() << m_method;

        QNetworkReply *reply = m_netMgr->post(request, m_envelope);
        reply->setProperty("soapMethod", m_method);
        reply->setProperty("soapAction", action);
        reply->setProperty("destHost", QUrl(url).host());
        return reply;
    }

private:
    QNetworkAccessManager *m_netMgr;
    QString m_action;
    QString m_method;
    QByteArray m_envelope;
    QXmlStreamWriter m_xml;
    QString m_language;
    QSslConfiguration *m_sslConfig;
};

}
