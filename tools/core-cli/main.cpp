#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QHash>
#include <QSet>
#include <QTextStream>
#include <QTimer>
#include <QVariant>

#include <QNetworkAccessManager>

#include "Logging.h"
#include "control/SoapResponse.h"
#include "control/services/MusicServices.h"
#include "services/MusicService.h"
#include "services/SmapiService.h"
#include "services/SpotifyServiceConfig.h"
#include "settings/Settings.h"
#include "zone/Household.h"
#include "zone/ZonePlayer.h"

using namespace RoomTunes;

namespace
{

QString playStateName(PlayState state)
{
    switch (state)
    {
    case PlayState::Playing:
        return QStringLiteral("playing");
    case PlayState::Paused:
        return QStringLiteral("paused");
    case PlayState::Transitioning:
        return QStringLiteral("transitioning");
    case PlayState::Stopped:
    default:
        return QStringLiteral("stopped");
    }
}

void printZone(ZonePlayer *zone)
{
    QTextStream out(stdout);
    out << "  " << zone->roomName() << " (" << zone->udn() << ") " << (zone->isCoordinator() ? "[coordinator] " : "")
        << (zone->invisible() ? "[invisible] " : "") << "coordinatorUdn=" << zone->coordinatorUdn()
        << " vol=" << zone->volume() << " state=" << playStateName(zone->playState()) << Qt::endl;
}

} // namespace

