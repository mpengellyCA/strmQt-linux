#pragma once

#include "app/models/MediaItemModel.h"

#include <QObject>
#include <QString>
#include <QVariantList>

namespace strmqt {

namespace emby {
class EmbyClient;
}

// Playlists (ARCHITECTURE.md): the user's own lists, their members, and the verbs
// that change them.
//
// Members address ENTRIES, not items. Emby gives each row a PlaylistItemId
// because the same track can legitimately appear twice in one list, so removing
// "that item" is ambiguous and removing "that entry" is not — the same reason
// PlayQueue keys on an entry.
class PlaylistController : public QObject
{
    Q_OBJECT
    // Every playlist the user has, for pickers and the browse page.
    Q_PROPERTY(strmqt::MediaItemModel *playlists READ playlists CONSTANT)
    // Members of the currently opened playlist.
    Q_PROPERTY(strmqt::MediaItemModel *items READ items CONSTANT)
    Q_PROPERTY(QString currentId READ currentId NOTIFY currentChanged)
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged)

public:
    explicit PlaylistController(emby::EmbyClient *client, QObject *parent = nullptr);

    MediaItemModel *playlists() const { return m_playlists; }
    MediaItemModel *items() const { return m_items; }
    QString currentId() const { return m_currentId; }
    QString currentName() const { return m_currentName; }
    bool loading() const { return m_loading; }
    QString errorMessage() const { return m_error; }

    // ── The list is walked to the END, not paged on demand ────────────────────
    // Measured on the target server: 1,564 playlists, and one request returns at
    // most `kListPageSize`. Every consumer of this model needs the WHOLE list,
    // and one of them needs it for correctness rather than for completeness:
    // PlaylistPicker offers to CREATE a playlist when the typed name matches
    // nothing it holds, so a name sorting past the first page read as free and
    // Return made a SECOND playlist with a name the user already had. There is
    // no server-side answer to "is this name taken" either — SearchTerm is a
    // word-prefix match (measured: "Waltz" finds "Waltzes", "altz" finds
    // nothing), so it can confirm a hit and never an absence.
    //
    // So the list pages itself: refresh() asks for page 0 and each reply asks
    // for the next until a short page ends it. Four requests on the measured
    // library, once per session and once per mutation, in exchange for a picker
    // and a browse rail that are not quietly missing two thirds of their rows.
    Q_PROPERTY(bool playlistsComplete READ playlistsComplete NOTIFY playlistsChanged)

    bool playlistsComplete() const { return m_listComplete; }

    // Refresh the list of playlists, from page 0, walking to the end.
    Q_INVOKABLE void refresh();
    // The walk, as a resume rather than a restart: nothing to do when the list
    // is already complete or a walk is running, and otherwise it picks up from
    // the page that stopped it. The same contract MusicController::loadGenres()
    // has, and for the same reason — a walk that broke on page 2 is not a
    // finished list, so a surface about to read the list may always ask.
    Q_INVOKABLE void ensureAllPlaylists();
    Q_INVOKABLE void open(const QString &playlistId, const QString &name);
    Q_INVOKABLE void reload();

    // `itemIds` may be empty: an empty playlist is a legitimate thing to make.
    //
    // `mediaType` is Emby's `MediaType` on POST /Playlists — "Audio" from a
    // music surface, empty everywhere else, which is what every caller passed
    // before this existed. It is the only thing that tells the server what kind
    // of list this is, and the server's answer to "which playlists belong to
    // this library" is derived from it: a playlist created with no MediaType
    // and no members belongs to no library at all (measured on 4.9.5.0), so the
    // music library's Playlists tab would never show it.
    Q_INVOKABLE void create(const QString &name, const QStringList &itemIds,
                            const QString &mediaType = QString());
    Q_INVOKABLE void addItems(const QString &playlistId, const QStringList &itemIds);
    // Entry ids, from the model's playlistItemId role — NOT item ids.
    Q_INVOKABLE void removeEntries(const QStringList &entryIds);
    Q_INVOKABLE void moveEntry(const QString &entryId, int newIndex);
    Q_INVOKABLE void rename(const QString &playlistId, const QString &name);
    // Irreversible on the server. The caller must have confirmed with the user
    // before reaching here; this does not ask.
    Q_INVOKABLE void remove(const QString &playlistId);

signals:
    void currentChanged();
    void loadingChanged();
    void errorChanged();
    void playlistsChanged();
    // A verb succeeded and the UI should say so.
    void actionSucceeded(const QString &message);
    void actionFailed(const QString &message);
    // The open playlist was deleted; the UI must leave it.
    void currentRemoved();
    // The SET of playlists changed: one was created, renamed or deleted. This
    // controller answers by refreshing its own list, but it is not the only
    // list of playlists in the app — MusicController keeps an audio-scoped one
    // for the music library's Playlists tab, and nothing else would ever tell
    // it that the playlist the user just made from a track exists. Emitted
    // alongside refresh(), never instead of it.
    void playlistsMutated();

private:
    void fetchPlaylistPage(int startIndex);
    void fetchMemberPage(const QString &playlistId, int startIndex, int generation);
    void setLoading(bool loading);
    void setError(const QString &message);

    emby::EmbyClient *m_client;
    MediaItemModel *m_playlists;
    MediaItemModel *m_items;
    QString m_currentId;
    QString m_currentName;
    QString m_error;
    bool m_loading = false;
    // Two counters, not one. The playlist LIST and the OPEN playlist's members
    // are independent fetches that supersede only themselves: create(),
    // rename() and remove() all end in refresh(), and with a shared counter
    // that refresh invalidated an in-flight reload() — whose reply then
    // returned above setLoading(false) and left the page spinning on an empty
    // list forever. The same collision ran the other way: open() invalidated an
    // in-flight page of the playlist list, so a picker silently lost a page.
    int m_listGeneration = 0;
    int m_itemsGeneration = 0;
    // The walk's own state, so a walk that stopped on an error can be told apart
    // from one that reached the end of the list — the difference between "every
    // playlist you have" and "the 500 that arrived before page 1 failed", which
    // is precisely the difference between a picker that may offer to create a
    // name and one that must not.
    int m_listNextIndex = 0;
    bool m_listComplete = false;
    bool m_listWalkActive = false;
};

} // namespace strmqt
