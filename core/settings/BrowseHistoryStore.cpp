#include "BrowseHistoryStore.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>

#include "Settings.h"

namespace RoomTunes {

namespace {

QString browseHistoryFilePath()
{
    return QDir(configDirectoryPath()).filePath(QStringLiteral("browse-history.json"));
}

}

BrowseHistoryStore::BrowseHistoryStore(QObject *parent)
    : QObject(parent)
{
    load();
}

void BrowseHistoryStore::recordUse(const QString &key)
{
    if (key.isEmpty())
        return;

    m_scores.insert(key, now());
    save();
}

void BrowseHistoryStore::refresh()
{
    ++m_revision;
    emit changed();
}

qint64 BrowseHistoryStore::score(const QString &key) const
{
    if (key.isEmpty())
        return 0;

    return m_scores.value(key, 0);
}

void BrowseHistoryStore::load()
{
    QFile file(browseHistoryFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return;

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it)
        m_scores.insert(it.key(), qint64(it.value().toDouble()));
}

void BrowseHistoryStore::save() const
{
    QJsonObject root;
    for (auto it = m_scores.constBegin(); it != m_scores.constEnd(); ++it)
        root.insert(it.key(), double(it.value()));

    QFile file(browseHistoryFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

qint64 BrowseHistoryStore::now() const
{
    return QDateTime::currentMSecsSinceEpoch();
}

}
