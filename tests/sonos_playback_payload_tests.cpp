#include "control/SonosPlaybackPayload.h"
#include "support/FakeNetworkAccessManager.h"
#include "zone/ZonePlayer.h"

#include <QTest>

using namespace RoomTunes;
using namespace RoomTunes::Tests;

class SonosPlaybackPayloadTests : public QObject
{
    Q_OBJECT

  private slots:
    void streamItemsAreNotQueueable_data();
    void streamItemsAreNotQueueable();
    void musicTrackIsQueueable();
    void metadataIncludesPlayableResource();
    void metadataInfersRadioProtocolInfo();
    void tvSourceUsesDirectTransportUri();
    void lineInSourceUsesDirectTransportUri();
};

namespace
{

QByteArray soapEnvelope(const QByteArray &body)
{
    return QByteArrayLiteral(R"xml(
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/">
  <s:Body>
)xml") + body + QByteArrayLiteral(R"xml(
  </s:Body>
</s:Envelope>
)xml");
}

} // namespace

void SonosPlaybackPayloadTests::streamItemsAreNotQueueable_data()
{
    QTest::addColumn<QString>("uri");
    QTest::addColumn<QString>("upnpClass");

    QTest::newRow("audio-broadcast-class")
        << QStringLiteral("x-sonosapi-stream:s25419?sid=254&flags=0")
        << QStringLiteral("object.item.audioItem.audioBroadcast");
    QTest::newRow("radio-program-uri") << QStringLiteral("x-sonosapi-radio:p123?sid=254&flags=0")
                                       << QStringLiteral("object.item.audioItem.audioBroadcast");
    QTest::newRow("hls-uri") << QStringLiteral("x-sonosapi-hls:s123?sid=37&flags=0") << QString();
    QTest::newRow("mp3-radio-uri") << QStringLiteral("x-rincon-mp3radio://example.test/live") << QString();
    QTest::newRow("line-in-uri") << QStringLiteral("x-rincon-stream:RINCON_123") << QString();
    QTest::newRow("tv-uri") << QStringLiteral("x-sonos-htastream:RINCON_123:spdif") << QString();
    QTest::newRow("dock-uri") << QStringLiteral("x-sonos-dock:RINCON_123") << QString();
    QTest::newRow("pandora-radio-uri") << QStringLiteral("pndrradio:station:123") << QString();
    QTest::newRow("rhapsody-radio-uri") << QStringLiteral("rdradio:station:123") << QString();
}

void SonosPlaybackPayloadTests::streamItemsAreNotQueueable()
{
    QFETCH(QString, uri);
    QFETCH(QString, upnpClass);

    QVariantMap item;
    item[QStringLiteral("uri")]       = uri;
    item[QStringLiteral("upnpClass")] = upnpClass;

    QVERIFY(SonosPlaybackPayload::isStreamItem(item));
    QVERIFY(!SonosPlaybackPayload::isQueueableItem(item));
}

void SonosPlaybackPayloadTests::musicTrackIsQueueable()
{
    QVariantMap item;
    item[QStringLiteral("uri")]       = QStringLiteral("x-file-cifs://server/share/track.mp3");
    item[QStringLiteral("upnpClass")] = QStringLiteral("object.item.audioItem.musicTrack");

    QVERIFY(!SonosPlaybackPayload::isStreamItem(item));
    QVERIFY(SonosPlaybackPayload::isQueueableItem(item));
}

void SonosPlaybackPayloadTests::metadataIncludesPlayableResource()
{
    QVariantMap item;
    item[QStringLiteral("id")]           = QStringLiteral("track-1");
    item[QStringLiteral("parentId")]     = QStringLiteral("A:TRACKS");
    item[QStringLiteral("title")]        = QStringLiteral("Track One");
    item[QStringLiteral("uri")]          = QStringLiteral("x-file-cifs://server/share/track.mp3");
    item[QStringLiteral("protocolInfo")] = QStringLiteral("x-file-cifs:*:audio/mpeg:*");
    item[QStringLiteral("upnpClass")]    = QStringLiteral("object.item.audioItem.musicTrack");

    const QByteArray metadata = SonosPlaybackPayload::buildItemMetadata(item);

    QVERIFY(metadata.contains("<res protocolInfo=\"x-file-cifs:*:audio/mpeg:*\">x-file-cifs://server/share/track.mp3</res>"));
}

