#pragma once

#include <QDateTime>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSslConfiguration>
#include <QString>

#include <cstdlib>

#include "../control/Soap.h"

namespace RoomTunes
{

// Generic Sonos Music API (SMAPI) client. This is the protocol Spotify (and
// most other Sonos music partners) use -- ported from services/smapi.h.
class Smapi
{
  public:
    enum class CredentialType
    {
        None,
        Basic,
        LoginToken,
        SessionId
    };

    Smapi(QNetworkAccessManager *netMgr, const QString &serviceUrl) : m_netMgr(netMgr)
    {
        bindService(serviceUrl);
    }

    QSslConfiguration &sslConfig()
    {
        return m_sslConfig;
    }

    void bindService(const QString &serviceUrl)
    {
        m_soapUrl    = serviceUrl;
        m_soapAction = QStringLiteral("http://www.sonos.com/Services/1.1");
    }

    void setLanguage(const QString &lang)
    {
        m_language = lang;
    }

    void setContextEnabled(bool enabled)
    {
        m_contextEnabled = enabled;
    }

    void setDeviceCredentials(const QString &deviceId, const QString &deviceProvider)
    {
        m_credType                               = CredentialType::Basic;
        m_cred[QStringLiteral("deviceId")]       = deviceId;
        m_cred[QStringLiteral("deviceProvider")] = deviceProvider;
    }

    void setSessionIdCredentials(const QString &deviceId, const QString &deviceProvider, const QString &sessionId)
    {
        m_credType                               = CredentialType::SessionId;
        m_cred[QStringLiteral("deviceId")]       = deviceId;
        m_cred[QStringLiteral("deviceProvider")] = deviceProvider;
        m_cred[QStringLiteral("sessionId")]      = sessionId;
    }

    void setLoginTokenCredentials(const QString &deviceId, const QString &deviceProvider, const QString &token,
                                  const QString &key, const QString &householdId)
    {
        m_credType                               = CredentialType::LoginToken;
        m_cred[QStringLiteral("deviceId")]       = deviceId;
        m_cred[QStringLiteral("deviceProvider")] = deviceProvider;
        m_cred[QStringLiteral("token")]          = token;
        m_cred[QStringLiteral("key")]            = key;
        m_cred[QStringLiteral("householdId")]    = householdId;
    }

    QString credential(const QString &name) const
    {
        return m_cred.value(name);
    }

