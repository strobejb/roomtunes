#pragma once

#include <QLoggingCategory>
#include <QString>

namespace RoomTunes {

Q_DECLARE_LOGGING_CATEGORY(logDiscovery)
Q_DECLARE_LOGGING_CATEGORY(logZone)
Q_DECLARE_LOGGING_CATEGORY(logSoap)
Q_DECLARE_LOGGING_CATEGORY(logSmapi)

// Call once, as early as possible in main() (before constructing
// QGuiApplication/QCoreApplication is fine -- qInstallMessageHandler()
// doesn't need one). Installs a custom message handler (deliberately not
// qSetMessagePattern(): QT_MESSAGE_PATTERN in the environment silently
// overrides that, and Qt Creator's own Run environment sets it) that
// prefixes every log line with elapsed time since process start. Directed
// network logs pass ">dest" or "<source" as the first message field; the
// handler moves that into the prefix so lines use
// "[SSS.mmm|category|>dest] Method(...)" or
// "[SSS.mmm|category|<source] NOTIFY ...", while ordinary logs stay
// "[SSS.mmm|category] message".
void installLogMessagePattern();

// Call once, right after installLogMessagePattern(). Matches
// roomtunes-bb10's own startup dump (app name/locale, then every network
// interface with its addresses) -- genuinely useful diagnostic parity: the
// SSDP/discovery bind issues chased earlier in this app's development
// would have been obvious immediately against a log showing every
// interface Windows actually offered to bind against.
void logStartupBanner(const QString &appName);

}

// QLOG()/QWARN() read like the original bb10 app's ZLOG() -- each .cpp
// picks its own category once, right after including this header, instead
// of repeating qCDebug(logWhatever)/qCWarning(logWhatever) at every call
// site:
//
//   #include "../Logging.h"
//   #define QLOG_CATEGORY logDiscovery
//
// every QLOG()/QWARN() in that file then logs under the right category
// automatically. A file that uses QLOG()/QWARN() without defining
// QLOG_CATEGORY first fails to compile at the first call site, naming the
// missing macro -- not silent/wrong-category output.
#define QLOG() qCDebug(QLOG_CATEGORY)
#define QWARN() qCWarning(QLOG_CATEGORY)
