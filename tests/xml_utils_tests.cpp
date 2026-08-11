#include "control/Didl.h"
#include "control/SoapResponse.h"
#include "services/MusicServiceCatalog.h"
#include "services/ServiceLogoCatalog.h"
#include "xml/XmlUtils.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTest>
#include <QUrl>

#include <algorithm>
#include <cstring>
#include <utility>

using namespace RoomTunes;

class FakeReply : public QNetworkReply
{
    Q_OBJECT

  public:
    explicit FakeReply(QByteArray body, QObject *parent = nullptr) : QNetworkReply(parent), m_body(std::move(body))
    {
        setRequest(QNetworkRequest(QUrl(QStringLiteral("http://speaker.example/MediaRenderer/Control"))));
        setUrl(request().url());
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
    }

    void abort() override
    {
    }

    qint64 bytesAvailable() const override
    {
        return m_body.size() - m_offset + QNetworkReply::bytesAvailable();
    }

  protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        const qint64 remaining = m_body.size() - m_offset;
        const qint64 count     = std::min(maxSize, remaining);
        if (count <= 0)
            return -1;

        memcpy(data, m_body.constData() + m_offset, size_t(count));
        m_offset += count;
        return count;
    }

  private:
    QByteArray m_body;
    qint64     m_offset = 0;
};

class XmlUtilsTests : public QObject
{
    Q_OBJECT

  private slots:
    void wrapperReadsDeviceDescriptionShape();
    void wrapperReadsZoneGroupStateShape();
    void wrapperReadsRootWildcardAndNamespacedAttributes();
    void wrapperCanRememberNameSensitivity();
    void wrapperReadsFirstDescendantText();
    void wrapperReadsBooleanText();
    void didlParsesNamespacedItems();
    void didlAppliesFavoriteResourceMetadata();
    void catalogParsesServiceDescriptors();
    void catalogParsesNestedServiceLogos();
    void soapResponseReadsValues();
    void soapResponseReadsBooleanValues();
    void soapResponseReadsFaultDetails();
};

void XmlUtilsTests::wrapperReadsDeviceDescriptionShape()
{
    const QByteArray xml = R"xml(
<root>
  <device>
    <roomName>Kitchen</roomName>
    <displayName>Sonos One</displayName>
    <feature1>AIRPLAY</feature1>
    <serviceList>
      <service><serviceType>urn:schemas-upnp-org:service:ZoneGroupTopology:1</serviceType></service>
    </serviceList>
    <deviceList>
      <device>
        <serviceList>
          <service><serviceType>urn:schemas-upnp-org:service:RenderingControl:1</serviceType></service>
        </serviceList>
      </device>
    </deviceList>
  </device>
</root>
)xml";

    const XmlDoc  doc    = XmlDoc::parse(xml);
    const XmlNode device = doc.first("//device");

    QVERIFY(doc);
    QVERIFY(device);
    QCOMPARE(device.text("roomName"), QStringLiteral("Kitchen"));
    QCOMPARE(device.text("displayName"), QStringLiteral("Sonos One"));
    QCOMPARE(device.child("feature1").text(), QStringLiteral("AIRPLAY"));
    QCOMPARE(device.all(".//serviceList/service").size(), 2);
}

void XmlUtilsTests::wrapperReadsZoneGroupStateShape()
{
    const QByteArray xml = R"xml(
<ZoneGroups>
  <ZoneGroup Coordinator="RINCON_MAIN">
    <ZoneGroupMember UUID="RINCON_MAIN" Location="http://192.0.2.10:1400/xml/device_description.xml"
                     ZoneName="Living Room" Invisible="0" SoftwareVersion="80.1-00000">
      <Satellite UUID="RINCON_SUB" Location="http://192.0.2.11:1400/xml/device_description.xml"
                 ZoneName="Sub"/>
    </ZoneGroupMember>
  </ZoneGroup>
</ZoneGroups>
)xml";

    const XmlDoc  doc    = XmlDoc::parse(xml);
    const XmlNode group  = doc.first("//ZoneGroup");
    const XmlNode member = group.child("ZoneGroupMember");
    const XmlNode sat    = member.child("Satellite");

    QVERIFY(doc);
    QCOMPARE(group.attr("Coordinator"), QStringLiteral("RINCON_MAIN"));
    QCOMPARE(member.attr("UUID"), QStringLiteral("RINCON_MAIN"));
    QCOMPARE(member.attrBool01("Invisible", true), false);
    QCOMPARE(member.attr("SoftwareVersion"), QStringLiteral("80.1-00000"));
    QCOMPARE(sat.attr("UUID"), QStringLiteral("RINCON_SUB"));
}