    QNetworkReply *getSessionId(const QString &username, const QString &password)
    {
        SoapRequest request(m_netMgr, m_soapAction, QStringLiteral("getSessionId"), m_language, &m_sslConfig);
        configureSmapiRequest(request);
        request.openEnvelope();
        writeCredentials(request, false);
        request.openCommand();
        request.writeStrParameter(QStringLiteral("username"), username);
        request.writeStrParameter(QStringLiteral("password"), password);
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

    QNetworkReply *getMetadata(const QString &id, int index, int count)
    {
        SoapRequest request(m_netMgr, m_soapAction, QStringLiteral("getMetadata"), m_language, &m_sslConfig);
        configureSmapiRequest(request);
        request.openEnvelope();
        writeCredentials(request);
        request.openCommand();
        request.writeStrParameter(QStringLiteral("id"), id);
        request.writeIntParameter(QStringLiteral("index"), index);
        request.writeIntParameter(QStringLiteral("count"), count);
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

    QNetworkReply *search(const QString &id, const QString &term, int index, int count)
    {
        SoapRequest request(m_netMgr, m_soapAction, QStringLiteral("search"), m_language, &m_sslConfig);
        configureSmapiRequest(request);
        request.openEnvelope();
        writeCredentials(request);
        request.openCommand();
        request.writeStrParameter(QStringLiteral("id"), id);
        request.writeStrParameter(QStringLiteral("term"), term);
        request.writeIntParameter(QStringLiteral("index"), index);
        request.writeIntParameter(QStringLiteral("count"), count);
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

    QNetworkReply *getMediaMetadata(const QString &id)
    {
        SoapRequest request(m_netMgr, m_soapAction, QStringLiteral("getMediaMetadata"), m_language, &m_sslConfig);
        configureSmapiRequest(request);
        request.openEnvelope();
        writeCredentials(request);
        request.openCommand();
        request.writeStrParameter(QStringLiteral("id"), id);
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

    QNetworkReply *rateItem(const QString &id, const QString &rating)
    {
        SoapRequest request(m_netMgr, m_soapAction, QStringLiteral("rateItem"), m_language, &m_sslConfig);
        configureSmapiRequest(request);
        request.openEnvelope();
        writeCredentials(request);
        request.openCommand();
        request.writeStrParameter(QStringLiteral("id"), id);
        request.writeStrParameter(QStringLiteral("rating"), rating);
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

    // DeviceLink auth flow (used by Spotify): the caller shows linkCode/regUrl
    // to the user, who authorizes in a browser, then getDeviceAuthToken()
    // exchanges the link code for a token/key pair.
    QNetworkReply *getDeviceLinkCode(const QString &householdId)
    {
        SoapRequest request(m_netMgr, m_soapAction, QStringLiteral("getDeviceLinkCode"), m_language, &m_sslConfig);
        configureSmapiRequest(request);
        request.openEnvelope();
        writeCredentials(request);
        request.openCommand();
        request.writeStrParameter(QStringLiteral("householdId"), householdId);
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

    QNetworkReply *getAppLink(const QString &householdId, const QString &hardware, const QString &osVersion,
                              const QString &sonosAppName, const QString &callbackPath)
    {
        SoapRequest request(m_netMgr, m_soapAction, QStringLiteral("getAppLink"), m_language, &m_sslConfig);
        configureSmapiRequest(request);
        request.openEnvelope();
        writeCredentials(request);
        request.openCommand();
        request.writeStrParameter(QStringLiteral("householdId"), householdId);
        request.writeStrParameter(QStringLiteral("hardware"), hardware);
        request.writeStrParameter(QStringLiteral("osVersion"), osVersion);
        request.writeStrParameter(QStringLiteral("sonosAppName"), sonosAppName);
        request.writeStrParameter(QStringLiteral("callbackPath"), callbackPath);
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

    QNetworkReply *getDeviceAuthToken(const QString &householdId, const QString &linkCode,
                                      const QString &linkDeviceId = QString())
    {
        SoapRequest request(m_netMgr, m_soapAction, QStringLiteral("getDeviceAuthToken"), m_language, &m_sslConfig);
        configureSmapiRequest(request);
        request.openEnvelope();
        writeCredentials(request);
        request.openCommand();
        request.writeStrParameter(QStringLiteral("householdId"), householdId);
        request.writeStrParameter(QStringLiteral("linkCode"), linkCode);
        if (!linkDeviceId.isEmpty())
            request.writeStrParameter(QStringLiteral("linkDeviceId"), linkDeviceId);
        request.closeCommand();
        request.closeEnvelope();
        return request.send(m_soapUrl);
    }

  private:
    static void configureBaseSmapiRequest(SoapRequest &request)
    {
        // SMAPI is an internet SOAP service, not a UPnP control endpoint.
        // Modern partners such as BBC Sounds can be stricter than speakers;
        // Sonos' own SMAPI examples use a plain SOAP envelope without the
        // SOAP encodingStyle attribute that UPnP actions traditionally carry.
        request.setSoapEncodingStyleEnabled(false);
    }

    void configureSmapiRequest(SoapRequest &request) const
    {
        configureBaseSmapiRequest(request);
    }

    void writeCredentials(SoapRequest &request, bool full = true)
    {
        QXmlStreamWriter &xml = request.xmlWriter();

        switch (m_credType)
        {
        case CredentialType::Basic:
            xml.writeStartElement(QStringLiteral("s:Header"));
            xml.writeStartElement(QStringLiteral("credentials"));
            xml.writeDefaultNamespace(QStringLiteral("http://www.sonos.com/Services/1.1"));
            writeCredential(xml, QStringLiteral("deviceId"));
            writeCredential(xml, QStringLiteral("deviceProvider"));
            xml.writeEndElement();
            if (m_contextEnabled)
                writeContext(xml);
            xml.writeEndElement();
            break;

        case CredentialType::SessionId:
            xml.writeStartElement(QStringLiteral("s:Header"));
            xml.writeStartElement(QStringLiteral("credentials"));
            xml.writeDefaultNamespace(QStringLiteral("http://www.sonos.com/Services/1.1"));
            writeCredential(xml, QStringLiteral("deviceId"));
            writeCredential(xml, QStringLiteral("deviceProvider"));
            if (full)
                writeCredential(xml, QStringLiteral("sessionId"));
            xml.writeEndElement();
            if (m_contextEnabled)
                writeContext(xml);
            xml.writeEndElement();
            break;

        case CredentialType::LoginToken:
            xml.writeStartElement(QStringLiteral("s:Header"));
            xml.writeStartElement(QStringLiteral("credentials"));
            xml.writeDefaultNamespace(QStringLiteral("http://www.sonos.com/Services/1.1"));
            writeCredential(xml, QStringLiteral("deviceId"));
            writeCredential(xml, QStringLiteral("deviceProvider"));
            if (full)
            {
                xml.writeStartElement(QStringLiteral("loginToken"));
                writeCredential(xml, QStringLiteral("token"));
                writeCredential(xml, QStringLiteral("key"));
                writeCredential(xml, QStringLiteral("householdId"));
                xml.writeEndElement();
            }
            xml.writeEndElement();
            if (m_contextEnabled)
                writeContext(xml);
            xml.writeEndElement();
            break;

        case CredentialType::None:
            if (m_contextEnabled)
            {
                xml.writeStartElement(QStringLiteral("s:Header"));
                writeContext(xml);
                xml.writeEndElement();
            }
            break;
        }
    }

    void writeContext(QXmlStreamWriter &xml)
    {
        const int     offsetSeconds = QDateTime::currentDateTime().offsetFromUtc();
        const QChar   sign          = offsetSeconds < 0 ? QLatin1Char('-') : QLatin1Char('+');
        const int     offsetMinutes = std::abs(offsetSeconds) / 60;
        const QString timeZone      = QStringLiteral("%1%2:%3")
                                     .arg(sign)
                                     .arg(offsetMinutes / 60, 2, 10, QLatin1Char('0'))
                                     .arg(offsetMinutes % 60, 2, 10, QLatin1Char('0'));

        xml.writeStartElement(QStringLiteral("context"));
        xml.writeDefaultNamespace(QStringLiteral("http://www.sonos.com/Services/1.1"));
        xml.writeTextElement(QStringLiteral("timeZone"), timeZone);
        xml.writeEndElement();
    }

    void writeCredential(QXmlStreamWriter &xml, const QString &name)
    {
        xml.writeTextElement(name, m_cred.value(name));
    }

  private:
    QNetworkAccessManager *m_netMgr;
    QString                m_soapUrl;
    QString                m_soapAction;
    QString                m_language;
    QSslConfiguration      m_sslConfig = QSslConfiguration::defaultConfiguration();

    QMap<QString, QString> m_cred;
    CredentialType         m_credType       = CredentialType::None;
    bool                   m_contextEnabled = false;
};

} // namespace RoomTunes
