#pragma once

#include "Soap.h"
#include "UpnpService.h"

namespace RoomTunes
{

// Shared boilerplate for the thin per-UPnP-service wrappers (device/event
// URL construction, GENA subscribe/unsubscribe, SID tracking). Each concrete
// service (AVTransport, RenderingControl, ...) only needs to add its own
// action methods, matching the original generated Sonos_*.hpp files without
// repeating their identical constructor/subscribe/unsubscribe boilerplate
// six times over.
class UpnpServiceBase : public UpnpService
{
  public:
    UpnpServiceBase(QNetworkAccessManager *netMgr, const QString &device, int port, QString controlPath,
                    QString eventPath, QString serviceUrn, const char *name)
        : m_netMgr(netMgr), m_controlPath(std::move(controlPath)), m_eventPath(std::move(eventPath)),
          m_action(std::move(serviceUrn)), m_name(name)
    {
        UpnpServiceBase::setDevice(device, port);
    }

    void setDevice(const QString &device, int port = 1400) override
    {
        m_soapUrl      = QStringLiteral("http://%1:%2%3").arg(device).arg(port).arg(m_controlPath);
        m_subscribeUrl = QStringLiteral("http://%1:%2%3").arg(device).arg(port).arg(m_eventPath);
    }

    void setSid(const QString &sid) override
    {
        m_sid = sid;
    }

    const QString &sid() const override
    {
        return m_sid;
    }

    bool subscribed() const override
    {
        return !m_sid.isEmpty();
    }

    const char *serviceName() const override
    {
        return m_name;
    }

    QNetworkReply *subscribe(const QString &localAddr, int port, int timeoutSeconds = 3600) override
    {
        SoapRequest request(m_netMgr, QString(), QString());

        if (!m_sid.isEmpty())
            return request.resubscribe(m_subscribeUrl, m_sid, timeoutSeconds);

        const QString callback =
            QStringLiteral("<http://%1:%2/%3/notify>").arg(localAddr).arg(port).arg(QLatin1String(m_name));
        return request.subscribe(m_subscribeUrl, callback, timeoutSeconds);
    }

    QNetworkReply *unsubscribe() override
    {
        SoapRequest    request(m_netMgr, QString(), QString());
        QNetworkReply *reply = request.unsubscribe(m_subscribeUrl, m_sid);
        m_sid.clear();
        return reply;
    }

  protected:
    QNetworkAccessManager *m_netMgr;
    QString                m_action;
    QString                m_soapUrl;

  private:
    QString     m_controlPath;
    QString     m_eventPath;
    const char *m_name;
    QString     m_subscribeUrl;
    QString     m_sid;
};

} // namespace RoomTunes
