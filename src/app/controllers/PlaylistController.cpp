#include "PlaylistController.h"

#include "core/Log.h"
#include "server/dto/ItemsQuery.h"
#include "server/emby/EmbyClient.h"

#include <QCryptographicHash>
#include <QHash>

namespace strmqt {

namespace {
constexpr int kMemberPageSize = 500;
// PlaylistPage's member view is intentionally able to hold 10,000 rows. Keep
// eager loading for shuffle/reorder correctness, but never retain or request
// beyond that UI-scale ceiling when a server reports no usable total or keeps
// manufacturing full pages.
constexpr int kMemberPageLimit = 20;
constexpr int kMemberRowLimit = kMemberPageSize * kMemberPageLimit;
// One page of the playlist LIST. The walk in fetchPlaylistPage() covers the
// measured 1,564 in four round trips.
constexpr int kListPageSize = 500;
// A hard stop on that walk, the same guard MusicController's genre walk carries:
// a server that answers a full page forever must not spin this loop. 20 pages is
// 10,000 playlists — an order of magnitude past the measured library.
constexpr int kListPageLimit = 20;

void appendSignatureField(QByteArray *signature, const QByteArray &field)
{
    signature->append(QByteArray::number(field.size()));
    signature->append(':');
    signature->append(field);
}

QByteArray memberIdentityBase(const MediaItem &item)
{
    QByteArray identity;
    if (!item.playlistItemId.isEmpty()) {
        appendSignatureField(&identity, QByteArrayLiteral("entry"));
        appendSignatureField(&identity, item.playlistItemId.toUtf8());
        return QCryptographicHash::hash(identity, QCryptographicHash::Sha256);
    }

    // A missing PlaylistItemId is malformed for a member row, but tolerant DTO
    // parsing deliberately permits it. Use several stable fields rather than
    // item id alone, which would reject a valid playlist containing the same
    // media item more than once. Per-page occurrence ordinals are added below
    // so repeated identical malformed rows retain their multiplicity.
    appendSignatureField(&identity, QByteArrayLiteral("fallback"));
    appendSignatureField(&identity, item.id.toUtf8());
    appendSignatureField(&identity, item.name.toUtf8());
    appendSignatureField(&identity, item.type.toUtf8());
    appendSignatureField(&identity, item.albumId.toUtf8());
    appendSignatureField(&identity, item.seriesId.toUtf8());
    appendSignatureField(&identity, item.seasonId.toUtf8());
    appendSignatureField(&identity, QByteArray::number(item.indexNumber));
    appendSignatureField(&identity, QByteArray::number(item.parentIndexNumber));
    appendSignatureField(&identity, QByteArray::number(item.runtimeTicks));
    return QCryptographicHash::hash(identity, QCryptographicHash::Sha256);
}

QByteArray memberPageSignature(const QList<MediaItem> &items, QList<QByteArray> *entrySignatures)
{
    QHash<QByteArray, int> occurrences;
    QByteArray page;
    entrySignatures->reserve(items.size());
    for (const MediaItem &item : items) {
        const QByteArray base = memberIdentityBase(item);
        const int occurrence = ++occurrences[base];
        QByteArray entry = base;
        appendSignatureField(&entry, QByteArray::number(occurrence));
        entry = QCryptographicHash::hash(entry, QCryptographicHash::Sha256);
        entrySignatures->append(entry);
        appendSignatureField(&page, entry);
    }
    return QCryptographicHash::hash(page, QCryptographicHash::Sha256);
}
} // namespace

PlaylistController::PlaylistController(emby::EmbyClient *client, QObject *parent)
    : QObject(parent), m_client(client), m_playlists(new MediaItemModel(this)),
      m_items(new MediaItemModel(this))
{
}

void PlaylistController::refresh()
{
    // A restart, not a resume: create(), rename() and remove() all end here, and
    // each of them changes the set this list is a picture of.
    m_listNextIndex = 0;
    m_listComplete = false;
    m_listWalkActive = true;
    emit playlistsChanged(); // `playlistsComplete` just went false
    fetchPlaylistPage(0);
}

void PlaylistController::ensureAllPlaylists()
{
    // NOT "the model has rows, so we are done". A walk that answered page 0 and
    // then failed leaves 500 of 1,564 behind, and an emptiness guard calls that
    // finished for the life of the session — which is what let the picker offer
    // to create a name that already existed on page 2.
    if (m_listComplete || m_listWalkActive)
        return;
    m_listWalkActive = true;
    fetchPlaylistPage(m_listNextIndex);
}

void PlaylistController::fetchPlaylistPage(int startIndex)
{
    const int generation = ++m_listGeneration;
    ItemsQuery query;
    query.includeItemTypes = {QStringLiteral("Playlist")};
    query.recursive = true;
    query.sortBy = QStringLiteral("SortName");
    query.startIndex = startIndex;
    query.limit = kListPageSize;
    m_client->items(query).then(
        this, [this, generation, startIndex](const Result<ItemsPage> &result) {
            if (generation != m_listGeneration)
                return;
            if (!result.ok()) {
                // The walk stops, but it is not *complete*: m_listNextIndex still
                // points at the page that failed, so the next ensureAllPlaylists()
                // picks it up again. `playlistsComplete` stays false, which is
                // what stops a picker offering to create a name it cannot know
                // is free.
                m_listWalkActive = false;
                setError(result.error);
                emit playlistsChanged();
                return;
            }
            if (startIndex == 0)
                m_playlists->setItems(result.value.items, result.value.totalRecordCount);
            else
                m_playlists->appendItems(result.value.items, result.value.totalRecordCount);
            // Page on the ARRAY's own size, never on TotalRecordCount: this
            // family of endpoints has a documented habit of reporting 0 while
            // returning rows (ARCHITECTURE.md §2), and a short page is the end
            // of the list whatever the count says.
            const int received = static_cast<int>(result.value.items.size());
            m_listNextIndex = startIndex + received;
            if (received == kListPageSize && m_listNextIndex < kListPageSize * kListPageLimit) {
                emit playlistsChanged();
                fetchPlaylistPage(m_listNextIndex);
                return;
            }
            // A short page (the end) or the hard stop, which is deliberately also
            // "done": a server that answers a full page forever must not be
            // walked forever either.
            m_listComplete = true;
            m_listWalkActive = false;
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
    resetMemberWalk();
    setError(QString());
    setLoading(true);
    fetchMemberPage(m_currentId, 0, generation);
}

void PlaylistController::fetchMemberPage(const QString &playlistId, int startIndex,
                                          int generation)
{
    m_client->playlistItems(playlistId, startIndex, kMemberPageSize)
        .then(this, [this, playlistId, startIndex, generation](const Result<ItemsPage> &result) {
            // A newer reload/open owns both the model and the spinner. A reply
            // for the old playlist must touch neither.
            if (generation != m_itemsGeneration || playlistId != m_currentId)
                return;
            if (!result.ok()) {
                resetMemberWalk();
                setLoading(false);
                setError(result.error);
                return;
            }

            const int received = static_cast<int>(result.value.items.size());
            if (received > 0) {
                QList<QByteArray> entrySignatures;
                const QByteArray pageSignature =
                    memberPageSignature(result.value.items, &entrySignatures);
                bool contributesNewEntry = false;
                for (const QByteArray &entry : entrySignatures) {
                    if (!m_memberEntrySignatures.contains(entry)) {
                        contributesNewEntry = true;
                        break;
                    }
                }
                if (m_memberPageSignatures.contains(pageSignature) || !contributesNewEntry) {
                    stopMemberWalk(
                        tr("Playlist loading stopped at %1 entries because the server repeated "
                           "or did not advance a page. Reload to try again.")
                            .arg(m_items->rowCount()));
                    return;
                }
                m_memberPageSignatures.insert(pageSignature);
                for (const QByteArray &entry : entrySignatures)
                    m_memberEntrySignatures.insert(entry);
            }

            const int remaining = kMemberRowLimit - m_items->rowCount();
            const int acceptedCount = qMin(received, qMax(0, remaining));
            const QList<MediaItem> accepted = result.value.items.mid(0, acceptedCount);
            if (startIndex == 0)
                m_items->setItems(accepted, result.value.totalRecordCount);
            else
                m_items->appendItems(accepted, result.value.totalRecordCount);
            ++m_memberPagesLoaded;

            const int nextIndex = startIndex + received;
            // Trust a positive advertised total when we have reached it, but
            // never trust zero: several Emby list endpoints return rows with a
            // zero total (ARCHITECTURE.md §2). A short page remains the
            // authoritative end marker in either case.
            const bool reachedAdvertisedTotal =
                result.value.totalRecordCount > 0 && nextIndex >= result.value.totalRecordCount;
            const bool hitSafetyLimit = acceptedCount < received ||
                                        m_items->rowCount() >= kMemberRowLimit ||
                                        m_memberPagesLoaded >= kMemberPageLimit;
            if (acceptedCount < received ||
                (received == kMemberPageSize && !reachedAdvertisedTotal && hitSafetyLimit)) {
                stopMemberWalk(
                    tr("Playlist loading stopped at the %1-entry safety limit. The playlist "
                       "may be incomplete; reload after checking the server.")
                        .arg(kMemberRowLimit));
                return;
            }
            if (received == kMemberPageSize && !reachedAdvertisedTotal) {
                fetchMemberPage(playlistId, nextIndex, generation);
                return;
            }

            resetMemberWalk();
            setError(QString());
            setLoading(false);
        });
}

void PlaylistController::resetMemberWalk()
{
    m_memberPageSignatures.clear();
    m_memberEntrySignatures.clear();
    m_memberPagesLoaded = 0;
}

void PlaylistController::stopMemberWalk(const QString &message)
{
    resetMemberWalk();
    setLoading(false);
    setError(message);
    // A partial model remains usable, so PlaylistPage does not replace it with
    // its empty-error state. Raise the same toast channel as failed verbs to
    // make the truncation visible while those retained rows stay on screen.
    emit actionFailed(message);
}

void PlaylistController::create(const QString &name, const QStringList &itemIds,
                                const QString &mediaType)
{
    if (name.trimmed().isEmpty()) {
        emit actionFailed(tr("A playlist needs a name."));
        return;
    }
    m_client->createPlaylist(name.trimmed(), itemIds, mediaType)
        .then(this, [this, name](const Result<QString> &result) {
            if (!result.ok()) {
                emit actionFailed(result.error);
                return;
            }
            emit actionSucceeded(tr("Created \"%1\"").arg(name.trimmed()));
            emit playlistsMutated();
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
            emit playlistsMutated();
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
            resetMemberWalk();
            // Retire any reload() still in flight for this playlist. Its reply
            // does not know the playlist is gone; without bumping the counter
            // it passes the stale check and repopulates the page we just
            // cleared, with m_currentId already empty.
            ++m_itemsGeneration;
            emit currentChanged();
            emit currentRemoved();
        }
        emit playlistsMutated();
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
