#include <QSignalSpy>
#include <QtTest>

#include "app/controllers/SearchController.h"
#include "app/models/MediaItemModel.h"
#include "app/models/SearchSectionModel.h"
#include "server/emby/EmbyClient.h"

using namespace strmqt;

namespace {

MediaItem item(const QString &id, const QString &type)
{
    MediaItem value;
    value.id = id;
    value.name = id;
    value.type = type;
    return value;
}

} // namespace

class SearchSectionsTest : public QObject
{
    Q_OBJECT

private slots:
    void controllerOwnsTaxonomyViews();
    void allTaxonomySectionsAreLiveViews();
    void resetAndAppendStayIncremental();
    void displayAndUserStateRolesPropagate();
    void unknownAndMissingTypesFallBackToOther();
    void sourceAndNavigationIdentityMappingStayStable();
};

void SearchSectionsTest::controllerOwnsTaxonomyViews()
{
    emby::EmbyClient client;
    SearchController search(&client);

    const QList<SearchSectionModel *> sections = {
        search.movies(),  search.series(), search.episodes(), search.collections(),
        search.artists(), search.albums(), search.tracks(),   search.other(),
    };
    const QList<SearchSectionModel::Section> kinds = {
        SearchSectionModel::Section::Movies,   SearchSectionModel::Section::Series,
        SearchSectionModel::Section::Episodes, SearchSectionModel::Section::Collections,
        SearchSectionModel::Section::Artists,  SearchSectionModel::Section::Albums,
        SearchSectionModel::Section::Tracks,   SearchSectionModel::Section::Other,
    };
    for (int i = 0; i < sections.size(); ++i) {
        QVERIFY(sections.at(i));
        QCOMPARE(sections.at(i)->sourceModel(), search.model());
        QCOMPARE(sections.at(i)->section(), kinds.at(i));
    }
}

void SearchSectionsTest::allTaxonomySectionsAreLiveViews()
{
    MediaItemModel source;
    SearchSectionModel movies(SearchSectionModel::Section::Movies, &source);
    SearchSectionModel series(SearchSectionModel::Section::Series, &source);
    SearchSectionModel episodes(SearchSectionModel::Section::Episodes, &source);
    SearchSectionModel collections(SearchSectionModel::Section::Collections, &source);
    SearchSectionModel artists(SearchSectionModel::Section::Artists, &source);
    SearchSectionModel albums(SearchSectionModel::Section::Albums, &source);
    SearchSectionModel tracks(SearchSectionModel::Section::Tracks, &source);
    SearchSectionModel other(SearchSectionModel::Section::Other, &source);

    source.setItems({item(QStringLiteral("movie"), QStringLiteral("Movie")),
                     item(QStringLiteral("series"), QStringLiteral("Series")),
                     item(QStringLiteral("episode"), QStringLiteral("Episode")),
                     item(QStringLiteral("collection"), QStringLiteral("BoxSet")),
                     item(QStringLiteral("artist"), QStringLiteral("MusicArtist")),
                     item(QStringLiteral("album"), QStringLiteral("MusicAlbum")),
                     item(QStringLiteral("track"), QStringLiteral("Audio")),
                     item(QStringLiteral("folder"), QStringLiteral("Folder"))});

    const QList<SearchSectionModel *> sections = {&movies,  &series, &episodes, &collections,
                                                  &artists, &albums, &tracks,   &other};
    const QStringList expectedIds = {
        QStringLiteral("movie"),      QStringLiteral("series"), QStringLiteral("episode"),
        QStringLiteral("collection"), QStringLiteral("artist"), QStringLiteral("album"),
        QStringLiteral("track"),      QStringLiteral("folder"),
    };
    for (int i = 0; i < sections.size(); ++i) {
        QCOMPARE(sections.at(i)->rowCount(), 1);
        QCOMPARE(sections.at(i)->get(0).value(QStringLiteral("itemId")).toString(),
                 expectedIds.at(i));
    }
}

void SearchSectionsTest::resetAndAppendStayIncremental()
{
    MediaItemModel source;
    SearchSectionModel movies(SearchSectionModel::Section::Movies, &source);
    SearchSectionModel other(SearchSectionModel::Section::Other, &source);
    QSignalSpy movieInserts(&movies, &QAbstractItemModel::rowsInserted);
    QSignalSpy otherInserts(&other, &QAbstractItemModel::rowsInserted);
    QSignalSpy movieResets(&movies, &QAbstractItemModel::modelReset);

    source.setItems({item(QStringLiteral("movie-a"), QStringLiteral("Movie")),
                     item(QStringLiteral("track-a"), QStringLiteral("Audio"))});
    QCOMPARE(movies.rowCount(), 1);
    QCOMPARE(movies.sourceRow(0), 0);

    movieInserts.clear();
    otherInserts.clear();
    source.appendItems({item(QStringLiteral("movie-b"), QStringLiteral("Movie")),
                        item(QStringLiteral("channel"), QStringLiteral("Channel"))});
    QCOMPARE(movieInserts.count(), 1);
    QCOMPARE(otherInserts.count(), 1);
    QCOMPARE(movies.rowCount(), 2);
    QCOMPARE(movies.sourceRow(1), 2);
    QCOMPARE(other.sourceRow(0), 3);

    movieResets.clear();
    source.setItems({item(QStringLiteral("series"), QStringLiteral("Series")),
                     item(QStringLiteral("folder"), QStringLiteral("Folder"))});
    QCOMPARE(movieResets.count(), 1);
    QCOMPARE(movies.rowCount(), 0);
    QCOMPARE(other.rowCount(), 1);
    QCOMPARE(other.sourceRow(0), 1);
}