void XmlUtilsTests::wrapperReadsRootWildcardAndNamespacedAttributes()
{
    const QByteArray xml = R"xml(
<getMetadataResponse xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:nil="true">
  <mediaCollection>
    <id>artists</id>
  </mediaCollection>
</getMetadataResponse>
)xml";

    const XmlDoc           doc      = XmlDoc::parse(xml);
    const XmlNode          response = doc.first("//getMetadataResponse");
    const QVector<XmlNode> allNodes = doc.all("//*");

    QVERIFY(doc);
    QVERIFY(response);
    QCOMPARE(response.attr("nil"), QStringLiteral("true"));
    QCOMPARE(allNodes.first().name(), QStringLiteral("getMetadataResponse"));
    QCOMPARE(allNodes.at(1).name(), QStringLiteral("mediaCollection"));
    QCOMPARE(allNodes.at(2).name(), QStringLiteral("id"));
}

void XmlUtilsTests::wrapperCanRememberNameSensitivity()
{
    const QByteArray xml = R"xml(
<DeviceLink>
  <RegUrl>https://example.test/link</RegUrl>
  <Token>abc</Token>
</DeviceLink>
)xml";

    const XmlDoc  strictDoc  = XmlDoc::parse(xml);
    const XmlDoc  relaxedDoc = XmlDoc::parse(xml, XmlOptions{Qt::CaseInsensitive});
    const XmlNode strictRoot = strictDoc.first("//deviceLink");
    const XmlNode relaxed    = relaxedDoc.first("//deviceLink");

    QVERIFY(strictDoc);
    QVERIFY(!strictRoot);
    QVERIFY(relaxed);
    QCOMPARE(relaxed.text("regUrl"), QStringLiteral("https://example.test/link"));
    QVERIFY(relaxed.first("token").nameIs("token"));
    QVERIFY(relaxed.first("token").nameIs("Token", Qt::CaseSensitive));
}

void XmlUtilsTests::wrapperReadsFirstDescendantText()
{
    const QByteArray xml = R"xml(
<Envelope>
  <Body>
    <getDeviceAuthTokenResponse>
      <credentials>
        <Token>abc</Token>
        <PrivateKey>xyz</PrivateKey>
      </credentials>
    </getDeviceAuthTokenResponse>
  </Body>
</Envelope>
)xml";

    const XmlDoc  doc       = XmlDoc::parse(xml, XmlOptions{Qt::CaseInsensitive});
    const QString tokenName = QStringLiteral("token");

    QVERIFY(doc);
    QCOMPARE(doc.firstText({"AuthToken", "token"}), QStringLiteral("abc"));
    QCOMPARE(doc.firstText(tokenName), QStringLiteral("abc"));
    QCOMPARE(doc.firstText({"Key", "PrivateKey"}), QStringLiteral("xyz"));
}

void XmlUtilsTests::wrapperReadsBooleanText()
{
    const QByteArray xml = R"xml(
<deviceLink>
  <showLinkCode>false</showLinkCode>
  <enabled>1</enabled>
</deviceLink>
)xml";

    const XmlNode node = XmlDoc::parse(xml).root();

    QCOMPARE(node.textBool("showLinkCode", true), false);
    QCOMPARE(node.textBool("enabled"), true);
    QCOMPARE(node.textBool("missing", true), true);
}

