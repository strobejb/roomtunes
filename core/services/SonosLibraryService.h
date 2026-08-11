#pragma once

#include "MusicService.h"

namespace RoomTunes
{

class Household;

// Browses the Sonos local Music Library (a NAS share or USB drive added to
// the household) via plain UPnP ContentDirectory -- no SMAPI involved, so
// no sign-in. "root" returns a synthetic, no-network-call category list
// (ordinary UPnP ContentDirectory object IDs -- A:ALBUMARTIST/A:ALBUM/etc,
// a bb10 precedent); anything else forwards straight to the topology
// zone's ContentDirectory::Browse.
//
// Search: ContentDirectory::Search was never ported (see
// core/control/services/ContentDirectory.h) and doesn't need to be --
// roomtunes-bb10's SonosLibrary::search() never called it either. Sonos'
// own ContentDirectory implementation treats "<category>:<term>" as a
// browsable object id in its own right (e.g. "A:TRACKS:led"), so a
// "search" here is just an ordinary Browse against a constructed id; see
// doSearch(). The category list itself is hardcoded (bb10's own
// m_searchTerms was too -- three fixed root categories, not the full
// browse set: no Genres/Playlists, matching SonosLibrary.cpp exactly),
// not fetched from any getMetadata-equivalent call the way SmapiService's
// SMAPI categories are. Non-root browse calls use Household's current
// browseCoordinator(); UI requests fail immediately if discovery has not
// produced one yet.
class SonosLibraryService : public MusicService
{
    Q_OBJECT

  public:
    explicit SonosLibraryService(Household *household, QObject *parent = nullptr);

    bool canSearch() const override
    {
        return true;
    }

    QVariantList searchCategories() const override;

    QString activeSearchCategory() const override
    {
        return m_activeSearchCategoryId;
    }

  protected:
    void doBrowse(const QString &objectId, ResultCallback callback) override;
    void doBrowseItem(const QVariantMap &item, ResultCallback callback) override;
    void doSearch(const QString &category, const QString &term, ResultCallback callback) override;
    void doSearchPreview(const QString &term, int limit, ResultCallback callback) override;

  private:
    Household *m_household;
    QString    m_activeSearchCategoryId;
};

} // namespace RoomTunes
