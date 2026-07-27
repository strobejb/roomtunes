#include "MusicService.h"

namespace RoomTunes {

MusicService::MusicService(QString serviceKey, QString title, QString iconSource, QObject *parent)
    : QObject(parent)
    , m_serviceKey(std::move(serviceKey))
    , m_title(std::move(title))
    , m_iconSource(std::move(iconSource))
{
}

void MusicService::browse(const QString &requestToken, const QString &objectId)
{
    doBrowse(objectId, [this, requestToken](bool ok, const QString &errorMessage, const QVariantList &items) {
        emit browseFinished(requestToken, ok, errorMessage, items);
    });
}

void MusicService::browseItem(const QString &requestToken, const QVariantMap &item)
{
    doBrowseItem(item, [this, requestToken](bool ok, const QString &errorMessage, const QVariantList &items) {
        emit browseFinished(requestToken, ok, errorMessage, items);
    });
}

void MusicService::browseDirect(const QString &objectId, ResultCallback callback)
{
    doBrowse(objectId, std::move(callback));
}

void MusicService::search(const QString &requestToken, const QString &category, const QString &term)
{
    doSearch(category, term, [this, requestToken](bool ok, const QString &errorMessage, const QVariantList &items) {
        emit browseFinished(requestToken, ok, errorMessage, items);
    });
}

void MusicService::doSearch(const QString &, const QString &, ResultCallback callback)
{
    callback(false, tr("Search isn't supported for this service."), {});
}

void MusicService::doBrowseItem(const QVariantMap &item, ResultCallback callback)
{
    QString objectId = item.value(QStringLiteral("browseId")).toString();
    if (objectId.isEmpty())
        objectId = item.value(QStringLiteral("id")).toString();
    doBrowse(objectId, std::move(callback));
}

void MusicService::setTitle(const QString &title)
{
    if (m_title != title) {
        m_title = title;
        emit titleChanged();
    }
}

void MusicService::setIconSource(const QString &iconSource)
{
    if (m_iconSource != iconSource) {
        m_iconSource = iconSource;
        emit iconSourceChanged();
    }
}

}
