#include "control/SonosZoneControl.h"
#include "control/services/AVTransport.h"
#include "control/services/ContentDirectory.h"
#include "control/services/Queue.h"
#include "control/services/RenderingControl.h"
#include "support/FakeNetworkAccessManager.h"

#include <QTest>

using namespace RoomTunes;
using namespace RoomTunes::Tests;

class SonosZoneControlTests : public QObject
{
    Q_OBJECT

  private slots:
    void getVolumeSendsRenderingControlRequestAndParsesResponse();
    void setMutedSendsDesiredMute();
    void getTransportInfoParsesTransportState();
    void browseParsesDidlItems();
};

namespace
{

constexpr auto kSpeakerIp = "192.0.2.10";

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

QByteArray escapedXml(QString xml)
{
    return xml.replace(QLatin1Char('&'), QStringLiteral("&amp;"))
        .replace(QLatin1Char('<'), QStringLiteral("&lt;"))
        .replace(QLatin1Char('>'), QStringLiteral("&gt;"))
        .toUtf8();
}

class ControlHarness
{
  public:
    FakeNetworkAccessManager net;
    AVTransport             avTransport{&net, QString::fromLatin1(kSpeakerIp)};
    RenderingControl        renderingControl{&net, QString::fromLatin1(kSpeakerIp)};
    ContentDirectory        contentDirectory{&net, QString::fromLatin1(kSpeakerIp)};
    Queue                   queue{&net, QString::fromLatin1(kSpeakerIp)};
    SonosZoneControl        control{avTransport, renderingControl, contentDirectory, queue,
                                    []() { return QStringLiteral("Kitchen"); }};
};

} // namespace

