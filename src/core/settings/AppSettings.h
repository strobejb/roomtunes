#pragma once

#include <QObject>
#include <QString>

namespace RoomTunes
{

class AppSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString lastSelectedZoneUdn READ lastSelectedZoneUdn WRITE setLastSelectedZoneUdn NOTIFY
                   lastSelectedZoneUdnChanged)

  public:
    explicit AppSettings(QObject *parent = nullptr);

    QString lastSelectedZoneUdn() const;
    void    setLastSelectedZoneUdn(const QString &udn);

  signals:
    void lastSelectedZoneUdnChanged();
};

} // namespace RoomTunes
