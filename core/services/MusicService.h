#pragma once

#include <functional>

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace RoomTunes {

// Abstract interface for a browsable music service -- a SMAPI partner
// (Spotify, Pandora, ...), the Sonos local Music Library, or (eventually)
// something entirely outside the Sonos ecosystem. Deliberately tiny: only
// what every kind of service can actually do (browse/search) lives here.
// Auth-specific behavior (SMAPI's DeviceLink code exchange, for instance)
// stays on the concrete subclass that needs it -- never forced onto every
// subclass as a no-op, and never downcast-to from generic calling code. See
// SmapiService/SonosLibraryService.
//
// serviceKey is an opaque string identity (e.g. "smapi:9", "sonos-library")
// rather than a numeric id specifically so a non-SMAPI service never needs
// a sentinel/magic-number id to fit into the same list as SMAPI ones.
//
// browse()/search() are the QML-facing entry points: Q_INVOKABLE, taking a
// caller-chosen requestToken that's echoed back on browseFinished so a
// QML page mid-navigation can tell its own reply apart from a stale one
// (same contract the old ServiceBrowser used). Each item in the result is a
// QVariantMap of {id, title, artist, album, imageUrl, container} -- the one
// shape every browsable thing in this app is represented as, regardless of
// whether it came from SMAPI XML or a UPnP DIDL-Lite response.
class MusicService : public QObject
{
    Q_OBJECT

    // title/iconSource aren't CONSTANT -- a SmapiService's may improve once
    // the SMAPI catalog/icon feed resolves after construction (see
    // Household::rebuildMusicServices() -> updateResolved()).
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(QString iconSource READ iconSource NOTIFY iconSourceChanged)
    Q_PROPERTY(int serviceId READ serviceId CONSTANT)
    Q_PROPERTY(bool canSearch READ canSearch NOTIFY canSearchChanged)
    Q_PROPERTY(bool needsSignIn READ needsSignIn NOTIFY needsSignInChanged)
    // {id, title} pairs a search-capable service offers to filter by (e.g.
    // Spotify's own "Tracks"/"Albums"/"Artists"/"Playlists" categories) --
    // base default empty, since a service that can't search has none.
    // Populated lazily (only known once a search has actually resolved
    // them; see SmapiService::resolveSearchCategory()), not upfront.
    Q_PROPERTY(QVariantList searchCategories READ searchCategories NOTIFY searchCategoriesChanged)
    // Which of searchCategories the *last* search() call actually used --
    // empty until the first search resolves one. QML uses this to
    // highlight the active filter pill; picking a different one just
    // calls search() again with that category's id.
    Q_PROPERTY(QString activeSearchCategory READ activeSearchCategory NOTIFY activeSearchCategoryChanged)

public:
    using ResultCallback = std::function<void(bool ok, const QString &errorMessage, const QVariantList &items)>;

    MusicService(QString serviceKey, QString title, QString iconSource, QObject *parent = nullptr);
    ~MusicService() override = default;

    const QString &serviceKey() const { return m_serviceKey; }
    virtual int serviceId() const { return -1; }
    QString title() const { return m_title; }
    QString iconSource() const { return m_iconSource; }

    // false on the base; SmapiService overrides. Search isn't offered at
    // all for a service that can't do it (e.g. the library -- UPnP
    // ContentDirectory's Search action was never ported, see
    // core/control/services/ContentDirectory.h) rather than exposed and
    // failing every time.
    virtual bool canSearch() const { return false; }

    // false on the base; SmapiService overrides for DeviceLink/AppLink
    // services with no stored token/key yet.
    virtual bool needsSignIn() const { return false; }

    // Reauthorization is only meaningful for services whose auth policy
    // supports a browser/link-code flow. The default implementation is
    // false; SmapiService overrides it so QML can offer a "Reauthorize"
    // action when browse() returns an expired-account style error.
    Q_INVOKABLE virtual bool shouldOfferReauthorize(const QString &errorMessage) const;

    // Empty on the base -- see the Q_PROPERTY comments above.
    virtual QVariantList searchCategories() const { return {}; }
    virtual QString activeSearchCategory() const { return {}; }

    Q_INVOKABLE void browse(const QString &requestToken, const QString &objectId);
    Q_INVOKABLE void browseItem(const QString &requestToken, const QVariantMap &item);
    Q_INVOKABLE void search(const QString &requestToken, const QString &category, const QString &term);
    Q_INVOKABLE void searchPreview(const QString &requestToken, const QString &term, int limit);

    // Internal C++ browse entry point for service-to-service redirects.
    // Sonos Favourites are the current use: they are discovered via the
    // library's FV:2 ContentDirectory container, but a Spotify favourite's
    // children must be fetched through Spotify SMAPI, not through another
    // ContentDirectory::Browse.
    void browseDirect(const QString &objectId, ResultCallback callback);

signals:
    void browseFinished(const QString &requestToken, bool ok, const QString &errorMessage, const QVariantList &items);
    void needsSignInChanged();
    void searchCategoriesChanged();
    void activeSearchCategoryChanged();
    void canSearchChanged();
    void titleChanged();
    void iconSourceChanged();

protected:
    virtual void doBrowse(const QString &objectId, ResultCallback callback) = 0;
    virtual void doBrowseItem(const QVariantMap &item, ResultCallback callback);
    // Base default: reports "not supported". A subclass only overrides
    // this if it can actually search (see canSearch()).
    virtual void doSearch(const QString &category, const QString &term, ResultCallback callback);
    virtual void doSearchPreview(const QString &term, int limit, ResultCallback callback);

protected:
    void setTitle(const QString &title);
    void setIconSource(const QString &iconSource);

private:
    QString m_serviceKey;
    QString m_title;
    QString m_iconSource;
};

}
