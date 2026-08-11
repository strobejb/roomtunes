#pragma once

#include <functional>

#include <QElapsedTimer>
#include <QList>
#include <QNetworkAccessManager>
#include <QString>

#include "MusicService.h"
#include "Smapi.h"

namespace RoomTunes
{

class Household;

// The sole home of all SMAPI (Sonos Music API) behavior -- a Spotify/
// Pandora/etc. partner service, one instance per entry in this
// household's ThirdPartyMediaServersX. Absorbs what used to be
// ServiceBrowser's browse() logic (see doBrowse()/withCredentials(): a
// DeviceLink/AppLink service reuses the credentials Sonos already stored
// for it, a UserId service asks the zone itself for a sessionId via
// MusicServices:1 GetSessionId, anything else browses directly) plus a
// DeviceLink/AppLink sign-in flow. beginSignIn() obtains the service's
// browser/link-code handoff and immediately starts C++-owned
// getDeviceAuthToken polling; completeSignIn() is only a user-triggered
// "check now" nudge from the dialog, not the owner of the exchange.
class SmapiService : public MusicService
{
    Q_OBJECT

  public:
    // serviceKey is derived as "smapi:<serviceId>" -- see MusicService.h
    // for why services are keyed by opaque string rather than a bare int.
    // serviceId is the household-scoped id from ThirdPartyMediaServersX
    // (stable identity, known even before the SMAPI catalog resolves);
    // smapiId is the separate id MusicServices:1 GetSessionId expects,
    // resolved from the catalog (equal to serviceId for legacy services --
    // see ThirdPartyMediaServers.h) and possibly still unknown (0) the
    // first time a service is constructed, filled in later via
    // updateResolved().
    SmapiService(Household *household, int serviceId, int smapiId, const QString &serviceUri, const QString &authPolicy,
                 const QString &username, const QString &token, const QString &key, const QString &title,
                 const QString &iconUrl, quint32 capabilities = 0, const QString &manifestUri = QString(),
                 QObject *parent = nullptr);

    int serviceId() const override
    {
        return m_serviceId;
    }

    int smapiId() const
    {
        return m_smapiId;
    }

    bool canSearch() const override;
    bool needsSignIn() const override;
    bool shouldOfferReauthorize(const QString &errorMessage) const override;

    QVariantList searchCategories() const override
    {
        return m_searchCategories;
    }

    QString activeSearchCategory() const override
    {
        return m_activeSearchCategoryId;
    }

    // Refreshes the resolved display/credential fields in place (title,
    // icon, smapiId, and whatever ThirdPartyMediaServersX/the SMAPI
    // catalog now say about this service) without destroying/recreating
    // the instance -- Household keeps SmapiService instances alive across
    // catalog rebuilds so a QML BrowseListPage mid-navigation never holds
    // a dangling pointer; see Household::rebuildMusicServices().
    void updateResolved(int smapiId, const QString &serviceUri, const QString &authPolicy, const QString &username,
                        const QString &token, const QString &key, const QString &title, const QString &iconUrl,
                        quint32 capabilities = 0, const QString &manifestUri = QString());

    Q_INVOKABLE void beginSignIn();
    Q_INVOKABLE void completeSignIn(const QString &linkCode);
    Q_INVOKABLE void cancelSignIn();

  signals:
    void authorized();
    void authorizationFailed(const QString &message);
    void deviceLinkCodeReady(const QString &linkCode, const QString &regUrl, bool showLinkCode);

  protected:
    void doBrowse(const QString &objectId, ResultCallback callback) override;
    void doSearch(const QString &category, const QString &term, ResultCallback callback) override;
    void doSearchPreview(const QString &term, int limit, ResultCallback callback) override;

  private:
    // DeviceLink and AppLink browser-fallback building blocks. Not
    // QML-facing themselves -- beginSignIn() owns the sequenced flow and
    // starts getDeviceAuthToken polling once a link code exists.
    void requestDeviceLinkCode(const QString                                                               &householdId,
                               std::function<void(bool ok, const QString &linkCode, const QString &regUrl)> callback);
    void requestAppLinkCode(const QString                                                               &householdId,
                            std::function<void(bool ok, const QString &linkCode, const QString &regUrl)> callback);
    void startDeviceAuthTokenPolling(const QString &linkCode);
    void pollDeviceAuthToken();
    void setDeviceLinkToken(const QString &deviceId, const QString &token, const QString &key,
                            const QString &householdId);
    bool applyPersistedDeviceLinkToken();
    void persistDeviceLinkToken(const QString &deviceId, const QString &token, const QString &key,
                                const QString &householdId);

