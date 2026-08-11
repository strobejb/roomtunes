#pragma once

#include <QObject>
#include <QStringList>

// Exposed to QML as the "PlatformChrome" context property. Tells the custom
// title bar which platform it's on and, on GNOME, what window-control
// button layout the user has configured -- so the custom bar matches
// whatever native windows already look like on that desktop.
class PlatformChrome : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool isWindows READ isWindows CONSTANT)
    Q_PROPERTY(bool isKde READ isKde CONSTANT)
    Q_PROPERTY(bool isGnome READ isGnome CONSTANT)
    Q_PROPERTY(QStringList leftButtons READ leftButtons CONSTANT)
    Q_PROPERTY(QStringList rightButtons READ rightButtons CONSTANT)

  public:
    explicit PlatformChrome(QObject *parent = nullptr);

    bool isWindows() const;
    bool isKde() const;
    bool isGnome() const;

    QStringList leftButtons() const
    {
        return m_leftButtons;
    }

    QStringList rightButtons() const
    {
        return m_rightButtons;
    }

  private:
    QStringList m_leftButtons;
    QStringList m_rightButtons;
};
