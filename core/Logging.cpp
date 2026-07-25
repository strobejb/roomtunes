#include "Logging.h"

#include <cstdio>

#include <QDateTime>
#include <QLocale>
#include <QNetworkAddressEntry>
#include <QNetworkInterface>
#include <QSysInfo>
#include <QtGlobal>

namespace RoomTunes {

Q_LOGGING_CATEGORY(logDiscovery, "roomtunes.core.discovery")
Q_LOGGING_CATEGORY(logZone, "roomtunes.core.zone")
Q_LOGGING_CATEGORY(logSoap, "roomtunes.core.soap")
Q_LOGGING_CATEGORY(logSmapi, "roomtunes.core.smapi")

namespace {

qint64 g_startMsecs = 0;

// A custom handler (rather than qSetMessagePattern()) specifically because
// QT_MESSAGE_PATTERN, when set in the environment, silently overrides any
// pattern the app requests via qSetMessagePattern() -- and Qt Creator's own
// Run environment sets it, which is why that approach produced literally
// no visible change. Installing a handler bypasses the pattern system
// (and its environment override) entirely: once installed, Qt never
// applies its own formatting, so this is the only formatting logic run.
void logMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - g_startMsecs;
    const qint64 seconds = elapsed / 1000;
    const qint64 millis = elapsed % 1000;

    QString bracket = QStringLiteral("%1.%2")
                           .arg(seconds, 3, 10, QLatin1Char('0'))
                           .arg(millis, 3, 10, QLatin1Char('0'));

    // The category goes in the bracket alongside the timestamp, matching
    // roomtunes-bb10's "[SSS.mmm|IP]" shape with the category standing in
    // for IP -- there's no single field to pull a per-request zone/host
    // address into here the same way: that's just plain text at the start
    // of the message body already (see SoapRequest::send() in
    // upnp/Soap.h), not something this handler has separate access to.
    if (context.category && qstrcmp(context.category, "default") != 0)
        bracket += QLatin1Char('|') + QString::fromUtf8(context.category);

    const QString line = QStringLiteral("[%1] %2").arg(bracket, message);

    // Routing everything through stderr made Qt Creator's Application
    // Output color every single line red (it colors by stream, not by
    // message content) -- debug/info go to stdout, warnings and up stay on
    // stderr, so only genuine warnings actually stand out.
    FILE *stream = (type == QtDebugMsg || type == QtInfoMsg) ? stdout : stderr;
    std::fprintf(stream, "%s\n", line.toLocal8Bit().constData());
    std::fflush(stream);
}

}

void installLogMessagePattern()
{
    g_startMsecs = QDateTime::currentMSecsSinceEpoch();
    qInstallMessageHandler(logMessageHandler);
}

void logStartupBanner(const QString &appName)
{
    qCDebug(logDiscovery) << appName << "-- Qt" << qVersion() << "on" << QSysInfo::prettyProductName();
    qCDebug(logDiscovery) << "  locale:" << QLocale::system().name();
    qCDebug(logDiscovery) << "--------------------------------------------------------------------------------";

    // Every interface Windows offers to bind against, not just the one
    // NetworkWatcher/Ssdp end up picking -- this exact dump would have
    // made the SSDP-bind-port saga earlier in this app's development
    // obvious in seconds instead of a long debugging session.
    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        const bool running = iface.flags().testFlag(QNetworkInterface::IsRunning);
        qCDebug(logDiscovery).noquote() << QStringLiteral("%1  [%2]")
                                                .arg(iface.humanReadableName(), -20)
                                                .arg(running ? QStringLiteral("connected") : QStringLiteral("down"));

        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            qCDebug(logDiscovery).noquote() << QStringLiteral("  >> %1 / %2")
                                                    .arg(entry.ip().toString(), -28)
                                                    .arg(entry.netmask().toString());
        }
    }

    qCDebug(logDiscovery) << "--------------------------------------------------------------------------------";
}

}