void SonosZoneControlTests::getVolumeSendsRenderingControlRequestAndParsesResponse()
{
    ControlHarness harness;
    harness.net.enqueueResponse({soapEnvelope(QByteArrayLiteral(R"xml(
<u:GetVolumeResponse xmlns:u="urn:schemas-upnp-org:service:RenderingControl:1">
  <CurrentVolume>27</CurrentVolume>
</u:GetVolumeResponse>
)xml"))});

    bool called = false;
    bool ok     = false;
    int  level  = 0;

    harness.control.getVolume(this, [&](bool callbackOk, int callbackLevel) {
        called = true;
        ok     = callbackOk;
        level  = callbackLevel;
    });

    harness.net.completePendingReplies();

    QTRY_VERIFY(called);
    QVERIFY(ok);
    QCOMPARE(level, 27);
    QCOMPARE(harness.net.requests().size(), 1);

    const auto &request = harness.net.lastRequest();
    QCOMPARE(request.requestOperation, QNetworkAccessManager::PostOperation);
    QCOMPARE(request.url.path(), QStringLiteral("/MediaRenderer/RenderingControl/Control"));
    QCOMPARE(request.rawHeader("SOAPACTION"),
             QByteArrayLiteral("\"urn:schemas-upnp-org:service:RenderingControl:1#GetVolume\""));
    QVERIFY(request.body.contains("<u:GetVolume"));
    QVERIFY(request.body.contains("<InstanceID>0</InstanceID>"));
    QVERIFY(request.body.contains("<Channel>Master</Channel>"));
}

void SonosZoneControlTests::setMutedSendsDesiredMute()
{
    ControlHarness harness;
    harness.net.enqueueResponse({soapEnvelope(QByteArrayLiteral(R"xml(
<u:SetMuteResponse xmlns:u="urn:schemas-upnp-org:service:RenderingControl:1"/>
)xml"))});

    bool called = false;
    bool ok     = false;

    harness.control.setMuted(this, true, [&](bool callbackOk) {
        called = true;
        ok     = callbackOk;
    });

    harness.net.completePendingReplies();

    QTRY_VERIFY(called);
    QVERIFY(ok);

    const auto &request = harness.net.lastRequest();
    QCOMPARE(request.url.path(), QStringLiteral("/MediaRenderer/RenderingControl/Control"));
    QCOMPARE(request.rawHeader("SOAPACTION"),
             QByteArrayLiteral("\"urn:schemas-upnp-org:service:RenderingControl:1#SetMute\""));
    QVERIFY(request.body.contains("<DesiredMute>1</DesiredMute>"));
}

void SonosZoneControlTests::getTransportInfoParsesTransportState()
{
    ControlHarness harness;
    harness.net.enqueueResponse({soapEnvelope(QByteArrayLiteral(R"xml(
<u:GetTransportInfoResponse xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">
  <CurrentTransportState>PLAYING</CurrentTransportState>
</u:GetTransportInfoResponse>
)xml"))});

    bool    called = false;
    bool    ok     = false;
    QString state;

    harness.control.getTransportInfo(this, [&](bool callbackOk, const QString &callbackState) {
        called = true;
        ok     = callbackOk;
        state  = callbackState;
    });

    harness.net.completePendingReplies();

    QTRY_VERIFY(called);
    QVERIFY(ok);
    QCOMPARE(state, QStringLiteral("PLAYING"));

    const auto &request = harness.net.lastRequest();
    QCOMPARE(request.url.path(), QStringLiteral("/MediaRenderer/AVTransport/Control"));
    QCOMPARE(request.rawHeader("SOAPACTION"),
             QByteArrayLiteral("\"urn:schemas-upnp-org:service:AVTransport:1#GetTransportInfo\""));
}

void SonosZoneControlTests::browseParsesDidlItems()
{
    ControlHarness harness;
    const QByteArray didl = escapedXml(QStringLiteral(R"xml(
<DIDL-Lite xmlns="urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/"
           xmlns:dc="http://purl.org/dc/elements/1.1/"
           xmlns:upnp="urn:schemas-upnp-org:metadata-1-0/upnp/">
  <item id="track-1" parentID="SQ:0">
    <dc:title>Track One</dc:title>
    <dc:creator>Artist</dc:creator>
    <upnp:album>Album</upnp:album>
    <upnp:class>object.item.audioItem.musicTrack</upnp:class>
    <res protocolInfo="x-rincon:*:*:*">x-rincon:RINCON_1</res>
  </item>
</DIDL-Lite>
)xml"));

    harness.net.enqueueResponse({soapEnvelope(QByteArrayLiteral(R"xml(
<u:BrowseResponse xmlns:u="urn:schemas-upnp-org:service:ContentDirectory:1">
  <Result>)xml") + didl + QByteArrayLiteral(R"xml(</Result>
  <NumberReturned>1</NumberReturned>
  <TotalMatches>1</TotalMatches>
  <UpdateID>44</UpdateID>
</u:BrowseResponse>
)xml"))});

    bool                      called = false;
    bool                      ok     = false;
    SonosZoneControl::BrowseResult result;

    harness.control.browseDetailed(this, QStringLiteral("SQ:0"), [&](bool callbackOk, const auto &callbackResult) {
        called = true;
        ok     = callbackOk;
        result = callbackResult;
    }, 0, 100, QStringLiteral("BrowseDirectChildren"));

    harness.net.completePendingReplies();

    QTRY_VERIFY(called);
    QVERIFY(ok);
    QCOMPARE(result.items.size(), 1);
    QCOMPARE(result.items.first().id, QStringLiteral("track-1"));
    QCOMPARE(result.items.first().title, QStringLiteral("Track One"));
    QCOMPARE(result.updateId, 44);
    QVERIFY(result.updateIdKnown);

    const auto &request = harness.net.lastRequest();
    QCOMPARE(request.url.path(), QStringLiteral("/MediaServer/ContentDirectory/Control"));
    QCOMPARE(request.rawHeader("SOAPACTION"),
             QByteArrayLiteral("\"urn:schemas-upnp-org:service:ContentDirectory:1#Browse\""));
    QVERIFY(request.body.contains("<ObjectID>SQ:0</ObjectID>"));
    QVERIFY(request.body.contains("<RequestedCount>100</RequestedCount>"));
}

QTEST_APPLESS_MAIN(SonosZoneControlTests)

#include "sonos_zone_control_tests.moc"
