#include "ThirdPartyMediaServers.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QHash>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "../Logging.h"
#include "../crypto/Aes128Cbc.h"

#define QLOG_CATEGORY logServices

namespace RoomTunes
{

namespace
{

constexpr char kCryptoHeader[] = "2:";

// Fixed secret folded into the household-ID hash below. The original
// (ThirdPartyMediaServersX.cpp's calc_hhid_digest()) stores this as
// quint32 secret[4] = {0x31A7011A, 0xBD9E6EC9, 0x825147E8, 0x0EB774B2} and
// MD5-hashes it as a raw 16-byte buffer on a little-endian (ARM) device --
// spelled out here byte-for-byte rather than reinterpreted from quint32s,
// to not depend on this build's endianness matching that assumption.
constexpr quint8 kHhidSecret[16] = {
    0x1A, 0x01, 0xA7, 0x31, 0xC9, 0x6E, 0x9E, 0xBD, 0xE8, 0x47, 0x51, 0x82, 0xB2, 0x74, 0xB7, 0x0E,
};

QByteArray hhidDigest(const QString &householdId)
{
    // The original reads exactly 32 bytes from the household ID with no
    // bounds check, trusting it's always at least that long -- true for
    // every real Sonos household ID ("Sonos_..."/"HHID_..." are longer).
    const QByteArray hhid32 = householdId.toLatin1().left(32);

    QCryptographicHash hash(QCryptographicHash::Md5);
    hash.addData(hhid32);
    hash.addData(QByteArrayView(reinterpret_cast<const char *>(kHhidSecret), sizeof(kHhidSecret)));
    return hash.result();
}

QByteArray cipherKey(const QByteArray &hhidDigestBytes, const QByteArray &salt)
{
    QCryptographicHash hash(QCryptographicHash::Md5);
    hash.addData(salt);
    hash.addData(hhidDigestBytes);
    return hash.result();
}

// OpenSSL's EVP interface applies standard PKCS#7 padding by default (the
// original never calls EVP_CIPHER_CTX_set_padding(ctx, 0) to disable it),
// so the decrypted buffer needs it stripped before the trailing checksum
// and XML payload underneath are visible.
QByteArray stripPkcs7Padding(const QByteArray &data)
{
    if (data.isEmpty())
        return {};

    const auto padLen = static_cast<quint8>(data.at(data.size() - 1));
    if (padLen == 0 || padLen > 16 || padLen > data.size())
        return {};

    for (int i = data.size() - padLen; i < data.size(); ++i)
    {
        if (static_cast<quint8>(data.at(i)) != padLen)
            return {};
    }

    return data.left(data.size() - padLen);
}

QByteArray decrypt(const QString &householdId, const QByteArray &encoded)
{
    if (!encoded.startsWith(kCryptoHeader))
    {
        QWARN() << "ThirdPartyMediaServersX: unrecognized format" << encoded.left(32);
        return {};
    }

    const QByteArray decoded = QByteArray::fromBase64(encoded.mid(qstrlen(kCryptoHeader)));
    if (decoded.size() <= 16)
    {
        QWARN() << "ThirdPartyMediaServersX: payload too short";
        return {};
    }

    const QByteArray salt       = decoded.left(16);
    const QByteArray ciphertext = decoded.mid(16);

    const QByteArray key               = cipherKey(hhidDigest(householdId), salt);
    const QByteArray padded            = Aes128Cbc::decrypt(key, salt, ciphertext);
    const QByteArray plainWithChecksum = stripPkcs7Padding(padded);

    if (plainWithChecksum.size() < 5)
    {
        QWARN() << "ThirdPartyMediaServersX: decrypt failed or too short";
        return {};
    }

    const QByteArray content          = plainWithChecksum.chopped(4);
    const QByteArray checksum         = plainWithChecksum.right(4);
    const QByteArray expectedChecksum = QCryptographicHash::hash(content, QCryptographicHash::Md5).left(4);

    if (checksum != expectedChecksum)
    {
        QWARN() << "ThirdPartyMediaServersX: checksum mismatch (wrong household id?)";
        return {};
    }

    return content;
}

QString redactedFormattedXml(QString xml)
{
    // TPMSX contains account passwords and AppLink/DeviceLink token/key
    // material. Keep the startup dump structurally complete but safe to
    // paste into bug reports/logs.
    xml.replace(
        QRegularExpression(
            QStringLiteral(
                R"(\b((?:Password|Password0|Token|Token0|Key|Key0|AuthToken|PrivateKey|SessionId)\d*)="[^"]*")"),
            QRegularExpression::CaseInsensitiveOption),
        QStringLiteral(R"(\1="<redacted>")"));
    xml.replace(
        QRegularExpression(
            QStringLiteral(
                "<((?:[A-Za-z_][\\w.-]*:)?(?:authToken|privateKey|sessionId|password|token|key))([^>]*)>.*?</\\1>"),
            QRegularExpression::CaseInsensitiveOption),
        QStringLiteral("<\\1\\2><redacted></\\1>"));

    QString          output;
    QXmlStreamReader reader(xml);
    QXmlStreamWriter writer(&output);
    writer.setAutoFormatting(true);
    writer.setAutoFormattingIndent(2);

    while (!reader.atEnd())
    {
        reader.readNext();
        if (reader.hasError())
            break;
        if (reader.tokenType() != QXmlStreamReader::StartDocument)
            writer.writeCurrentToken(reader);
    }

    return output.isEmpty() ? xml : output;
}

} // namespace

QList<InstalledService> ThirdPartyMediaServers::parse(const QString &householdId, const QString &encoded)
{
    QList<InstalledService> services;
    if (householdId.isEmpty() || encoded.isEmpty())
        return services;

    const QByteArray xml = decrypt(householdId, encoded.toUtf8());
    if (xml.isEmpty())
        return services;

    if (verboseLoggingEnabled())
    {
        QLOG() << "ThirdPartyMediaServersX decrypted XML:";
        QLOG() << redactedFormattedXml(QString::fromUtf8(xml));
    }

    static const QRegularExpression kUdnPattern(QStringLiteral("^SA_RINCON(\\d+)_(.*)$"));

    // A linked (DeviceLink/AppLink) service's token/key show up under a
    // *separate* <Service> element from its regular Username0/Password0
    // one -- same numeric serviceId prefix, but UDN suffix
    // "X_#Svc<serviceId>-0-Token" instead of a real username. Track which
    // list index a serviceId is already at so the two elements merge into
    // one InstalledService rather than producing a phantom duplicate entry.
    QHash<int, int> indexByServiceId;

    QXmlStreamReader reader(xml);
    while (!reader.atEnd())
    {
        if (!reader.readNextStartElement())
            continue;
        if (reader.name() != QLatin1String("Service"))
        {
            // Don't skip -- this also matches the root <MediaServers>
            // wrapper, which needs descending into (via the next bare
            // readNextStartElement()) rather than skipping past.
            continue;
        }

        const QXmlStreamAttributes    attrs = reader.attributes();
        const QString                 udn   = attrs.value(QStringLiteral("UDN")).toString();
        const QRegularExpressionMatch match = kUdnPattern.match(udn);
        if (match.hasMatch())
        {
            const int     serviceId = match.captured(1).toInt();
            QString       username  = attrs.value(QStringLiteral("Username0")).toString();
            const QString password  = attrs.value(QStringLiteral("Password0")).toString();
            const QString token     = attrs.value(QStringLiteral("Token0")).toString();
            const QString key       = attrs.value(QStringLiteral("Key0")).toString();
            const QString nickname  = attrs.value(QStringLiteral("Nickname0")).toString();

            // The UDN suffix is only a real fallback username for the
            // legacy "SA_RINCON<id>_<username>" shape; on a token-only
            // element it's a synthetic "X_#Svc<id>-0-Token" string that
            // must not leak in as a fake username.
            if (username.isEmpty() && token.isEmpty())
                username = match.captured(2);

            const auto it = indexByServiceId.constFind(serviceId);
            if (it != indexByServiceId.constEnd())
            {
                InstalledService &existing = services[it.value()];
                if (existing.username.isEmpty())
                    existing.username = username;
                if (existing.password.isEmpty())
                    existing.password = password;
                if (existing.token.isEmpty())
                    existing.token = token;
                if (existing.key.isEmpty())
                    existing.key = key;
                if (existing.nickname.isEmpty())
                    existing.nickname = nickname;
            }
            else
            {
                InstalledService service;
                service.serviceId = serviceId;
                service.username  = username;
                service.password  = password;
                service.token     = token;
                service.key       = key;
                service.nickname  = nickname;
                indexByServiceId.insert(serviceId, services.size());
                services.append(service);
            }
        }

        reader.skipCurrentElement();
    }

    QLOG() << "ThirdPartyMediaServersX: parsed" << services.size() << "installed service(s)";
    return services;
}

} // namespace RoomTunes
