#pragma once

#include <QAbstractListModel>
#include <QList>

namespace RoomTunes {

class Household;
class BrowseHistoryStore;
class MusicService;

// One row per browsable music service on this household -- the Sonos
// Music Library plus every SMAPI service actually configured/logged-in --
// see Household::services(). TitleRole/ImageUrlRole are read straight off
// the MusicService object, kept as roles purely for QML delegate
// convenience; ServiceRole is the object itself, for anything that needs
// to call browse()/search() on it (see qml/BrowseStack.qml).
class MusicServiceListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        ServiceRole = Qt::UserRole + 1,
        TitleRole,
        ImageUrlRole,
        ServiceKeyRole,
        ServiceIdRole,
        ItemRole,
    };

    explicit MusicServiceListModel(Household *household, BrowseHistoryStore *browseHistoryStore, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

private slots:
    void rebuild();

private:
    MusicService *serviceAt(int row) const;
    QList<MusicService *> orderedServices() const;

    Household *m_household;
    BrowseHistoryStore *m_browseHistoryStore;
    QList<MusicService *> m_services;
};

}
