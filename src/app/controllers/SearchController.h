#pragma once

#include "app/models/MediaItemModel.h"

#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QTimer>

namespace strmqt {

namespace emby {
class EmbyClient;
}

// Drives the Search page: debounced server-side search across movies, series
// and episodes.
class SearchController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(strmqt::MediaItemModel *model READ model CONSTANT)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)
    // People and genres matching the query, {id, name, imageUrl} (ARCHITECTURE.md).
    // Separate requests that land after the item search, hence their own signal.
    //
    // NOTE for anyone extending this: /Persons and /Genres report
    // TotalRecordCount = 0 even when they return rows (measured on 4.9.5.0), so
    // these lists are built from the array's own size and never from that count.
    Q_PROPERTY(QVariantList people READ people NOTIFY facetsChanged)
    Q_PROPERTY(QVariantList genres READ genres NOTIFY facetsChanged)
    // Recent queries, newest first, persisted across sessions.
    Q_PROPERTY(QStringList recentQueries READ recentQueries NOTIFY recentQueriesChanged)

public:
    explicit SearchController(emby::EmbyClient *client, QObject *parent = nullptr);

    MediaItemModel *model() const { return m_model; }
    QString query() const { return m_query; }
    void setQuery(const QString &query);
    bool searching() const { return m_searching; }
    QVariantList people() const { return m_people; }
    QVariantList genres() const { return m_genres; }
    QStringList recentQueries() const { return m_recentQueries; }
    // Remembered only when the user acts on a result: every keystroke is a
    // query, and recording those would fill the list with prefixes.
    Q_INVOKABLE void noteQueryUsed(const QString &query);
    Q_INVOKABLE void clearRecentQueries();

signals:
    void queryChanged();
    void searchingChanged();
    void facetsChanged();
    void recentQueriesChanged();

private:
    void runSearch();
    QString recentKey() const;
    void reloadRecentQueries();

    emby::EmbyClient *m_client;
    MediaItemModel *m_model;
    QTimer m_debounce;
    QString m_query;
    bool m_searching = false;
    QVariantList m_people;
    QVariantList m_genres;
    QStringList m_recentQueries;
    int m_generation = 0;
};

} // namespace strmqt
