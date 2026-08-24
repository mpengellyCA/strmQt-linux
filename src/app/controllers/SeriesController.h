#pragma once

#include "app/models/MediaItemModel.h"

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace strmqt {

class LiveUpdateService;

namespace emby {
class EmbyClient;
}

// Drives the Series page (Emby-web parity): seasons of a series and the
// episode list of the selected season.
//
// `episodes` is the SELECTED season, which is what the page lists. "What do I
// watch next" is independent of that selection: a bounded server-filtered query
// asks for the first unplayed episode across the series without retaining a
// second, hidden copy of every episode.
class SeriesController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(strmqt::MediaItemModel *seasons READ seasons CONSTANT)
    Q_PROPERTY(strmqt::MediaItemModel *episodes READ episodes CONSTANT)
    Q_PROPERTY(QString seriesId READ seriesId NOTIFY seriesChanged)
    Q_PROPERTY(QString seriesName READ seriesName NOTIFY seriesChanged)
    // The series' own record — backdrop, overview, year, certificate, rating,
    // status — in MediaItemModel's role vocabulary.
    //
    // Fetched rather than recovered from a registered model: the page can be
    // reached from an episode's "go to series", and in that case the series was
    // never in any model, so a model lookup returns nothing and the hero
    // silently degrades to a bare title. That is the commonest path onto this
    // page, not an edge case.
    Q_PROPERTY(QVariantMap series READ series NOTIFY seriesMetadataChanged)
    Q_PROPERTY(int currentSeason READ currentSeason NOTIFY currentSeasonChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    // First unplayed episode across the WHOLE series, in the item-map shape
    // MediaItemModel::get() produces, so it hands straight to ItemActions.
    // Empty when the series is finished or not loaded yet.
    Q_PROPERTY(QVariantMap nextUnwatched READ nextUnwatched NOTIFY nextUnwatchedChanged)
    Q_PROPERTY(bool hasNextUnwatched READ hasNextUnwatched NOTIFY nextUnwatchedChanged)

public:
    explicit SeriesController(emby::EmbyClient *client, QObject *parent = nullptr);

    MediaItemModel *seasons() const { return m_seasons; }
    MediaItemModel *episodes() const { return m_episodes; }
    QString seriesName() const { return m_seriesName; }
    QString seriesId() const { return m_seriesId; }
    int currentSeason() const { return m_currentSeason; }
    bool loading() const { return m_loading; }
    QVariantMap nextUnwatched() const { return m_nextUnwatched; }
    bool hasNextUnwatched() const { return !m_nextUnwatched.isEmpty(); }

    QVariantMap series() const { return m_series; }

    void bindLiveUpdates(LiveUpdateService *service);
    void resetSessionState();

    Q_INVOKABLE void open(const QString &seriesId, const QString &seriesName);
    Q_INVOKABLE void selectSeason(int row);

    // Watched state changed somewhere else in the app (a row's ⋯, the context
    // menu, the player finishing an episode). ItemActions' committed
    // played-state signal and live user-data invalidations reach here so the
    // bounded next-unwatched query is refreshed. The changed item may be in
    // another season, so the selected-season model alone cannot decide whether
    // the answer moved.
    Q_INVOKABLE void notePlayed(const QString &itemId, bool played);

signals:
    void seriesMetadataChanged();
    void seriesChanged();
    void currentSeasonChanged();
    void loadingChanged();
    void nextUnwatchedChanged();

private:
    void setLoading(bool loading);
    void refreshNextUnwatched();
    void setNextUnwatched(QVariantMap next);

    emby::EmbyClient *m_client;
    MediaItemModel *m_seasons;
    MediaItemModel *m_episodes;
    QString m_seriesId;
    QString m_seriesName;
    QVariantMap m_series;
    QVariantMap m_nextUnwatched;
    int m_currentSeason = -1;
    bool m_loading = false;
    // Invalidates in-flight season/episode replies across open()/selectSeason().
    int m_generation = 0;
    // Separate counters for requests that must survive the selectSeason() that
    // open()'s own seasons reply performs, which bumps m_generation.
    int m_seriesGeneration = 0;
    int m_nextUnwatchedGeneration = 0;
};

} // namespace strmqt
