#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <qqml.h>
#include <QQuickStyle>
#include <QQuickWindow>

#include "Logging.h"
#include "RoundedImage.h"
#include "Settings.h"
#include "version.h"
#include "chrome/PlatformChrome.h"
#include "zone/BrowseHistoryStore.h"
#include "chrome/WindowsChrome.h"
#include "zone/GroupedZoneModel.h"
#include "zone/Household.h"
#include "zone/MusicServiceListModel.h"
#include "zone/QueueModel.h"
#include "zone/RecentlyPlayedModel.h"

using namespace RoomTunes;

int main(int argc, char *argv[])
{
    installLogMessagePattern();

    QGuiApplication app(argc, argv);
    app.setApplicationDisplayName(QStringLiteral("Room Tunes"));
    app.setApplicationVersion(QStringLiteral(PRODUCT_VERSION_STRING));
#ifndef Q_OS_WIN
    app.setWindowIcon(QIcon(QStringLiteral(":/qt/qml/RoomTunes/resources/icons/app.png")));
#endif
    logStartupBanner(QStringLiteral("RoomTunes"));
    configureApplicationSettings();

    // Without an explicit style, Qt Quick Controls auto-selects a native
    // one on each platform (e.g. "Windows" here) -- native styles draw
    // controls like ScrollBar/ComboBox with their own OS-native chrome and
    // *ignore* custom background/contentItem overrides entirely (confirmed
    // directly: SlimScrollBar.qml's restyling had zero visible effect,
    // still showing a native track+arrows scrollbar). "Basic" is the one
    // QQC2 style that's a plain QML reference implementation respecting
    // every control's own background/contentItem, which this app relies on
    // throughout (ScrollBar here, but also Button/TextField/BusyIndicator/
    // Popup wherever they're used directly rather than via one of this
    // app's own hand-built components).
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    qmlRegisterType<RoundedImage>("RoomTunes", 1, 0, "RoundedImage");

    Household household;
    BrowseHistoryStore browseHistory;
    GroupedZoneModel groupsModel(&household);
    MusicServiceListModel musicServiceModel(&household, &browseHistory);
    RecentlyPlayedModel recentlyPlayedModel(&household);
    household.startDiscovery();

    PlatformChrome platformChrome;
    QueueModel queueModel;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("household"), &household);
    engine.rootContext()->setContextProperty(QStringLiteral("browseHistory"), &browseHistory);
    engine.rootContext()->setContextProperty(QStringLiteral("groupsModel"), &groupsModel);
    engine.rootContext()->setContextProperty(QStringLiteral("musicServiceModel"), &musicServiceModel);
    engine.rootContext()->setContextProperty(QStringLiteral("recentlyPlayedModel"), &recentlyPlayedModel);
    engine.rootContext()->setContextProperty(QStringLiteral("PlatformChrome"), &platformChrome);
    engine.rootContext()->setContextProperty(QStringLiteral("queueModel"), &queueModel);
    engine.rootContext()->setContextProperty(QStringLiteral("appVersion"),
                                             QStringLiteral(PRODUCT_VERSION_STRING));

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.loadFromModule("RoomTunes", "Main");

    // Main.qml starts with visible: false so the WM_NCCALCSIZE filter is
    // attached before the window's first paint -- otherwise the native
    // title bar flashes on screen for one frame before collapsing.
    const auto rootObjects = engine.rootObjects();
    if (!rootObjects.isEmpty()) {
        if (auto *window = qobject_cast<QQuickWindow *>(rootObjects.first())) {
#ifdef Q_OS_WIN
            installWindowsChrome(window);
#endif
            window->setVisible(true);
        }
    }

    return app.exec();
}