    // Resolves credentials for the current authPolicy (reusing stored
    // DeviceLink/AppLink credentials, or fetching a fresh UserId sessionId from
    // the zone) and then invokes onReady(); reports through callback
    // instead of calling onReady() if credentials can't be resolved.
    // requestDescription is purely for logging (e.g. "browse A:ALBUM" or
    // "search category=tracks term=abba") -- never sent to the server --
    // so every failure/success line says what request it was against. See
    // ServiceBrowser::browse() (removed) for the logic this replaces.
    void withCredentials(const QString &requestDescription, ResultCallback callback, std::function<void()> onReady);
    void applyStoredCredentials();
    void browseViaSoap(const QString &objectId, const QString &requestDescription, ResultCallback callback);
    void browseRootViaManifest(ResultCallback callback, std::function<void()> fallback, bool allowAuthRefresh = true);
    void refreshAuthTokenForManifestBrowse(ResultCallback callback, std::function<void()> fallback);
    void resolveManifestBrowseEndpoint(std::function<void(const QString &)> callback);

    // reissue rebuilds and re-sends the exact same request (getMetadata or
    // search, whichever doBrowse/doSearch is calling through) -- needed so
    // a "Client.TokenRefreshRequired" fault (see SoapResponse::
    // refreshedAuthToken()) can be retried once with the fresh credentials
    // instead of surfacing as a hard failure the caller sees.
    void runMetadataRequest(class QNetworkReply *reply, const QString &requestDescription, ResultCallback callback,
                            std::function<QNetworkReply *()> reissue, bool isRetry = false);

    // SMAPI's search() takes a service-specific category id (Spotify's own
    // internal ids, not a universal string) -- confirmed against a real
    // "Client.ItemNotFound" fault after guessing a plain "tracks" literal.
    // roomtunes-bb10 (SonosSearch.cpp) resolved this the same way: fetch
    // the service's own category list via a reserved getMetadata("search")
    // call, matching each returned {id, title} against the caller's plain-
    // language hint (e.g. "tracks") since that's all this app's QML side
    // actually knows/needs to say. Cached for this instance's lifetime once
    // fetched -- a service's own search categories aren't something that
    // changes mid-session.
    void    resolveSearchCategory(const QString &hint, ResultCallback callback,
                                  std::function<void(const QString &)> onResolved);
    void    ensureSearchCategories(ResultCallback callback, std::function<void()> onReady);
    QString pickSearchCategoryId(const QString &hint) const;
    // Records which category id a search actually used (for QML's active-
    // pill highlight), emitting activeSearchCategoryChanged() only if it's
    // actually different -- called on every resolved search, not just the
    // first, so switching pills keeps this in sync.
    void    applyResolvedSearchCategory(const QString &categoryId, std::function<void(const QString &)> onResolved);

  private:
    Household *m_household;
    int        m_serviceId;
    int        m_smapiId;
    QString    m_serviceUri;
    quint32    m_capabilities = 0;
    // AppLink manifests describe additional, non-SOAP HTTP APIs. Keep the
    // URI as service metadata, but never bind those endpoints to m_smapi:
    // standard SMAPI calls always go to the descriptor's SecureUri.
    QString    m_manifestUri;
    QString    m_authPolicy;
    QString    m_username;
    QString    m_token;
    QString    m_key;

    // The token/key as last seen from Household (ultimately
    // ThirdPartyMediaServersX) -- kept separately from m_token/m_key so a
    // routine rebuild carrying the *same* stale TPMSX snapshot doesn't
    // stomp a token this instance has since refreshed on its own via
    // Client.TokenRefreshRequired (Sonos' own TPMSX blob has no idea that
    // happened -- it only changes when the service is genuinely
    // relinked). See updateResolved().
    QString       m_tpmsxToken;
    QString       m_tpmsxKey;
    QString       m_pendingLinkDeviceId;
    QString       m_pendingAuthLinkCode;
    bool          m_pendingShowLinkCode   = true;
    bool          m_searchUnsupported     = false;
    bool          m_authTokenPolling      = false;
    bool          m_authTokenPollInFlight = false;
    QElapsedTimer m_authTokenPollStarted;

    enum class ManifestEndpointState
    {
        Unresolved,
        Resolving,
        Resolved,
        Unavailable
    };
    ManifestEndpointState                       m_manifestEndpointState = ManifestEndpointState::Unresolved;
    QString                                     m_manifestBrowseEndpoint;
    QList<std::function<void(const QString &)>> m_manifestEndpointWaiters;

    // {id, title} pairs from getMetadata("search") -- see
    // resolveSearchCategory(). Empty until the first search.
    QVariantList m_searchCategories;
    // Which of m_searchCategories the last search() actually used.
    QString      m_activeSearchCategoryId;

    Smapi m_smapi;
};

} // namespace RoomTunes
