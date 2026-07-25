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
