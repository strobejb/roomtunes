#pragma once

#include <memory>

#include <QImage>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QSvgRenderer>
#include <QUrl>

class QNetworkReply;

namespace RoomTunes
{

class RoundedImage : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(qreal radius READ radius WRITE setRadius NOTIFY radiusChanged)
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)

  public:
    enum Status
    {
        Null,
        Loading,
        Ready,
        Error
    };
    Q_ENUM(Status)

    explicit RoundedImage(QQuickItem *parent = nullptr);
    ~RoundedImage() override;

    QUrl source() const
    {
        return m_source;
    }

    void setSource(const QUrl &source);

    qreal radius() const
    {
        return m_radius;
    }

    void setRadius(qreal radius);

    Status status() const
    {
        return m_status;
    }

    void paint(QPainter *painter) override;

  signals:
    void sourceChanged();
    void radiusChanged();
    void statusChanged();

  private:
    void load();
    void cancelPendingReply();
    bool loadSvg(const QByteArray &data);
    bool loadSvgFile(const QString &path);
    void setStatus(Status status);
    void finishLoad(const QByteArray &data);

  private:
    QUrl                          m_source;
    qreal                         m_radius = 0;
    Status                        m_status = Null;
    QImage                        m_image;
    std::unique_ptr<QSvgRenderer> m_svg;
    QNetworkAccessManager         m_network;
    QPointer<QNetworkReply>       m_reply;
};

} // namespace RoomTunes
