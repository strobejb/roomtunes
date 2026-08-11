#pragma once

#include <QByteArray>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QQueue>
#include <QUrl>

#include <algorithm>
#include <cstring>
#include <utility>

namespace RoomTunes::Tests
{

class FakeNetworkReply : public QNetworkReply
{
    Q_OBJECT

  public:
    struct Response
    {
        QByteArray body;
        int        httpStatus = 200;
        NetworkError error    = NoError;
        QString    errorText;
        QList<QPair<QByteArray, QByteArray>> rawHeaders;
    };

    FakeNetworkReply(const QNetworkRequest &request, QNetworkAccessManager::Operation operation, Response response,
                     QObject *parent = nullptr)
        : QNetworkReply(parent), m_body(std::move(response.body))
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(operation);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response.httpStatus);
        for (const auto &header : response.rawHeaders)
            setRawHeader(header.first, header.second);
        if (response.error != NoError)
            setError(response.error, response.errorText);

        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }

    void abort() override
    {
    }

    void finish()
    {
        if (!m_body.isEmpty())
            emit readyRead();
        setFinished(true);
        emit finished();
    }

    qint64 bytesAvailable() const override
    {
        return m_body.size() - m_offset + QNetworkReply::bytesAvailable();
    }

  protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        const qint64 remaining = m_body.size() - m_offset;
        const qint64 count     = std::min(maxSize, remaining);
        if (count <= 0)
            return -1;

        memcpy(data, m_body.constData() + m_offset, size_t(count));
        m_offset += count;
        return count;
    }

  private:
    QByteArray m_body;
    qint64     m_offset = 0;
};

class FakeNetworkAccessManager : public QNetworkAccessManager
{
    Q_OBJECT

  public:
    struct RecordedRequest
    {
        Operation requestOperation = UnknownOperation;
        QUrl      url;
        QList<QPair<QByteArray, QByteArray>> rawHeaders;
        QByteArray body;

        QByteArray rawHeader(const QByteArray &name) const
        {
            for (const auto &header : rawHeaders)
            {
                if (header.first.compare(name, Qt::CaseInsensitive) == 0)
                    return header.second;
            }
            return {};
        }
    };

    explicit FakeNetworkAccessManager(QObject *parent = nullptr) : QNetworkAccessManager(parent)
    {
    }

    void enqueueResponse(FakeNetworkReply::Response response)
    {
        m_responses.enqueue(std::move(response));
    }

    const QList<RecordedRequest> &requests() const
    {
        return m_requests;
    }

    const RecordedRequest &lastRequest() const
    {
        return m_requests.last();
    }

    QNetworkReply *lastReply() const
    {
        return m_lastReply;
    }

    void completePendingReplies()
    {
        const QList<FakeNetworkReply *> pending = std::exchange(m_pendingReplies, {});
        for (FakeNetworkReply *reply : pending)
        {
            if (reply)
                reply->finish();
        }
    }

  protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                                 QIODevice *outgoingData = nullptr) override
    {
        RecordedRequest recorded;
        recorded.requestOperation = operation;
        recorded.url              = request.url();
        for (const QByteArray &name : request.rawHeaderList())
            recorded.rawHeaders.append({name, request.rawHeader(name)});
        if (outgoingData)
            recorded.body = outgoingData->readAll();
        m_requests.append(recorded);

        FakeNetworkReply::Response response;
        if (!m_responses.isEmpty())
            response = m_responses.dequeue();
        else
        {
            response.httpStatus = 500;
            response.error      = QNetworkReply::ContentNotFoundError;
            response.errorText  = QStringLiteral("No fake response queued");
        }

        auto *reply = new FakeNetworkReply(request, operation, std::move(response), this);
        m_lastReply = reply;
        m_pendingReplies.append(reply);
        return reply;
    }

  private:
    QList<RecordedRequest>             m_requests;
    QQueue<FakeNetworkReply::Response> m_responses;
    QList<FakeNetworkReply *>          m_pendingReplies;
    QNetworkReply                     *m_lastReply = nullptr;
};

} // namespace RoomTunes::Tests
