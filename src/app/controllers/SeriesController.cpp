#include "SeriesController.h"

#include "core/Log.h"
#include "server/emby/EmbyClient.h"

namespace strmqt {

SeriesController::SeriesController(emby::EmbyClient *client, QObject *parent)
    : QObject(parent), m_client(client), m_seasons(new MediaItemModel(this)),
      m_episodes(new MediaItemModel(this)), m_allEpisodes(new MediaItemModel(this))
{
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
    m_allEpisodes->clear();
    m_currentSeason = -1;
    emit currentSeasonChanged();
    recomputeNextUnwatched();

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

    // Whole-series episode list, once per page entry. An empty seasonId asks
    // Emby for every episode of the series, so "next unwatched" is a real
    // answer instead of "next unwatched in the season you happen to be on".
    // This deliberately does not drive `loading`: the page is usable as soon as
    // the selected season arrives, and the button appears when this settles.
    m_client->episodes(seriesId, QString())
        .then(this, [this, seriesGeneration](const Result<ItemsPage> &result) {
            if (seriesGeneration != m_seriesGeneration)
                return;
            if (!result.ok()) {
                qCWarning(logApp) << "series episodes load failed:" << result.error;
                return;
            }
            m_allEpisodes->setItems(result.value.items, result.value.totalRecordCount);
            recomputeNextUnwatched();
        });

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

void SeriesController::notePlayed(const QString &itemId, bool played)
{
    if (itemId.isEmpty())
        return;
    const auto &items = m_allEpisodes->items();
    for (int row = 0; row < items.size(); ++row) {
        if (items[row].id != itemId)
            continue;
        if (items[row].played == played)
            return;
        // updateUserData() writes both flags, so carry the favorite through
        // untouched rather than clearing it as a side effect of a watch toggle.
        m_allEpisodes->updateUserData(itemId, played, items[row].favorite);
        recomputeNextUnwatched();
        return;
    }
}

void SeriesController::recomputeNextUnwatched()
{
    QVariantMap next;
    const auto &items = m_allEpisodes->items();
    for (int row = 0; row < items.size(); ++row) {
        if (items[row].played)
            continue;
        next = m_allEpisodes->get(row);
        break;
    }
    if (next == m_nextUnwatched)
        return;
    m_nextUnwatched = next;
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