void SearchSectionsTest::displayAndUserStateRolesPropagate()
{
    MediaItem credited = item(QStringLiteral("credited"), QStringLiteral("Audio"));
    credited.artists = {QStringLiteral("One"), QStringLiteral("Two")};
    credited.albumArtist = QStringLiteral("Fallback");
    credited.album = QStringLiteral("Record");
    credited.runtimeTicks = 180 * kTicksPerSecond;

    MediaItem fallback = item(QStringLiteral("fallback"), QStringLiteral("Audio"));
    fallback.albumArtist = QStringLiteral("Album Artist");

    MediaItemModel source;
    SearchSectionModel tracks(SearchSectionModel::Section::Tracks, &source);
    source.setItems({credited, fallback});

    const QVariantMap first = tracks.get(0);
    QCOMPARE(first.value(QStringLiteral("artistText")).toString(), QStringLiteral("One, Two"));
    QCOMPARE(first.value(QStringLiteral("albumText")).toString(), QStringLiteral("Record"));
    QCOMPARE(first.value(QStringLiteral("runtimeMs")).toLongLong(), 180000);
    QCOMPARE(tracks.get(1).value(QStringLiteral("artistText")).toString(),
             QStringLiteral("Album Artist"));

    QSignalSpy changed(&tracks, &QAbstractItemModel::dataChanged);
    source.updateUserData(QStringLiteral("credited"), true, true, 42 * kTicksPerSecond, 3);
    QCOMPARE(changed.count(), 1);
    const QVariantMap updated = tracks.get(0);
    QVERIFY(updated.value(QStringLiteral("played")).toBool());
    QVERIFY(updated.value(QStringLiteral("favorite")).toBool());
    QCOMPARE(updated.value(QStringLiteral("playCount")).toInt(), 3);
    QCOMPARE(updated.value(QStringLiteral("positionMs")).toLongLong(), 42000);
}

void SearchSectionsTest::unknownAndMissingTypesFallBackToOther()
{
    MediaItem missing;
    missing.name = QStringLiteral("Tolerant row");

    MediaItemModel source;
    SearchSectionModel other(SearchSectionModel::Section::Other, &source);
    source.setItems({item(QStringLiteral("book"), QStringLiteral("AudioBook")), missing});

    QCOMPARE(other.rowCount(), 2);
    QCOMPARE(other.get(0).value(QStringLiteral("type")).toString(), QStringLiteral("AudioBook"));
    QCOMPARE(other.get(1).value(QStringLiteral("name")).toString(), QStringLiteral("Tolerant row"));
    QCOMPARE(other.sourceRow(-1), -1);
    QCOMPARE(other.sourceRow(2), -1);
    QVERIFY(other.get(2).isEmpty());
}

void SearchSectionsTest::sourceAndNavigationIdentityMappingStayStable()
{
    MediaItemModel source;
    SearchSectionModel movies(SearchSectionModel::Section::Movies, &source);
    source.setItems({item(QStringLiteral("track"), QStringLiteral("Audio")),
                     item(QStringLiteral("movie-a"), QStringLiteral("Movie")),
                     item(QStringLiteral("folder"), QStringLiteral("Folder")),
                     item(QStringLiteral("movie-b"), QStringLiteral("Movie"))});

    QCOMPARE(movies.sourceRow(0), 1);
    QCOMPARE(movies.sourceRow(1), 3);
    QCOMPARE(movies.get(1).value(QStringLiteral("sourceRow")).toInt(), 3);
    QCOMPARE(movies.indexOfNavigationIdentity(QStringLiteral("i:movie-a")), 0);
    QCOMPARE(movies.indexOfNavigationIdentity(QStringLiteral("i:movie-b")), 1);
    QCOMPARE(movies.indexOfNavigationIdentity(QStringLiteral("i:track")), -1);

    source.setItems({item(QStringLiteral("movie-b"), QStringLiteral("Movie")),
                     item(QStringLiteral("track"), QStringLiteral("Audio")),
                     item(QStringLiteral("movie-a"), QStringLiteral("Movie"))});
    QCOMPARE(movies.indexOfNavigationIdentity(QStringLiteral("i:movie-a")), 1);
    QCOMPARE(movies.indexOfNavigationIdentity(QStringLiteral("i:movie-b")), 0);
    QCOMPARE(movies.sourceRow(1), 2);
}

QTEST_GUILESS_MAIN(SearchSectionsTest)
#include "tst_search_sections.moc"
