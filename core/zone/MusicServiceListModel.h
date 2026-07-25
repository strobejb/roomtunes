#pragma once

#include <QAbstractListModel>

namespace RoomTunes {

class Household;

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
    };

    explicit MusicServiceListModel(Household *household, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

private slots:
    void rebuild();

private:
    Household *m_household;
};

}
