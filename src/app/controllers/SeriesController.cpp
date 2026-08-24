#include "SeriesController.h"

#include "app/controllers/LiveUpdateService.h"
#include "core/Log.h"
#include "server/emby/EmbyClient.h"

#include <utility>

namespace strmqt {

SeriesController::SeriesController(emby::EmbyClient *client, QObject *parent)
    : QObject(parent), m_client(client), m_seasons(new MediaItemModel(this)),
      m_episodes(new MediaItemModel(this))
{
}

void SeriesController::bindLiveUpdates(LiveUpdateService *service)
{
    if (!service)
        return;
    // A rich socket patch and a polling invalidation converge here after the
    // server owns the new watched state. The changed id may belong to an
    // unloaded season, so every delivered burst becomes one bounded refetch.
    connect(service, &LiveUpdateService::userDataInvalidated, this,
            [this](const QStringList &) {
                if (!m_seriesId.isEmpty())
                    refreshNextUnwatched();
            });
}

void SeriesController::resetSessionState()
{
    ++m_generation;
    ++m_seriesGeneration;
    ++m_nextUnwatchedGeneration;
    m_seasons->clear();
    m_episodes->clear();
    m_seriesId.clear();
    m_seriesName.clear();
    m_series.clear();
    m_nextUnwatched.clear();
    m_currentSeason = -1;
    setLoading(false);
    emit seriesChanged();
    emit seriesMetadataChanged();
    emit currentSeasonChanged();
    emit nextUnwatchedChanged();
}

void SeriesController::open(const QString &seriesId, const QString &seriesName)
{
    const int generation = ++m_generation;
    const int seriesGeneration = ++m_seriesGeneration;
    m_seriesId = seriesId;
    m_seriesName = seriesName;
    emit seriesChanged();
    m_seasons->clear();
    m_episodes->clear();
    m_currentSeason = -1;
    emit currentSeasonChanged();
    setNextUnwatched({});

    // The series' own record. A one-row model is used rather than duplicating
    // the role-name mapping here, so `series` and every item elsewhere in the
    // app speak exactly the same vocabulary and cannot drift.
    m_series.clear();
    emit seriesMetadataChanged();

    // An empty id is a page reset, not a series. The guard used to cover the
    // details fetch only, so /Shows//Episodes and /Shows//Seasons went out
    // anyway — two requests whose only possible answer is 404, one of them
    // logged as a load failure. Nothing is requested here, so nothing is
    // loading either.
    if (seriesId.isEmpty()) {
        setLoading(false);
        return;
    }
    setLoading(true);

    m_client->itemDetails(seriesId).then(
        this, [this, seriesGeneration](const Result<ItemDetails> &result) {
            if (seriesGeneration != m_seriesGeneration || !result.ok())
                return;
            MediaItemModel one;
            one.setItems({result.value.item});
            m_series = one.get(0);
            // Details-only fields the browse roles do not carry.
            m_series.insert(QStringLiteral("overview"), result.value.item.overview);
            m_series.insert(QStringLiteral("genres"), result.value.genres);
            m_series.insert(QStringLiteral("tagline"), result.value.tagline);
            emit seriesMetadataChanged();
        });

    // Independent of the selected season and deliberately not part of
    // `loading`: the page is usable as soon as that season arrives, and the
    // button appears when this bounded request settles.
    refreshNextUnwatched();

    m_client->seasons(seriesId).then(this, [this, generation](const Result<ItemsPage> &result) {
        if (generation != m_generation)
            return;
        if (!result.ok()) {
            setLoading(false);
            qCWarning(logApp) << "seasons load failed:" << result.error;
            return;
        }
        m_seasons->setItems(result.value.items, result.value.totalRecordCount);

        // Emby-web behavior: land on the first season that still has something
        // unwatched; fall back to the first season.
        int startRow = 0;
        const auto &seasons = m_seasons->items();
        for (int row = 0; row < seasons.size(); ++row) {
            if (seasons[row].unplayedItemCount > 0) {
                startRow = row;
                break;
            }
        }
        selectSeason(startRow);
    });
}

void SeriesController::selectSeason(int row)
{
    if (row < 0 || row >= m_seasons->rowCount())
        return;
    const int generation = ++m_generation;
    m_currentSeason = row;
    emit currentSeasonChanged();
    setLoading(true);

    const QString seasonId = m_seasons->items()[row].id;
    m_client->episodes(m_seriesId, seasonId)
        .then(this, [this, generation](const Result<ItemsPage> &result) {
            if (generation != m_generation)
                return;
            setLoading(false);
            if (result.ok())
                m_episodes->setItems(result.value.items, result.value.totalRecordCount);
            else
                qCWarning(logApp) << "episodes load failed:" << result.error;
        });
}

void SeriesController::notePlayed(const QString &itemId, bool)
{
    if (itemId.isEmpty() || m_seriesId.isEmpty())
        return;
    // The changed episode may be in a season that is not loaded. Refetching one
    // row is both cheaper and more reliable than trying to infer membership
    // from the selected-season model. A change from another open surface costs
    // one bounded request and cannot contaminate this series' answer because
    // ParentId remains the current series.
    refreshNextUnwatched();
}

void SeriesController::refreshNextUnwatched()
{
    if (m_seriesId.isEmpty()) {
        setNextUnwatched({});
        return;
    }

    ItemsQuery query;
    query.parentId = m_seriesId;
    query.recursive = true;
    query.includeItemTypes = {QStringLiteral("Episode")};
    query.filters = {QStringLiteral("IsUnplayed")};
    // This is the same verified cross-season air order used by series play-all:
    // PremiereDate is accepted by every Emby 4.x server and SortName makes ties
    // deterministic without requesting episode overviews.
    query.sortBy = QStringLiteral("PremiereDate,SortName");
    query.limit = 1;

    const int generation = ++m_nextUnwatchedGeneration;
    m_client->items(query).then(this, [this, generation](const Result<ItemsPage> &result) {
        if (generation != m_nextUnwatchedGeneration)
            return;
        if (!result.ok()) {
            qCWarning(logApp) << "next unwatched episode load failed:" << result.error;
            return;
        }

        QVariantMap next;
        if (!result.value.items.isEmpty()) {
            // Reuse the public model's one role-to-map implementation without
            // retaining a hidden model or the rest of the series.
            MediaItemModel one;
            one.setItems({result.value.items.first()});
            next = one.get(0);
        }
        setNextUnwatched(std::move(next));
    });
}

void SeriesController::setNextUnwatched(QVariantMap next)
{
    if (next == m_nextUnwatched)
        return;
    m_nextUnwatched = std::move(next);
    emit nextUnwatchedChanged();
}

void SeriesController::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

} // namespace strmqt
