#include "RoundedImage.h"

#include <algorithm>

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QQmlFile>

namespace RoomTunes {

RoundedImage::RoundedImage(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    setMipmap(true);
    setOpaquePainting(false);
}

void RoundedImage::setSource(const QUrl &source)
{
    if (m_source == source)
        return;

    m_source = source;
    emit sourceChanged();
    load();
}

void RoundedImage::setRadius(qreal radius)
{
    radius = std::max<qreal>(0, radius);
    if (qFuzzyCompare(m_radius, radius))
        return;

    m_radius = radius;
    emit radiusChanged();
    update();
}

void RoundedImage::paint(QPainter *painter)
{
    if (m_status != Ready || m_image.isNull() || width() <= 0 || height() <= 0)
        return;

    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QRectF target(0, 0, width(), height());
    QPainterPath clipPath;
    clipPath.addRoundedRect(target, m_radius, m_radius);
    painter->setClipPath(clipPath);

    const QSize imageSize = m_image.size();
    const qreal imageRatio = qreal(imageSize.width()) / qreal(imageSize.height());
    const qreal targetRatio = target.width() / target.height();

    QRectF sourceRect;
    if (imageRatio > targetRatio) {
        const qreal sourceWidth = imageSize.height() * targetRatio;
        sourceRect = QRectF((imageSize.width() - sourceWidth) / 2, 0, sourceWidth, imageSize.height());
    } else {
        const qreal sourceHeight = imageSize.width() / targetRatio;
        sourceRect = QRectF(0, (imageSize.height() - sourceHeight) / 2, imageSize.width(), sourceHeight);
    }

    painter->drawImage(target, m_image, sourceRect);
}

void RoundedImage::load()
{
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply.clear();
    }

    m_image = {};
    update();

    if (m_source.isEmpty()) {
        setStatus(Null);
        return;
    }

    if (m_source.isLocalFile() || m_source.scheme() == QLatin1String("qrc")) {
        const QString path = QQmlFile::urlToLocalFileOrQrc(m_source);
        QImage image(path);
        if (image.isNull()) {
            setStatus(Error);
            return;
        }
        m_image = image;
        setStatus(Ready);
        update();
        return;
    }

    setStatus(Loading);
    QNetworkReply *reply = m_network.get(QNetworkRequest(m_source));
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (m_reply != reply) {
            reply->deleteLater();
            return;
        }

        m_reply.clear();
        const QByteArray data = reply->readAll();
        const bool ok = reply->error() == QNetworkReply::NoError;
        reply->deleteLater();

        if (!ok) {
            setStatus(Error);
            return;
        }
        finishLoad(data);
    });
}

void RoundedImage::setStatus(Status status)
{
    if (m_status == status)
        return;

    m_status = status;
    emit statusChanged();
}

void RoundedImage::finishLoad(const QByteArray &data)
{
    QImage image;
    if (!image.loadFromData(data)) {
        setStatus(Error);
        return;
    }

    m_image = image;
    setStatus(Ready);
    update();
}

} // namespace RoomTunes
