#include "PlatformChrome.h"

#ifndef Q_OS_WIN
#include <QProcess>
#include <QProcessEnvironment>
#endif

namespace
{

#ifndef Q_OS_WIN
// Returns a layout string like ":minimize,maximize,close" or "close:" from
// GNOME's own setting, so the custom title bar matches native windows.
// Ported from roomtunes-bb10's sibling project qexed
// (src/HexEdit/chrome/titlebar.cpp, platformButtonLayout()).
QString platformButtonLayout()
{
    const QString desktop =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("XDG_CURRENT_DESKTOP")).toLower();
    if (desktop.contains(QStringLiteral("gnome")) || desktop.contains(QStringLiteral("unity")))
    {
        QProcess proc;
        proc.start(QStringLiteral("gsettings"),
                   {QStringLiteral("get"), QStringLiteral("org.gnome.desktop.wm.preferences"),
                    QStringLiteral("button-layout")});
        if (proc.waitForFinished(500) && proc.exitCode() == 0)
        {
            QString s = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
            s.remove(QLatin1Char('\'')); // gsettings wraps string values in single-quotes
            if (!s.isEmpty())
                return s;
        }
    }
    return QStringLiteral(":minimize,maximize,close"); // universal fallback
}

void parseButtonLayout(const QString &layout, QStringList &left, QStringList &right)
{
    auto split = [](const QString &s) {
        return s.split(QLatin1Char(','), Qt::SkipEmptyParts);
    };
    const int colon = layout.indexOf(QLatin1Char(':'));
    if (colon < 0)
    {
        right = split(layout);
    }
    else
    {
        left  = split(layout.left(colon));
        right = split(layout.mid(colon + 1));
    }
}
#endif

} // namespace

PlatformChrome::PlatformChrome(QObject *parent) : QObject(parent)
{
#ifndef Q_OS_WIN
    parseButtonLayout(platformButtonLayout(), m_leftButtons, m_rightButtons);
#else
    m_rightButtons = {QStringLiteral("minimize"), QStringLiteral("maximize"), QStringLiteral("close")};
#endif
}

bool PlatformChrome::isWindows() const
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

bool PlatformChrome::isKde() const
{
#ifdef Q_OS_WIN
    return false;
#else
    static const bool kde = qgetenv("XDG_CURRENT_DESKTOP").toUpper().contains("KDE");
    return kde;
#endif
}

bool PlatformChrome::isGnome() const
{
#ifdef Q_OS_WIN
    return false;
#else
    static const bool gnome = [] {
        const QByteArray desktop = qgetenv("XDG_CURRENT_DESKTOP").toUpper();
        return desktop.contains("GNOME") || desktop.contains("UNITY");
    }();
    return gnome;
#endif
}
