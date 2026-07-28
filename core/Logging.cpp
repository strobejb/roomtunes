#include "Logging.h"

#include <cstdio>

#include <QDateTime>
#include <QLocale>
#include <QNetworkAddressEntry>
#include <QNetworkInterface>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSysInfo>
#include <QtGlobal>

namespace RoomTunes {

Q_LOGGING_CATEGORY(logDiscovery, "roomtunes.core.discovery")
Q_LOGGING_CATEGORY(logZone, "roomtunes.core.zone")
Q_LOGGING_CATEGORY(logSoap, "roomtunes.core.soap")
Q_LOGGING_CATEGORY(logSmapi, "roomtunes.core.smapi")

namespace {

qint64 g_startMsecs = 0;
thread_local QString g_logEndpoint;
LogVerbosity g_logVerbosity = LogVerbosity::Normal;
constexpr qsizetype kCategoryWidth = 24;
constexpr qsizetype kEndpointWidth = 22;

QString directedEndpoint(const QString &address, LogDirection direction)
{
    if (address.isEmpty())
        return {};
    const QChar marker = direction == LogDirection::Outbound ? QLatin1Char('>') : QLatin1Char('<');
    return QString(marker) + address;
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
    if (text.startsWith(QLatin1Char('"'))) {
        qsizetype end = 1;
        bool escaped = false;
        for (; end < text.size(); ++end) {
            const QChar ch = text.at(end);
            if (escaped) {
                escaped = false;
            } else if (ch == QLatin1Char('\\')) {
                escaped = true;
            } else if (ch == QLatin1Char('"')) {
                break;
            }
        }

        if (end >= text.size())
            return {};

        field = text.mid(1, end - 1);
        text = text.mid(end + 1).trimmed();
    } else {
        const qsizetype end = text.indexOf(QLatin1Char(' '));
        if (end < 0) {
            field = text;
            text.clear();
        } else {
            field = text.left(end);
            text = text.mid(end + 1).trimmed();
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
    QString outputMessage = message;
    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - g_startMsecs;
    const qint64 seconds = elapsed / 1000;
    const qint64 millis = elapsed % 1000;

    QString bracket = QStringLiteral("%1.%2")
                           .arg(seconds, 3, 10, QLatin1Char('0'))
                           .arg(millis, 3, 10, QLatin1Char('0'));

    const bool isSoap = context.category && qstrcmp(context.category, "roomtunes.core.soap") == 0;
    const bool hasDirectedEndpoint = outputMessage.startsWith(QLatin1Char('>')) || outputMessage.startsWith(QLatin1Char('<'));

    // Directed network logs pass their endpoint as the first message field
    // or via ScopedLogEndpoint. Pull it out of the message and render it
    // after the fixed-width header as "[time|source] < peer message", so
    // scan-heavy logs keep both the message and endpoint columns aligned.
    const QString destination = !g_logEndpoint.isEmpty()
        ? g_logEndpoint
        : ((isSoap || hasDirectedEndpoint) ? takeFirstLogField(&outputMessage) : QString());

    if (context.category && qstrcmp(context.category, "default") != 0) {
        const QString category = QString::fromUtf8(context.category);
        bracket += QLatin1Char('|') + category.leftJustified(kCategoryWidth);
    }

    QString line = QStringLiteral("[%1]").arg(bracket);
    if (!destination.isEmpty()) {
        const QChar direction = destination.front();
        const QString endpoint = destination.mid(1);
        line += QStringLiteral(" %1 %2 %3")
                    .arg(direction)
                    .arg(endpoint.leftJustified(kEndpointWidth))
                    .arg(outputMessage);
    } else {
        line += QLatin1Char(' ') + outputMessage;
    }

    // Routing everything through stderr made Qt Creator's Application
    // Output color every single line red (it colors by stream, not by
    // message content) -- debug/info go to stdout, warnings and up stay on
    // stderr, so only genuine warnings actually stand out.
    FILE *stream = (type == QtDebugMsg || type == QtInfoMsg) ? stdout : stderr;
    std::fprintf(stream, "%s\n", line.toLocal8Bit().constData());
    std::fflush(stream);
}

}

ScopedLogEndpoint::ScopedLogEndpoint(QString address, LogDirection direction)
    : m_previousEndpoint(g_logEndpoint)
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
    body.replace(QRegularExpression(QStringLiteral("<((?:[A-Za-z_][\\w.-]*:)?(?:authToken|privateKey|sessionId|password|token|key))([^>]*)>.*?</\\1>"),
                                    QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral("<\\1\\2><redacted></\\1>"));
    body.replace(QRegularExpression(QStringLiteral("(\"?(?:authToken|privateKey|sessionId|password|token|key)\"?\\s*[:=]\\s*\")([^\"]*)(\")"),
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

    QStringList parts;
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString httpReason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
    const QByteArray method = reply->operation() == QNetworkAccessManager::GetOperation ? QByteArrayLiteral("GET")
        : reply->operation() == QNetworkAccessManager::PostOperation ? QByteArrayLiteral("POST")
        : reply->operation() == QNetworkAccessManager::PutOperation ? QByteArrayLiteral("PUT")
        : reply->operation() == QNetworkAccessManager::DeleteOperation ? QByteArrayLiteral("DELETE")
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
    const QString endpoint = reply ? reply->url().host() : QString();
    ScopedLogEndpoint scoped(endpoint, LogDirection::Inbound);

    QMessageLogger().warning(category).noquote()
        << QStringLiteral("HTTPERR:") << context << networkReplyDiagnosticText(reply);

    if (!body.isEmpty())
        QMessageLogger().warning(category).noquote()
            << QStringLiteral("HTTPXML:") << context
            << redactedNetworkBodyForLog(QString::fromUtf8(body), 0);
}

void installLogMessagePattern()
{
    g_logVerbosity = verbosityFromEnvironment();
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
