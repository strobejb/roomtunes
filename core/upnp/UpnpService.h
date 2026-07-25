#pragma once

#include <QNetworkReply>
#include <QString>

namespace RoomTunes {

// Interface implemented by each thin per-UPnP-service wrapper (AVTransport,
// RenderingControl, etc). Replaces the original UpnpBase.
class UpnpService
{
public:
    virtual ~UpnpService() = default;

    virtual void setDevice(const QString &device, int port = 1400) = 0;

    virtual void setSid(const QString &sid) = 0;
    virtual const QString &sid() const = 0;
    virtual bool subscribed() const = 0;

    virtual QNetworkReply *subscribe(const QString &localAddr, int port, int timeoutSeconds = 3600) = 0;
    virtual QNetworkReply *unsubscribe() = 0;

    virtual const char *serviceName() const = 0;
};

}