int main(int argc, char *argv[])
{
    installLogMessagePattern();
    logStartupBanner(QStringLiteral("core-cli"));

    QCoreApplication app(argc, argv);
    configureApplicationSettings();

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("RoomTunes backend engine smoke test: Sonos discovery + Spotify SMAPI DeviceLink"));
    parser.addHelpOption();

    QCommandLineOption spotifyUrlOption(
        QStringLiteral("spotify-url"),
        QStringLiteral("Spotify SMAPI endpoint URL (see core/services/SpotifyServiceConfig.h for why this must be "
                       "supplied rather than guessed)"),
        QStringLiteral("url"));
    parser.addOption(spotifyUrlOption);

    QCommandLineOption spotifyLinkOption(
        QStringLiteral("spotify-link"),
        QStringLiteral("Run the Spotify DeviceLink auth flow (requires --spotify-url)"));
    parser.addOption(spotifyLinkOption);

    QCommandLineOption browseOption(
        QStringLiteral("browse"),
        QStringLiteral("Once music services are found (the Sonos Music Library plus any linked SMAPI accounts), "
                       "browse() each one's root folder and print the results"));
    parser.addOption(browseOption);

    QCommandLineOption browseIdOption(
        QStringLiteral("browse-id"),
        QStringLiteral("Once the Sonos Music Library service is available, browse() this exact ContentDirectory "
                       "object id through it and print the results -- for probing an arbitrary/undocumented "
                       "container id (e.g. a candidate 'recently played' id) without going through the GUI."),
        QStringLiteral("objectId"));
    parser.addOption(browseIdOption);

    QCommandLineOption listServicesAtOption(
        QStringLiteral("list-services-at"),
        QStringLiteral("Skip SSDP discovery entirely (so it doesn't fight over the receive port with another "
                       "running instance) and call MusicServices.ListAvailableServices() directly against this "
                       "zone's IP, printing the raw HTTP status/fault/body -- a focused repro for a "
                       "'server replied: Internal Server Error' report against one specific zone."),
        QStringLiteral("ip"));
    parser.addOption(listServicesAtOption);

    parser.process(app);

    QTextStream out(stdout);

    if (parser.isSet(listServicesAtOption))
    {
        auto *netMgr        = new QNetworkAccessManager(&app);
        auto *musicServices = new MusicServices(netMgr, parser.value(listServicesAtOption));

        QNetworkReply *reply = musicServices->ListAvailableServices();
        QObject::connect(reply, &QNetworkReply::finished, &app, [reply]() {
            SoapResponse response(reply);
            reply->deleteLater();

            QTextStream out(stdout);
            out << "HTTP status: " << response.httpStatusCode() << Qt::endl;
            out << "error(): " << (response.error() ? "true" : "false") << Qt::endl;
            if (response.hasFault())
            {
                out << "SOAP fault: code=" << response.faultCode() << " string=" << response.faultString()
                    << " upnpErrorCode=" << response.upnpErrorCode()
                    << " upnpErrorDescription=" << response.upnpErrorDescription() << Qt::endl;
            }
            else
            {
                out << "faultString(): " << response.faultString() << Qt::endl;
            }
            out << "raw body (first 2000 bytes):" << Qt::endl << response.rawBody().left(2000) << Qt::endl;

            QCoreApplication::quit();
        });

        return app.exec();
    }

    out << "RoomTunes core-cli -- starting Sonos discovery..." << Qt::endl;

    Household household;

    QObject::connect(&household, &Household::zoneReady, &app, [](ZonePlayer *zone) {
        QTextStream out(stdout);
        out << "zone ready:" << Qt::endl;
        printZone(zone);
    });

    QObject::connect(&household, &Household::discoveryTimedOut, &app, [&household]() {
        QTextStream               out(stdout);
        const QList<ZonePlayer *> zones = household.zones();
        out << Qt::endl << "discovery finished, " << zones.size() << " zone(s) found:" << Qt::endl;
        for (ZonePlayer *zone : zones)
            printZone(zone);

        if (zones.isEmpty())
            out << "(no Sonos zones responded -- make sure this machine is on the same network as your Sonos "
                   "system)"
                << Qt::endl;
    });

    if (!household.startDiscovery())
    {
        out << "failed to start SSDP discovery (could not bind/join the multicast group)" << Qt::endl;
        return 1;
    }

    if (parser.isSet(spotifyLinkOption))
    {
        if (!parser.isSet(spotifyUrlOption))
        {
            out << "--spotify-link requires --spotify-url <endpoint>" << Qt::endl;
            return 1;
        }

        SpotifyServiceConfig config;
        config.serviceUrl = parser.value(spotifyUrlOption);

        auto *spotify = new SmapiService(&household, 0, 0, config.serviceUrl, QStringLiteral("DeviceLink"), QString(),
                                         QString(), QString(), config.title, config.imageSource, 0, QString(), &app);

        QObject::connect(
            spotify, &SmapiService::deviceLinkCodeReady, &app,
            [](const QString &linkCode, const QString &regUrl, bool showLinkCode) {
                QTextStream out(stdout);
                out << "Visit " << regUrl;
                if (showLinkCode)
                    out << " and enter link code: " << linkCode;
                out << Qt::endl;
                out << "(exchanging the code for a token once you've authorized is the next step -- not yet "
                       "wired up in this tool)"
                    << Qt::endl;
            });

        // DeviceLink needs a household id, which only becomes available once
        // at least one zone has responded -- wait for the first ready zone.
        QObject::connect(&household, &Household::zoneReady, &app, [spotify](ZonePlayer *) {
            static bool started = false;
            if (started)
                return;
            started = true;

            spotify->beginSignIn();
        });
    }

    if (parser.isSet(browseOption))
    {
        // rebuildMusicServices() fires musicServicesChanged once per async
        // piece that completes (catalog fetch, icon fetch, ...), so the
        // list grows across several emissions -- track which serviceKeys
        // have already had a browse listener attached rather than
        // latching onto just the first (likely incomplete) emission.
        auto *browsedKeys = new QSet<QString>();
        QObject::connect(&household, &Household::musicServicesChanged, &app, [&household, &app, browsedKeys]() {
            QTextStream out(stdout);
            for (MusicService *service : household.services())
            {
                if (browsedKeys->contains(service->serviceKey()))
                    continue;
                browsedKeys->insert(service->serviceKey());

                QObject::connect(
                    service, &MusicService::browseFinished, &app,
                    [service](const QString &, bool ok, const QString &errorMessage, const QVariantList &items) {
                        QTextStream out(stdout);
                        out << "[" << service->title() << "] ";
                        if (!ok)
                        {
                            out << "browse FAILED: " << errorMessage << Qt::endl;
                            return;
                        }
                        out << "browse OK, " << items.size() << " item(s):" << Qt::endl;
                        for (const QVariant &v : items)
                        {
                            const QVariantMap m = v.toMap();
                            out << "    " << (m.value(QStringLiteral("container")).toBool() ? "[folder] " : "[track]  ")
                                << m.value(QStringLiteral("title")).toString();
                            const QString artist = m.value(QStringLiteral("artist")).toString();
                            if (!artist.isEmpty())
                                out << "  --  " << artist;
                            out << Qt::endl;
                        }
                    });

                out << "Browsing \"" << service->title() << "\" (key=" << service->serviceKey()
                    << ", needsSignIn=" << (service->needsSignIn() ? "true" : "false") << ")..." << Qt::endl;
                service->browse(service->serviceKey(), QStringLiteral("root"));
            }
        });
    }

    if (parser.isSet(browseIdOption))
    {
        const QString objectId = parser.value(browseIdOption);
        auto         *probed   = new bool(false);
        QObject::connect(&household, &Household::musicServicesChanged, &app, [&household, &app, objectId, probed]() {
            if (*probed)
                return;
            MusicService *library = household.libraryService();
            if (!library)
                return;
            *probed = true;

            QObject::connect(
                library, &MusicService::browseFinished, &app,
                [objectId](const QString &, bool ok, const QString &errorMessage, const QVariantList &items) {
                    QTextStream out(stdout);
                    out << "browse-id \"" << objectId << "\": ";
                    if (!ok)
                    {
                        out << "FAILED: " << errorMessage << Qt::endl;
                        return;
                    }
                    out << "OK, " << items.size() << " item(s):" << Qt::endl;
                    for (const QVariant &v : items)
                    {
                        const QVariantMap m = v.toMap();
                        out << "    id=" << m.value(QStringLiteral("id")).toString()
                            << (m.value(QStringLiteral("container")).toBool() ? " [folder] " : " [track]  ")
                            << m.value(QStringLiteral("title")).toString();
                        const QString artist = m.value(QStringLiteral("artist")).toString();
                        if (!artist.isEmpty())
                            out << "  --  " << artist;
                        out << Qt::endl;
                    }
                });

            QTextStream out(stdout);
            out << "Browsing library objectId \"" << objectId << "\"..." << Qt::endl;
            library->browse(QStringLiteral("browse-id"), objectId);
        });
    }

    // Give discovery + topology a reasonable window, then exit for
    // non-interactive runs (e.g. CI). --browse/--browse-id need longer:
    // musicServices arrive over GENA only after topology, so the browse()
    // call may still be in flight when the plain-discovery timeout
    // would've fired.
    QTimer::singleShot(parser.isSet(browseOption) || parser.isSet(browseIdOption) ? 30000 : 15000, &app,
                       &QCoreApplication::quit);

    return app.exec();
}