void SonosPlaybackPayloadTests::metadataInfersRadioProtocolInfo()
{
    QVariantMap item;
    item[QStringLiteral("id")]        = QStringLiteral("R:0/0/0");
    item[QStringLiteral("parentId")]  = QStringLiteral("R:0/0");
    item[QStringLiteral("title")]     = QStringLiteral("BBC Radio 4");
    item[QStringLiteral("uri")]       = QStringLiteral("x-sonosapi-stream:s25419?sid=254&flags=32");
    item[QStringLiteral("upnpClass")] = QStringLiteral("object.item.audioItem.audioBroadcast");

    const QByteArray metadata = SonosPlaybackPayload::buildItemMetadata(item);

    QVERIFY(metadata.contains(
        "<res protocolInfo=\"x-rincon-mp3radio:*:*:*\">x-sonosapi-stream:s25419?sid=254&amp;flags=32</res>"));
}

void SonosPlaybackPayloadTests::tvSourceUsesDirectTransportUri()
{
    FakeNetworkAccessManager net;
    ZonePlayer               zone{&net, QStringLiteral("192.0.2.10"), QStringLiteral("RINCON_123")};
    zone.setModelName(QStringLiteral("PLAYBAR"));
    const QVariantMap item = zone.sourceItems().last().toMap();

    QCOMPARE(item.value(QStringLiteral("kind")).toString(), QStringLiteral("tv"));
    net.enqueueResponse({soapEnvelope(QByteArrayLiteral(R"xml(
<u:SetAVTransportURIResponse xmlns:u="urn:schemas-upnp-org:service:AVTransport:1"/>
)xml"))});
    net.enqueueResponse({soapEnvelope(QByteArrayLiteral(R"xml(
<u:PlayResponse xmlns:u="urn:schemas-upnp-org:service:AVTransport:1"/>
)xml"))});

    zone.playItem(item);
    net.completePendingReplies();
    net.completePendingReplies();

    QCOMPARE(net.requests().size(), 2);
    QCOMPARE(net.requests().at(0).rawHeader("SOAPACTION"),
             QByteArrayLiteral("\"urn:schemas-upnp-org:service:AVTransport:1#SetAVTransportURI\""));
    QVERIFY(net.requests().at(0).body.contains("<CurrentURI>x-sonos-htastream:RINCON_123:spdif</CurrentURI>"));
    QCOMPARE(net.requests().at(1).rawHeader("SOAPACTION"),
             QByteArrayLiteral("\"urn:schemas-upnp-org:service:AVTransport:1#Play\""));
}

void SonosPlaybackPayloadTests::lineInSourceUsesDirectTransportUri()
{
    FakeNetworkAccessManager net;
    ZonePlayer               zone{&net, QStringLiteral("192.0.2.10"), QStringLiteral("RINCON_123")};
    zone.setDeviceServices({QStringLiteral("AudioIn"), QStringLiteral("ContentDirectory")});
    const QVariantMap item = zone.sourceItems().first().toMap();

    QCOMPARE(item.value(QStringLiteral("kind")).toString(), QStringLiteral("lineIn"));
    net.enqueueResponse({soapEnvelope(QByteArrayLiteral(R"xml(
<u:SetAVTransportURIResponse xmlns:u="urn:schemas-upnp-org:service:AVTransport:1"/>
)xml"))});
    net.enqueueResponse({soapEnvelope(QByteArrayLiteral(R"xml(
<u:PlayResponse xmlns:u="urn:schemas-upnp-org:service:AVTransport:1"/>
)xml"))});

    zone.playItem(item);
    net.completePendingReplies();
    net.completePendingReplies();

    QCOMPARE(net.requests().size(), 2);
    QCOMPARE(net.requests().at(0).rawHeader("SOAPACTION"),
             QByteArrayLiteral("\"urn:schemas-upnp-org:service:AVTransport:1#SetAVTransportURI\""));
    QVERIFY(net.requests().at(0).body.contains("<CurrentURI>x-rincon-stream:RINCON_123</CurrentURI>"));
    QCOMPARE(net.requests().at(1).rawHeader("SOAPACTION"),
             QByteArrayLiteral("\"urn:schemas-upnp-org:service:AVTransport:1#Play\""));
}

QTEST_APPLESS_MAIN(SonosPlaybackPayloadTests)

#include "sonos_playback_payload_tests.moc"
