#pragma once

#include <QByteArray>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslConfiguration>
#include <QSslError>
#include <QStringList>
#include <QUrl>
#include <QXmlStreamWriter>

#include "../Logging.h"

namespace RoomTunes
{

// Builds and sends a single SOAP request. Ported from the original
// soapy.hpp::Soap, modernized (toAscii() -> toUtf8(), no Cascades types).
class SoapRequest
{
  public:
    SoapRequest(QNetworkAccessManager *netMgr, const QString &action, const QString &method,
                const QString &language = QString(), QSslConfiguration *sslConfig = nullptr)
        : m_netMgr(netMgr), m_action(action), m_method(method), m_xml(&m_envelope), m_language(language),
          m_sslConfig(sslConfig)
    {
        m_xml.setAutoFormatting(true);
        m_xml.setAutoFormattingIndent(2);
    }

    QXmlStreamWriter &xmlWriter()
    {
        return m_xml;
    }

    void setSoapEncodingStyleEnabled(bool enabled)
    {
        m_useSoapEncodingStyle = enabled;
    }

    static void setUserAgent(const QString &userAgent)
    {
        s_userAgent = userAgent;
    }

    static const QString &userAgent()
    {
        return s_userAgent;
    }

    void openEnvelope()
    {
        m_xml.writeStartElement(QStringLiteral("s:Envelope"));
        m_xml.writeNamespace(QStringLiteral("http://schemas.xmlsoap.org/soap/envelope/"), QStringLiteral("s"));
        if (m_useSoapEncodingStyle)
            m_xml.writeAttribute(QStringLiteral("s:encodingStyle"),
                                 QStringLiteral("http://schemas.xmlsoap.org/soap/encoding/"));
    }

    void openCommand(const QString &xmlns = QString())
    {
        m_xml.writeStartElement(QStringLiteral("s:Body"));
        if (xmlns.isEmpty())
        {
            m_xml.writeStartElement(m_method);
            m_xml.writeDefaultNamespace(m_action);
        }
        else
        {
            m_xml.writeStartElement(xmlns + QLatin1Char(':') + m_method);
            m_xml.writeNamespace(m_action, xmlns);
        }
    }

    void closeCommand()
    {
        m_xml.writeEndElement();
        m_xml.writeEndElement();
    }

    void closeEnvelope()
    {
        m_xml.writeEndElement();
    }

    void writeIntParameter(const QString &name, qint64 value)
    {
        const QString text = QString::number(value);
        recordParameter(name, text, false);
        m_xml.writeTextElement(name, text);
    }

    void writeStrParameter(const QString &name, const QString &value)
    {
        recordParameter(name, value, true);
        m_xml.writeTextElement(name, value);
    }

    QNetworkReply *subscribe(const QString &url, const QString &notifyUrl, int timeoutSeconds)
    {
        QNetworkRequest request{QUrl(url)};
        request.setRawHeader("CALLBACK", notifyUrl.toUtf8());
        request.setRawHeader("NT", "upnp:event");
        request.setRawHeader("TIMEOUT", QStringLiteral("Second-%1").arg(timeoutSeconds).toUtf8());
        return m_netMgr->sendCustomRequest(request, "SUBSCRIBE");
    }

    QNetworkReply *resubscribe(const QString &url, const QString &sid, int timeoutSeconds)
    {
        QNetworkRequest request{QUrl(url)};
        request.setRawHeader("SID", sid.toUtf8());
        request.setRawHeader("TIMEOUT", QStringLiteral("Second-%1").arg(timeoutSeconds).toUtf8());
        return m_netMgr->sendCustomRequest(request, "SUBSCRIBE");
    }

    QNetworkReply *unsubscribe(const QString &url, const QString &sid)
    {
        qCDebug(logSoap).noquote() << directedHost(url, QLatin1Char('>')) << "UNSUBSCRIBE" << QUrl(url).path()
                                   << "sid=" << sid;
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
        const QString userAgent =
            s_userAgent.isEmpty()
                ? QStringLiteral("Linux UPnP/1.0 Sonos/80.0-00000 (WDCR:Microsoft Windows NT 10.0.22631)")
                : s_userAgent;
        request.setRawHeader("User-Agent", userAgent.toUtf8());

        // Sonos SOAP endpoints only reply in some locales unless we always
        // append 'en' as a fallback.
        QString lang = m_language;
        if (lang.isEmpty())
        {
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
        qCDebug(logSoap).noquote() << directedHost(url, QLatin1Char('>')) << callSummary();

        QNetworkReply *reply = m_netMgr->post(request, m_envelope);
        reply->setProperty("soapMethod", m_method);
        reply->setProperty("soapAction", action);
        reply->setProperty("soapBody", QString::fromUtf8(m_envelope));
        reply->setProperty("destHost", QUrl(url).host());
        QObject::connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError> &errors) {
            for (const QSslError &error : errors)
            {
                qCWarning(logSoap).noquote() << QStringLiteral("%1 %2 SSL error: %3")
                                                    .arg(QLatin1Char('<') + reply->property("destHost").toString(),
                                                         reply->property("soapMethod").toString(), error.errorString());
            }
        });
        return reply;
    }

  private:
    static QString directedHost(const QString &url, QChar direction)
    {
        return direction + QUrl(url).host();
    }

    static bool isSensitiveParameter(const QString &name)
    {
        const QString lower = name.toLower();
        return lower.contains(QStringLiteral("password")) || lower.contains(QStringLiteral("token")) ||
               lower.contains(QStringLiteral("key"));
    }

    static bool isLargeXmlParameter(const QString &name)
    {
        const QString lower = name.toLower();
        return lower.contains(QStringLiteral("metadata")) || lower == QStringLiteral("elements");
    }

    static QString compactLogValue(QString value)
    {
        value.replace(QLatin1Char('\r'), QLatin1Char(' '));
        value.replace(QLatin1Char('\n'), QLatin1Char(' '));
        value.replace(QLatin1Char('\t'), QLatin1Char(' '));

        constexpr qsizetype maxLength = 180;
        if (value.size() > maxLength)
            value = value.left(maxLength - 3) + QStringLiteral("...");
        return value;
    }

    static QString quoteLogValue(QString value)
    {
        value.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
        value.replace(QLatin1Char('"'), QStringLiteral("\\\""));
        return QLatin1Char('"') + value + QLatin1Char('"');
    }

    void recordParameter(const QString &name, const QString &value, bool quote)
    {
        QString display;
        bool    forceUnquoted = false;

        if (isSensitiveParameter(name))
        {
            display       = QStringLiteral("<redacted>");
            forceUnquoted = true;
        }
        else if (isLargeXmlParameter(name))
        {
            display       = QStringLiteral("<%1 chars>").arg(value.size());
            forceUnquoted = true;
        }
        else
        {
            display = compactLogValue(value);
        }

        if (quote && !forceUnquoted)
            display = quoteLogValue(display);

        m_parameters.append(name + QLatin1Char('=') + display);
    }

    QString callSummary() const
    {
        return m_method + QLatin1Char('(') + m_parameters.join(QStringLiteral(", ")) + QLatin1Char(')');
    }

    QNetworkAccessManager *m_netMgr;
    QString                m_action;
    QString                m_method;
    QByteArray             m_envelope;
    QXmlStreamWriter       m_xml;
    QString                m_language;
    QSslConfiguration     *m_sslConfig;
    QStringList            m_parameters;
    bool                   m_useSoapEncodingStyle = true;
    inline static QString  s_userAgent;
};

} // namespace RoomTunes
