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
    void radioNowPlayingUsesCurrentUriStationMetadata();
};

namespace
{

QByteArray soapEnvelope(const QByteArray &body)
{
    return QByteArrayLiteral(R"xml(
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/">
  <s:Body>
)xml") + body +
           QByteArrayLiteral(R"xml(
  </s:Body>
</s:Envelope>
)xml");
}

QByteArray escapedXml(QString xml)
{
    return xml.replace(QLatin1Char('&'), QStringLiteral("&amp;"))
        .replace(QLatin1Char('<'), QStringLiteral("&lt;"))
        .replace(QLatin1Char('>'), QStringLiteral("&gt;"))
        .toUtf8();
}

} // namespace

void SonosPlaybackPayloadTests::streamItemsAreNotQueueable_data()
{
    QTest::addColumn<QString>("uri");
    QTest::addColumn<QString>("upnpClass");

    QTest::newRow("audio-broadcast-class") << QStringLiteral("x-sonosapi-stream:s25419?sid=254&flags=0")
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

    QVERIFY(metadata.contains(
        "<res protocolInfo=\"x-file-cifs:*:audio/mpeg:*\">x-file-cifs://server/share/track.mp3</res>"));
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

void SonosPlaybackPayloadTests::radioNowPlayingUsesCurrentUriStationMetadata()
{
    FakeNetworkAccessManager net;
    ZonePlayer               zone{&net, QStringLiteral("192.0.2.10"), QStringLiteral("RINCON_123")};

    const QString uri      = QStringLiteral("x-sonosapi-hls:stations%7eplayable%7e%7ebbc_radio_fourfm%7e%7eurn%3abbc%"
                                                 "3aradio%3anetwork%3abbc_radio_four?sid=325&flags=8488&sn=23");
    QString       innerUri = uri;
    innerUri.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    const QByteArray trackMetadata   = escapedXml(QStringLiteral(R"xml(
<DIDL-Lite xmlns:dc="http://purl.org/dc/elements/1.1/"
           xmlns:upnp="urn:schemas-upnp-org:metadata-1-0/upnp/"
           xmlns:r="urn:schemas-rinconnetworks-com:metadata-1-0/"
           xmlns="urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/">
  <item id="-1" parentID="-1" restricted="true">
    <res protocolInfo="sonos.com-http:*:application/x-mpegURL:*">%1</res>
    <r:streamContent>BR P|TYPE=SNG|TITLE Above and Below: The Battle to Save Britain&apos;s Prehistoric Art|ARTIST Artworks 16:00 - 16:30|ALBUM </r:streamContent>
    <upnp:albumArtURI>https://ichef.bbci.co.uk/images/ic/640x640/p0p0p9hb.jpg</upnp:albumArtURI>
    <upnp:class>object.item.audioItem.musicTrack</upnp:class>
  </item>
</DIDL-Lite>
)xml")
                                                      .arg(innerUri));
    const QByteArray stationMetadata = escapedXml(QStringLiteral(R"xml(
<DIDL-Lite xmlns:dc="http://purl.org/dc/elements/1.1/"
           xmlns:upnp="urn:schemas-upnp-org:metadata-1-0/upnp/"
           xmlns:r="urn:schemas-rinconnetworks-com:metadata-1-0/"
           xmlns="urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/">
  <item id="-1" parentID="-1" restricted="true">
    <dc:title>Radio 4</dc:title>
    <upnp:class>object.item.audioItem.audioBroadcast</upnp:class>
    <desc id="cdudn" nameSpace="urn:schemas-rinconnetworks-com:metadata-1-0/">SA_RINCON83207_X_#Svc83207-0-Token</desc>
    <upnp:albumArtURI>https://sounds.files.bbci.co.uk/3.12.0/networks/bbc_radio_four/colour_450x450.png</upnp:albumArtURI>
  </item>
</DIDL-Lite>
)xml"));

    net.enqueueResponse({soapEnvelope(QByteArrayLiteral(R"xml(
<u:GetTransportInfoResponse xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">
  <CurrentTransportState>PLAYING</CurrentTransportState>
</u:GetTransportInfoResponse>
)xml"))});
    net.enqueueResponse({soapEnvelope(QByteArrayLiteral(R"xml(
<u:GetTransportSettingsResponse xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">
  <CurrentPlayMode>NORMAL</CurrentPlayMode>
</u:GetTransportSettingsResponse>
)xml"))});
    net.enqueueResponse({soapEnvelope(QByteArrayLiteral(R"xml(
<u:GetCrossfadeModeResponse xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">
  <CurrentCrossfadeMode>0</CurrentCrossfadeMode>
</u:GetCrossfadeModeResponse>
)xml"))});
    net.enqueueResponse({soapEnvelope(QByteArrayLiteral(R"xml(
<u:GetPositionInfoResponse xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">
  <Track>1</Track>
  <TrackDuration>0:00:00</TrackDuration>
  <TrackMetaData>)xml") + trackMetadata +
                                      QByteArrayLiteral(R"xml(</TrackMetaData>
  <TrackURI>)xml") + uri.toUtf8().replace("&", "&amp;") +
                                      QByteArrayLiteral(R"xml(</TrackURI>
  <RelTime>0:10:14</RelTime>
</u:GetPositionInfoResponse>
)xml"))});
    net.enqueueResponse({soapEnvelope(QByteArrayLiteral(R"xml(
<u:GetMediaInfoResponse xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">
  <CurrentURI>)xml") + uri.toUtf8().replace("&", "&amp;") +
                                      QByteArrayLiteral(R"xml(</CurrentURI>
  <CurrentURIMetaData>)xml") + stationMetadata +
                                      QByteArrayLiteral(R"xml(</CurrentURIMetaData>
</u:GetMediaInfoResponse>
)xml"))});

    zone.refreshTransportState();
    net.completePendingReplies();
    net.completePendingReplies();

    QVERIFY(zone.currentTrack());
    QCOMPARE(zone.currentTrack()->title(),
             QStringLiteral("Above and Below: The Battle to Save Britain's Prehistoric Art"));
    QCOMPARE(zone.currentTrack()->album(), QStringLiteral("Radio 4"));
    QCOMPARE(zone.currentTrack()->imageUrl(),
             QStringLiteral("https://ichef.bbci.co.uk/images/ic/640x640/p0p0p9hb.jpg"));
    QCOMPARE(zone.currentTrack()->stationImageUrl(),
             QStringLiteral("https://sounds.files.bbci.co.uk/3.12.0/networks/bbc_radio_four/colour_450x450.png"));
}

QTEST_APPLESS_MAIN(SonosPlaybackPayloadTests)

#include "sonos_playback_payload_tests.moc"
