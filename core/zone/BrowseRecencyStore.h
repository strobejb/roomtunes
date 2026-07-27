#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>

namespace RoomTunes {

// App-owned browse ordering state. Today this is persisted local recency, but
// callers go through this single object so a future Sonos/network-provided
// ordering source can be folded in here without rewriting BrowseHome/models.
class BrowseRecencyStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    explicit BrowseRecencyStore(QObject *parent = nullptr);

    int revision() const { return m_revision; }
    Q_INVOKABLE void recordUse(const QString &key);
    Q_INVOKABLE qint64 score(const QString &key) const;
    Q_INVOKABLE void refresh();

signals:
    void changed();

private:
    QString settingKey(const QString &key) const;
    qint64 now() const;

    int m_revision = 0;
};

}
