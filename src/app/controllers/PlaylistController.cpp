#include "PlaylistController.h"

#include "core/Log.h"
#include "server/dto/ItemsQuery.h"
#include "server/emby/EmbyClient.h"

namespace strmqt {

namespace {
constexpr int kMemberLimit = 500;
} // namespace

PlaylistController::PlaylistController(emby::EmbyClient *client, QObject *parent)
    : QObject(parent), m_client(client), m_playlists(new MediaItemModel(this)),
      m_items(new MediaItemModel(this))
{
}

bool PlaylistController::canLoadMore() const
{
    return m_playlists->rowCount() < m_playlists->totalRecordCount();
}

void PlaylistController::refresh()
{
    m_playlistPage = 0;
    fetchPlaylistPage(0);
}

void PlaylistController::loadMorePlaylists()
{
    if (!canLoadMore())
        return;
    fetchPlaylistPage(m_playlists->rowCount());
}

void PlaylistController::fetchPlaylistPage(int startIndex)
{
    const int generation = ++m_listGeneration;
    ItemsQuery query;
    query.includeItemTypes = {QStringLiteral("Playlist")};
    query.recursive = true;
    query.sortBy = QStringLiteral("SortName");
    query.startIndex = startIndex;
    query.limit = kMemberLimit;
    m_client->items(query).then(
        this, [this, generation, startIndex](const Result<ItemsPage> &result) {
            if (generation != m_listGeneration)
                return;
            if (!result.ok()) {
                setError(result.error);
                return;
            }
            if (startIndex == 0)
                m_playlists->setItems(result.value.items, result.value.totalRecordCount);
            else
                m_playlists->appendItems(result.value.items, result.value.totalRecordCount);
            emit playlistsChanged();
        });
}

void PlaylistController::open(const QString &playlistId, const QString &name)
{
    if (playlistId.isEmpty())
        return;
    m_currentId = playlistId;
    m_currentName = name;
    emit currentChanged();
    m_items->clear();
    reload();
}

void PlaylistController::reload()
{
    if (m_currentId.isEmpty())
        return;
    const int generation = ++m_itemsGeneration;
    setLoading(true);
    m_client->playlistItems(m_currentId, 0, kMemberLimit)
        .then(this, [this, generation](const Result<ItemsPage> &result) {
            // The stale check stays above setLoading(false), which is correct
            // now that this counter is the members fetch's alone: the only way
            // to be stale here is that a newer reload() set the flag and is
            // still in flight, and clearing it would drop the spinner while
            // that page is still coming. The flag is never stranded, because
            // whichever reload() currently owns it always gets its own reply.
            if (generation != m_itemsGeneration)
                return;
            setLoading(false);
            if (!result.ok()) {
                setError(result.error);
                return;
            }
            setError(QString());
            m_items->setItems(result.value.items, result.value.totalRecordCount);
        });
}

void PlaylistController::create(const QString &name, const QStringList &itemIds)
{
    if (name.trimmed().isEmpty()) {
        emit actionFailed(tr("A playlist needs a name."));
        return;
    }
    m_client->createPlaylist(name.trimmed(), itemIds)
        .then(this, [this, name](const Result<QString> &result) {
            if (!result.ok()) {
                emit actionFailed(result.error);
                return;
            }
            emit actionSucceeded(tr("Created \"%1\"").arg(name.trimmed()));
            refresh();
        });
}

void PlaylistController::addItems(const QString &playlistId, const QStringList &itemIds)
{
    if (playlistId.isEmpty() || itemIds.isEmpty())
        return;
    m_client->addToPlaylist(playlistId, itemIds)
        .then(this, [this, playlistId, count = itemIds.size()](const Result<bool> &result) {
            if (!result.ok()) {
                emit actionFailed(result.error);
                return;
            }
            emit actionSucceeded(tr("Added %n item(s)", "", count));
            // Only refetch when the user is looking at the list they changed.
            if (playlistId == m_currentId)
                reload();
        });
}

void PlaylistController::removeEntries(const QStringList &entryIds)
{
    if (m_currentId.isEmpty() || entryIds.isEmpty())
        return;
    m_client->removeFromPlaylist(m_currentId, entryIds)
        .then(this, [this, count = entryIds.size()](const Result<bool> &result) {
            if (!result.ok()) {
                emit actionFailed(result.error);
                return;
            }
            emit actionSucceeded(tr("Removed %n item(s)", "", count));
            reload();
        });
}

void PlaylistController::moveEntry(const QString &entryId, int newIndex)
{
    if (m_currentId.isEmpty() || entryId.isEmpty() || newIndex < 0)
        return;
    m_client->movePlaylistItem(m_currentId, entryId, newIndex)
        .then(this, [this](const Result<bool> &result) {
            if (!result.ok()) {
                emit actionFailed(result.error);
                return;
            }
            // The server owns the order; refetch rather than reordering locally
            // and hoping the two agree.
            reload();
        });
}

void PlaylistController::rename(const QString &playlistId, const QString &name)
{
    const QString wanted = name.trimmed();
    if (playlistId.isEmpty() || wanted.isEmpty()) {
        emit actionFailed(tr("A playlist needs a name."));
        return;
    }
    m_client->renameItem(playlistId, wanted)
        .then(this, [this, playlistId, wanted](const Result<bool> &result) {
            if (!result.ok()) {
                emit actionFailed(result.error);
                return;
            }
            emit actionSucceeded(tr("Renamed to \"%1\"").arg(wanted));
            if (playlistId == m_currentId) {
                m_currentName = wanted;
                emit currentChanged();
            }
            refresh();
        });
}

void PlaylistController::remove(const QString &playlistId)
{
    if (playlistId.isEmpty())
        return;
    m_client->deleteItem(playlistId).then(this, [this, playlistId](const Result<bool> &result) {
        if (!result.ok()) {
            emit actionFailed(result.error);
            return;
        }
        emit actionSucceeded(tr("Playlist deleted"));
        if (playlistId == m_currentId) {
            // The open playlist just stopped existing; clear it before the
            // list refreshes, or the page keeps showing tracks from a
            // playlist the server no longer has.
            m_currentId.clear();
            m_currentName.clear();
            m_items->clear();
            // Retire any reload() still in flight for this playlist. Its reply
            // does not know the playlist is gone; without bumping the counter
            // it passes the stale check and repopulates the page we just
            // cleared, with m_currentId already empty.
            ++m_itemsGeneration;
            emit currentChanged();
            emit currentRemoved();
        }
        refresh();
    });
}

void PlaylistController::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void PlaylistController::setError(const QString &message)
{
    if (!message.isEmpty())
        qCWarning(logApp) << "playlist:" << message;
    if (m_error == message)
        return;
    m_error = message;
    emit errorChanged();
}

} // namespace strmqt