void XmlUtilsTests::didlParsesNamespacedItems()
{
    const QByteArray xml = R"xml(
<DIDL-Lite xmlns="urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/"
           xmlns:dc="http://purl.org/dc/elements/1.1/"
           xmlns:upnp="urn:schemas-upnp-org:metadata-1-0/upnp/"
           xmlns:r="urn:schemas-rinconnetworks-com:metadata-1-0/">
  <item id="track-1" parentID="album-1">
    <dc:title>Track One</dc:title>
    <dc:creator>Artist</dc:creator>
    <upnp:album>Album</upnp:album>
    <upnp:class>object.item.audioItem.musicTrack</upnp:class>
    <upnp:albumArtURI>/art.jpg</upnp:albumArtURI>
    <r:streamInfo>RINCON_STREAM</r:streamInfo>
    <upnp:originalTrackNumber>7</upnp:originalTrackNumber>
    <res protocolInfo="x-rincon:*:*:*">x-rincon:RINCON_1</res>
    <desc>RINCON_AssociatedZPUDN</desc>
  </item>
  <container id="folder-1" parentID="root">
    <dc:title>Folder</dc:title>
    <upnp:class>object.container.album.musicAlbum</upnp:class>
  </container>
</DIDL-Lite>
)xml";

    const QList<DidlItem> items = Didl::parseItems(xml);

    QCOMPARE(items.size(), 2);
    QCOMPARE(items.first().id, QStringLiteral("track-1"));
    QCOMPARE(items.first().title, QStringLiteral("Track One"));
    QCOMPARE(items.first().artist, QStringLiteral("Artist"));
    QCOMPARE(items.first().album, QStringLiteral("Album"));
    QCOMPARE(items.first().albumArtUri, QStringLiteral("/art.jpg"));
    QCOMPARE(items.first().streamInfo, QStringLiteral("RINCON_STREAM"));
    QCOMPARE(items.first().trackNumber, QStringLiteral("7"));
    QCOMPARE(items.first().protocolInfo, QStringLiteral("x-rincon:*:*:*"));
    QCOMPARE(items.first().res, QStringLiteral("x-rincon:RINCON_1"));
    QVERIFY(!items.first().container);
    QCOMPARE(items.at(1).id, QStringLiteral("folder-1"));
    QVERIFY(items.at(1).container);
}

void XmlUtilsTests::didlAppliesFavoriteResourceMetadata()
{
    const QByteArray xml = R"xml(
<DIDL-Lite xmlns="urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/"
           xmlns:dc="http://purl.org/dc/elements/1.1/"
           xmlns:upnp="urn:schemas-upnp-org:metadata-1-0/upnp/"
           xmlns:r="urn:schemas-rinconnetworks-com:metadata-1-0/">
  <item id="FV:2/233" parentID="FV:2">
    <dc:title>Favourite</dc:title>
    <upnp:class>object.item.sonos-favorite</upnp:class>
    <r:resMD>&lt;DIDL-Lite xmlns=&quot;urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/&quot; xmlns:dc=&quot;http://purl.org/dc/elements/1.1/&quot; xmlns:upnp=&quot;urn:schemas-upnp-org:metadata-1-0/upnp/&quot;&gt;&lt;item id=&quot;10032020spotify%3Aplaylist%3Aabc&quot; parentID=&quot;spotify%3Aroot&quot;&gt;&lt;dc:title&gt;Inner Playlist&lt;/dc:title&gt;&lt;upnp:class&gt;object.container.playlistContainer&lt;/upnp:class&gt;&lt;desc&gt;SA_RINCON2311_X_user&lt;/desc&gt;&lt;/item&gt;&lt;/DIDL-Lite&gt;</r:resMD>
  </item>
</DIDL-Lite>
)xml";

    const QList<DidlItem> items = Didl::parseItems(xml);

    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().id, QStringLiteral("FV:2/233"));
    QCOMPARE(items.first().didlId, QStringLiteral("spotify:playlist:abc"));
    QCOMPARE(items.first().didlParentId, QStringLiteral("spotify:root"));
    QCOMPARE(items.first().desc, QStringLiteral("SA_RINCON2311_X_user"));
    QCOMPARE(items.first().serviceId, 2311);
    QCOMPARE(items.first().upnpClass, QStringLiteral("object.container.playlistContainer"));
    QVERIFY(items.first().container);
}

