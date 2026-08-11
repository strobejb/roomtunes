#pragma once

#include <initializer_list>
#include <string_view>

#include <QAnyStringView>
#include <QMap>
#include <QNetworkReply>
#include <QString>

#include "../xml/XmlUtils.h"

namespace RoomTunes
{

// Parses a completed SOAP QNetworkReply. Sonos SOAP responses are shallow
// (Envelope > Body > ActionResponse > params), so this flattens the response
// body into a name -> text map, mirroring how ZonePlayer's state map already
// worked. Nested XML-as-text payloads (e.g. TrackMetaData containing escaped
// DIDL-Lite) come back as plain strings for the caller to re-parse with
// Didl::parseItems().
class SoapResponse
{
  public:
    // senderObject is typically QObject::sender() from a QNetworkReply::finished() slot.
    explicit SoapResponse(QObject *senderObject);

    QNetworkReply *reply() const
    {
        return m_reply;
    }

    // Full response body, for callers whose response shape doesn't fit the flat
    // name -> text map (e.g. SMAPI's repeated <mediaCollection>/<mediaMetadata>
    // siblings), so they can run their own parser pass.
    const QByteArray &rawBody() const
    {
        return m_rawBody;
    }

    bool hasFault() const
    {
        return m_hasFault;
    }

    const QString &faultCode() const
    {
        return m_faultCode;
    }

    const QString &faultString() const
    {
        return m_faultString;
    }

    const QString &upnpErrorCode() const
    {
        return m_upnpErrorCode;
    }

    const QString &upnpErrorDescription() const
    {
        return m_upnpErrorDescription;
    }

    QString diagnosticText() const;

    // Present only on a SMAPI "Client.TokenRefreshRequired" fault -- the
    // SMAPI server hands back a replacement DeviceLink token/key right in
    // the fault detail (<refreshAuthTokenResult>) instead of just
    // rejecting the call, so the caller can update its stored credentials
    // and retry rather than treating this as a hard failure. See
    // SmapiService::runMetadataRequest().
    const QString &refreshedAuthToken() const
    {
        return m_refreshedAuthToken;
    }

    const QString &refreshedPrivateKey() const
    {
        return m_refreshedPrivateKey;
    }

    // true if the network request failed OR the SOAP body was a Fault
    bool error() const;

    // value of an immediate child element of the response node, e.g. value("CurrentVolume")
    QString value(QAnyStringView name) const
    {
        return m_values.value(name.toString());
    }

    QString firstValue(std::initializer_list<std::string_view> names,
                       Qt::CaseSensitivity                     sensitivity = Qt::CaseInsensitive) const
    {
        for (std::string_view name : names)
        {
            const QString wanted = QString::fromUtf8(name.data(), qsizetype(name.size()));
            if (sensitivity == Qt::CaseSensitive)
            {
                const QString value = m_values.value(wanted);
                if (!value.isEmpty())
                    return value;
                continue;
            }

            for (auto it = m_values.cbegin(); it != m_values.cend(); ++it)
            {
                if (it.key().compare(wanted, sensitivity) == 0 && !it.value().isEmpty())
                    return it.value();
            }
        }
        return {};
    }

    bool boolValue(QAnyStringView name, bool defaultValue = false) const
    {
        const QString text = value(name);
        if (text.isEmpty())
            return defaultValue;
        return text.compare(QStringLiteral("false"), Qt::CaseInsensitive) != 0 && text != QStringLiteral("0");
    }

    const QMap<QString, QString> &values() const
    {
        return m_values;
    }

    int httpStatusCode() const;

  private:
    void parse(const QByteArray &body);
    void parseFault(const XmlNode &fault);

  private:
    QNetworkReply *m_reply;

    bool    m_hasFault = false;
    QString m_faultCode;
    QString m_faultString;
    QString m_upnpErrorCode;
    QString m_upnpErrorDescription;
    QString m_refreshedAuthToken;
    QString m_refreshedPrivateKey;

    QMap<QString, QString> m_values;
    QByteArray             m_rawBody;
};

} // namespace RoomTunes
