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

    Q_PROPERTY(bool canLoadMore READ canLoadMore NOTIFY playlistsChanged)

    bool canLoadMore() const;

    // Refresh the list of playlists. Cheap and idempotent.
    Q_INVOKABLE void refresh();
    // Next page of playlists. This server has 1,564 of them and one request
    // returns 500, so without paging a playlist named "Zappa" is unreachable
    // and the filter silently narrows only the first page.
    Q_INVOKABLE void loadMorePlaylists();
    Q_INVOKABLE void open(const QString &playlistId, const QString &name);
    Q_INVOKABLE void reload();

    // `itemIds` may be empty: an empty playlist is a legitimate thing to make.
    Q_INVOKABLE void create(const QString &name, const QStringList &itemIds);
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

private:
    void fetchPlaylistPage(int startIndex);
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
    int m_playlistPage = 0;
};

} // namespace strmqt