void XmlUtilsTests::catalogParsesServiceDescriptors()
{
    const QByteArray xml = R"xml(
<Services>
  <Service Id="10" Name="Example Music" Uri="http://example.test/smapi"
           SecureUri="https://example.test/smapi" ContainerType="MService" Capabilities="65537">
    <Policy Auth="DeviceLink" PollInterval="60"/>
    <Manifest Uri="https://example.test/manifest.json"/>
    <Presentation>
      <Strings Version="3"/>
      <PresentationMap Version="7"/>
    </Presentation>
  </Service>
</Services>
)xml";

    const QHash<int, SmapiCatalogEntry> catalog = MusicServiceCatalog::buildSmapiMap(xml, QStringLiteral("2567"));
    const SmapiCatalogEntry             entry   = catalog.value(2567);

    QCOMPARE(catalog.size(), 1);
    QCOMPARE(entry.smapiId, 10);
    QCOMPARE(entry.title, QStringLiteral("Example Music"));
    QCOMPARE(entry.uri, QStringLiteral("http://example.test/smapi"));
    QCOMPARE(entry.secureUri, QStringLiteral("https://example.test/smapi"));
    QCOMPARE(entry.auth, QStringLiteral("DeviceLink"));
    QCOMPARE(entry.pollInterval, QStringLiteral("60"));
    QCOMPARE(entry.containerType, QStringLiteral("MService"));
    QCOMPARE(entry.capabilities, QStringLiteral("65537"));
    QCOMPARE(entry.manifestUri, QStringLiteral("https://example.test/manifest.json"));
}

void XmlUtilsTests::catalogParsesNestedServiceLogos()
{
    const QByteArray xml = R"xml(
<logos>
  <service id="10">
    <images>
      <sized>
        <image placement="square:x-small">small.png</image>
        <image placement="square:x-large"> large.png </image>
      </sized>
    </images>
  </service>
</logos>
)xml";

    const QHash<int, QString> icons = ServiceLogoCatalog::parse(xml);

    QCOMPARE(icons.value(10), QStringLiteral("large.png"));
}

void XmlUtilsTests::soapResponseReadsValues()
{
    FakeReply reply(R"xml(
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/">
  <s:Body>
    <u:GetVolumeResponse xmlns:u="urn:schemas-upnp-org:service:RenderingControl:1">
      <CurrentVolume>37</CurrentVolume>
      <TrackMetaData>&lt;DIDL-Lite&gt;&lt;item/&gt;&lt;/DIDL-Lite&gt;</TrackMetaData>
    </u:GetVolumeResponse>
  </s:Body>
</s:Envelope>
)xml");

    SoapResponse response(&reply);

    QVERIFY(!response.error());
    QCOMPARE(response.value(QStringLiteral("CurrentVolume")), QStringLiteral("37"));
    QCOMPARE(response.value(QStringLiteral("TrackMetaData")), QStringLiteral("<DIDL-Lite><item/></DIDL-Lite>"));
}

void XmlUtilsTests::soapResponseReadsBooleanValues()
{
    FakeReply reply(R"xml(
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/">
  <s:Body>
    <u:GetDeviceLinkCodeResponse xmlns:u="http://www.sonos.com/Services/1.1">
      <ShowLinkCode>false</ShowLinkCode>
      <Enabled>1</Enabled>
    </u:GetDeviceLinkCodeResponse>
  </s:Body>
</s:Envelope>
)xml");

    SoapResponse response(&reply);

    QVERIFY(!response.error());
    QCOMPARE(response.boolValue("ShowLinkCode", true), false);
    QCOMPARE(response.boolValue("Enabled"), true);
    QCOMPARE(response.boolValue("Missing", true), true);
}

void XmlUtilsTests::soapResponseReadsFaultDetails()
{
    FakeReply reply(R"xml(
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/">
  <s:Body>
    <s:Fault>
      <faultcode>s:Client</faultcode>
      <faultstring>UPnPError</faultstring>
      <detail>
        <UPnPError xmlns="urn:schemas-upnp-org:control-1-0">
          <errorCode>701</errorCode>
          <errorDescription>Transition not available</errorDescription>
        </UPnPError>
        <refreshAuthTokenResult>
          <authToken>new-token</authToken>
          <privateKey>new-key</privateKey>
        </refreshAuthTokenResult>
      </detail>
    </s:Fault>
  </s:Body>
</s:Envelope>
)xml");

    SoapResponse response(&reply);

    QVERIFY(response.error());
    QVERIFY(response.hasFault());
    QCOMPARE(response.faultCode(), QStringLiteral("Client"));
    QCOMPARE(response.faultString(), QStringLiteral("UPnPError"));
    QCOMPARE(response.upnpErrorCode(), QStringLiteral("701"));
    QCOMPARE(response.upnpErrorDescription(), QStringLiteral("Transition not available"));
    QCOMPARE(response.refreshedAuthToken(), QStringLiteral("new-token"));
    QCOMPARE(response.refreshedPrivateKey(), QStringLiteral("new-key"));
}

QTEST_APPLESS_MAIN(XmlUtilsTests)

#include "xml_utils_tests.moc"
