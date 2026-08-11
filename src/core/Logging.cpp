#include "Logging.h"

#include <cstdio>

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QHash>
#include <QHostAddress>
#include <QLocale>
#include <QNetworkAddressEntry>
#include <QNetworkInterface>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <QSysInfo>
#include <QtGlobal>

namespace RoomTunes
{

Q_LOGGING_CATEGORY(logDiscovery, "roomtunes.core.discovery")
Q_LOGGING_CATEGORY(logZone, "roomtunes.core.zone")
Q_LOGGING_CATEGORY(logServices, "roomtunes.core.services")
Q_LOGGING_CATEGORY(logSoap, "roomtunes.core.soap")
Q_LOGGING_CATEGORY(logSmapi, "roomtunes.core.smapi")
Q_LOGGING_CATEGORY(logEventing, "roomtunes.core.eventing")

namespace
{

qint64               g_startMsecs = 0;
thread_local QString g_logEndpoint;
LogVerbosity         g_logVerbosity     = LogVerbosity::Normal;
constexpr qsizetype  kIpEndpointWidth   = 15;
constexpr qsizetype  kHostEndpointWidth = 22;
constexpr qsizetype  kHeaderWidth       = 38;

QString directedEndpoint(const QString &address, LogDirection direction)
{
    if (address.isEmpty())
        return {};
    const QChar marker = direction == LogDirection::Outbound ? QLatin1Char('>') : QLatin1Char('<');
    return QString(marker) + address;
}

qsizetype endpointWidth(const QString &endpoint)
{
    QHostAddress address(endpoint);
    if (!address.isNull())
        return kIpEndpointWidth;
    return kHostEndpointWidth;
}

QHash<QString, QStringList> defaultGatewaysByInterface()
{
    QHash<QString, QStringList> gateways;

#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    // Qt exposes interface addresses but not the default route/gateway.
    // Keep this as best-effort startup diagnostics only: unsupported
    // platforms simply omit gwN lines rather than affecting discovery.
    QString ipTool = QStandardPaths::findExecutable(QStringLiteral("ip"));
    if (ipTool.isEmpty())
    {
        const QStringList candidates = {
            QStringLiteral("/usr/sbin/ip"),
            QStringLiteral("/sbin/ip"),
            QStringLiteral("/usr/bin/ip"),
            QStringLiteral("/bin/ip"),
        };
        for (const QString &candidate : candidates)
        {
            if (QFile::exists(candidate))
            {
                ipTool = candidate;
                break;
            }
        }
    }

    if (ipTool.isEmpty())
        return gateways;

    QProcess process;
    process.start(ipTool, {QStringLiteral("-o"), QStringLiteral("-4"), QStringLiteral("route"), QStringLiteral("show"),
                           QStringLiteral("default")});
    if (!process.waitForFinished(1000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return gateways;

    const QString output = QString::fromUtf8(process.readAllStandardOutput());
    for (const QString &line : output.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
    {
        const QStringList fields   = line.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        const qsizetype   devIndex = fields.indexOf(QStringLiteral("dev"));
        const qsizetype   viaIndex = fields.indexOf(QStringLiteral("via"));
        if (devIndex < 0 || devIndex + 1 >= fields.size() || viaIndex < 0 || viaIndex + 1 >= fields.size())
            continue;

        gateways[fields.at(devIndex + 1)].append(fields.at(viaIndex + 1));
    }
#endif

    return gateways;
}

LogVerbosity verbosityFromEnvironment()
{
    const QString value = qEnvironmentVariable("ROOMTUNES_LOG").trimmed();
    return value.compare(QStringLiteral("verbose"), Qt::CaseInsensitive) == 0 ? LogVerbosity::Verbose
                                                                              : LogVerbosity::Normal;
}

QString takeFirstLogField(QString *message)
{
    QString text = message->trimmed();
    if (text.isEmpty())
        return {};

    QString field;
    if (text.startsWith(QLatin1Char('"')))
    {
        qsizetype end     = 1;
        bool      escaped = false;
        for (; end < text.size(); ++end)
        {
            const QChar ch = text.at(end);
            if (escaped)
            {
                escaped = false;
            }
            else if (ch == QLatin1Char('\\'))
            {
                escaped = true;
            }
            else if (ch == QLatin1Char('"'))
            {
                break;
            }
        }

        if (end >= text.size())
            return {};

        field = text.mid(1, end - 1);
        text  = text.mid(end + 1).trimmed();
    }
    else
    {
        const qsizetype end = text.indexOf(QLatin1Char(' '));
        if (end < 0)
        {
            field = text;
            text.clear();
        }
        else
        {
            field = text.left(end);
            text  = text.mid(end + 1).trimmed();
        }
    }

    if (field.isEmpty())
        return {};

    *message = text;
    return field;
}

// A custom handler (rather than qSetMessagePattern()) specifically because
// QT_MESSAGE_PATTERN, when set in the environment, silently overrides any
// pattern the app requests via qSetMessagePattern() -- and Qt Creator's own
// Run environment sets it, which is why that approach produced literally
// no visible change. Installing a handler bypasses the pattern system
// (and its environment override) entirely: once installed, Qt never
// applies its own formatting, so this is the only formatting logic run.
void logMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    QString      outputMessage = message;
    const qint64 elapsed       = QDateTime::currentMSecsSinceEpoch() - g_startMsecs;
    const qint64 seconds       = elapsed / 1000;
    const qint64 millis        = elapsed % 1000;

    QString bracket =
        QStringLiteral("%1.%2").arg(seconds, 3, 10, QLatin1Char('0')).arg(millis, 3, 10, QLatin1Char('0'));

    const bool isSoap = context.category && qstrcmp(context.category, "roomtunes.core.soap") == 0;
    const bool hasDirectedEndpoint =
        outputMessage.startsWith(QLatin1Char('>')) || outputMessage.startsWith(QLatin1Char('<'));

    // Directed network logs pass their endpoint as the first message field
    // or via ScopedLogEndpoint. Pull it out of the message and render it
    // after the compact "[time|source]" header. The rendered header is padded
    // after the closing bracket so the bracket text stays clean while scan-heavy
    // logs keep their message and endpoint columns aligned.
    QString destination;
    if (isSoap || hasDirectedEndpoint)
        destination = takeFirstLogField(&outputMessage);
    if (destination.isEmpty())
        destination = g_logEndpoint;

    if (context.category && qstrcmp(context.category, "default") != 0)
    {
        const QString category  = QString::fromUtf8(context.category);
        bracket                += QLatin1Char('|') + category;
    }

    const QString prefix = (QStringLiteral("[%1]").arg(bracket) + QLatin1Char(' ')).leftJustified(kHeaderWidth);
    QString       line   = prefix;
    if (!destination.isEmpty())
    {
        const QChar   direction  = destination.front();
        const QString endpoint   = destination.mid(1);
        line                    += QStringLiteral("%1 %2 %3")
                    .arg(direction)
                    .arg(endpoint.leftJustified(endpointWidth(endpoint)))
                    .arg(outputMessage);
    }
    else
    {
        line += outputMessage;
    }

    // Routing everything through stderr made Qt Creator's Application
    // Output color every single line red (it colors by stream, not by
    // message content) -- debug/info go to stdout, warnings and up stay on
    // stderr, so only genuine warnings actually stand out.
    FILE *stream = (type == QtDebugMsg || type == QtInfoMsg) ? stdout : stderr;
    std::fprintf(stream, "%s\n", line.toLocal8Bit().constData());
    std::fflush(stream);
}

} // namespace

ScopedLogEndpoint::ScopedLogEndpoint(QString address, LogDirection direction) : m_previousEndpoint(g_logEndpoint)
{
    g_logEndpoint = directedEndpoint(address, direction);
}

ScopedLogEndpoint::~ScopedLogEndpoint()
{
    g_logEndpoint = m_previousEndpoint;
}

void setLogVerbosity(LogVerbosity verbosity)
{
    g_logVerbosity = verbosity;
}

LogVerbosity logVerbosity()
{
    return g_logVerbosity;
}

bool verboseLoggingEnabled()
{
    return g_logVerbosity == LogVerbosity::Verbose;
}

QString redactedNetworkBodyForLog(QString body, qsizetype maxLength)
{
    body.replace(
        QRegularExpression(
            QStringLiteral(
                "<((?:[A-Za-z_][\\w.-]*:)?(?:authToken|privateKey|sessionId|password|token|key))([^>]*)>.*?</\\1>"),
            QRegularExpression::CaseInsensitiveOption),
        QStringLiteral("<\\1\\2><redacted></\\1>"));
    body.replace(
        QRegularExpression(
            QStringLiteral("(\"?(?:authToken|privateKey|sessionId|password|token|key)\"?\\s*[:=]\\s*\")([^\"]*)(\")"),
            QRegularExpression::CaseInsensitiveOption),
        QStringLiteral("\\1<redacted>\\3"));
    body.replace(QLatin1Char('\r'), QLatin1Char(' '));
    body.replace(QLatin1Char('\n'), QLatin1Char(' '));
    body.replace(QLatin1Char('\t'), QLatin1Char(' '));
    body = body.simplified();

    if (maxLength > 0 && body.size() > maxLength)
        body = body.left(maxLength - 3) + QStringLiteral("...");
    return body;
}

QString networkReplyDiagnosticText(const QNetworkReply *reply)
{
    if (!reply)
        return QStringLiteral("no QNetworkReply");

    QStringList      parts;
    const int        httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString    httpReason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
    const QByteArray method     = reply->operation() == QNetworkAccessManager::GetOperation ? QByteArrayLiteral("GET")
                                  : reply->operation() == QNetworkAccessManager::PostOperation ? QByteArrayLiteral("POST")
                                  : reply->operation() == QNetworkAccessManager::PutOperation ? QByteArrayLiteral("PUT")
                                  : reply->operation() == QNetworkAccessManager::DeleteOperation
                                      ? QByteArrayLiteral("DELETE")
                                  : reply->operation() == QNetworkAccessManager::CustomOperation
                                      ? reply->request().attribute(QNetworkRequest::CustomVerbAttribute).toByteArray()
                                      : QByteArrayLiteral("?");

    if (!method.isEmpty())
        parts << QStringLiteral("method=%1").arg(QString::fromLatin1(method));
    parts << QStringLiteral("url=%1").arg(reply->url().toString());
    parts << QStringLiteral("network=%1 %2").arg(int(reply->error())).arg(reply->errorString());
    parts << QStringLiteral("http=%1%2")
                 .arg(httpStatus)
                 .arg(httpReason.isEmpty() ? QString() : QStringLiteral(" ") + httpReason);

    return parts.join(QStringLiteral("; "));
}

void logNetworkReplyError(const QLoggingCategory &category, const QString &context, const QNetworkReply *reply,
                          const QByteArray &body)
{
    const QString     endpoint = reply ? reply->url().host() : QString();
    ScopedLogEndpoint scoped(endpoint, LogDirection::Inbound);

    QMessageLogger().warning(category).noquote()
        << QStringLiteral("HTTPERR:") << context << networkReplyDiagnosticText(reply);

    if (!body.isEmpty())
        QMessageLogger().warning(category).noquote()
            << QStringLiteral("HTTPXML:") << context << redactedNetworkBodyForLog(QString::fromUtf8(body), 0);
}

void installLogMessagePattern()
{
    g_logVerbosity = verbosityFromEnvironment();
    g_startMsecs   = QDateTime::currentMSecsSinceEpoch();
    qInstallMessageHandler(logMessageHandler);
}

void logStartupBanner(const QString &appName)
{
    const QString version = QCoreApplication::applicationVersion().isEmpty() ? QStringLiteral("(unknown)")
                                                                             : QCoreApplication::applicationVersion();
    const QString hostName =
        QSysInfo::machineHostName().isEmpty() ? QStringLiteral("(unknown)") : QSysInfo::machineHostName();

    qCDebug(logDiscovery).noquote() << QStringLiteral("%1: %2").arg(appName, version);
    qCDebug(logDiscovery).noquote() << QStringLiteral("  locale:       %1").arg(QLocale::system().name());
    qCDebug(logDiscovery).noquote() << QStringLiteral("  os:           %1").arg(QSysInfo::prettyProductName());
    qCDebug(logDiscovery).noquote()
        << QStringLiteral("  kernel:       %1 %2").arg(QSysInfo::kernelType(), QSysInfo::kernelVersion());
    qCDebug(logDiscovery).noquote() << QStringLiteral("  qt:           %1").arg(qVersion());
    qCDebug(logDiscovery).noquote() << QStringLiteral("  cpu:          %1").arg(QSysInfo::currentCpuArchitecture());
    qCDebug(logDiscovery).noquote() << QStringLiteral("  build cpu:    %1").arg(QSysInfo::buildCpuArchitecture());
    qCDebug(logDiscovery).noquote() << QStringLiteral("  host:         %1").arg(hostName);
    qCDebug(logDiscovery) << "--------------------------------------------------------------------------------";

    const QHash<QString, QStringList> gateways = defaultGatewaysByInterface();

    // Every interface Windows offers to bind against, not just the one
    // NetworkWatcher/Ssdp end up picking -- this exact dump would have
    // made the SSDP-bind-port saga earlier in this app's development
    // obvious in seconds instead of a long debugging session.
    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces())
    {
        const bool running = iface.flags().testFlag(QNetworkInterface::IsRunning);
        qCDebug(logDiscovery).noquote() << QStringLiteral("%1  [%2]")
                                               .arg(iface.humanReadableName(), -20)
                                               .arg(running ? QStringLiteral("connected") : QStringLiteral("down"));

        int ipIndex = 0;
        for (const QNetworkAddressEntry &entry : iface.addressEntries())
        {
            qCDebug(logDiscovery).noquote() << QStringLiteral("  ip%1: %2 / %3")
                                                   .arg(ipIndex++)
                                                   .arg(entry.ip().toString(), -28)
                                                   .arg(entry.netmask().toString());
        }

        int gatewayIndex = 0;
        for (const QString &gateway : gateways.value(iface.name()))
        {
            qCDebug(logDiscovery).noquote() << QStringLiteral("  gw%1: %2").arg(gatewayIndex++).arg(gateway);
        }
    }

    qCDebug(logDiscovery) << "--------------------------------------------------------------------------------";
}

} // namespace RoomTunes
