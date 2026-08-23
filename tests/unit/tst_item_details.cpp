#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include "server/emby/EmbyDtoMapper.h"

using namespace strmqt;

// parseItemDetails() used to drop MediaSources and Chapters on the floor even
// though EmbyClient::itemDetails() asks for both, so the details page could say
// nothing about codec/resolution/bitrate or chapters until playback had already
// started (ARCHITECTURE.md). The JSON is inline rather than a fixture file
// because these cases are about *shape* tolerance, not about a recorded server
// response — including the shapes a real server drifts into.
class ItemDetailsTest : public QObject
{
    Q_OBJECT

private:
    static QJsonObject parse(const QByteArray &json)
    {
        return QJsonDocument::fromJson(json).object();
    }

private slots:
    void mediaSourcesAndStreams();
    void chaptersWithBothImageTagShapes();
    void absentSectionsAreEmptyNotAnError();
    void malformedEntriesAreSkipped();
    void peopleCarryIdRoleAndImage();
    void genresStudiosAndExternalLinks();
    void numericIdsAndAbsentEnrichment();
};

// The details page's links (E3) all hang off fields the mapper used to discard:
// People became two lists of bare names, and ProviderIds / ExternalUrls /
// GenreItems / Studios were not read at all.
void ItemDetailsTest::peopleCarryIdRoleAndImage()
{
    const ItemDetails details = emby::parseItemDetails(parse(R"({
        "Id": "1856141",
        "Name": "Spider-Man: No Way Home",
        "Type": "Movie",
        "People": [
            { "Name": "Tom Holland", "Id": "11818", "Role": "Peter Parker / Spider-Man",
              "Type": "Actor", "PrimaryImageTag": "eedb8efa75d6b51760da853301949f34" },
            { "Name": "Zendaya", "Id": "19637", "Role": "MJ", "Type": "Actor" },
            { "Name": "Jon Watts", "Id": "20001", "Type": "Director" },
            { "Name": "", "Id": "99999", "Type": "Actor" }
        ]
    })"));

    // The nameless entry is dropped; server order is preserved for the rest.
    QCOMPARE(details.people.size(), 3);
    QCOMPARE(details.people[0].name, QStringLiteral("Tom Holland"));
    QCOMPARE(details.people[0].id, QStringLiteral("11818"));
    QCOMPARE(details.people[0].role, QStringLiteral("Peter Parker / Spider-Man"));
    QVERIFY(details.people[0].hasImage());

    // A headshot is missing often enough (verified live: 3 of 62 on one film)
    // that a card has to render without one.
    QVERIFY(!details.people[1].hasImage());
    QCOMPARE(details.people[1].role, QStringLiteral("MJ"));

    // The flat name lists the text rows are built on stay populated.
    QCOMPARE(details.cast, QStringList({QStringLiteral("Tom Holland"), QStringLiteral("Zendaya")}));
    QCOMPARE(details.directors, QStringList({QStringLiteral("Jon Watts")}));

    const QList<Person> actors = details.peopleOfType(QStringLiteral("Actor"));
    QCOMPARE(actors.size(), 2);
    QCOMPARE(details.peopleOfType(QStringLiteral("Director")).size(), 1);
}

void ItemDetailsTest::genresStudiosAndExternalLinks()
{
    const ItemDetails details = emby::parseItemDetails(parse(R"({
        "Id": "1856141", "Name": "Spider-Man: No Way Home", "Type": "Movie",
        "Genres": ["Action", "Adventure"],
        "GenreItems": [ { "Name": "Action", "Id": 8122 }, { "Name": "Adventure", "Id": 8120 } ],
        "Studios": [ { "Name": "Marvel Studios", "Id": 8901 } ],
        "ExternalUrls": [
            { "Name": "IMDb", "Url": "https://www.imdb.com/title/tt10872600" },
            { "Name": "TheTVDB", "Url": "https://thetvdb.com/dereferrer/movie/132448" },
            { "Name": "Broken", "Url": "" }
        ],
        "ProviderIds": { "Tmdb": "634649", "Imdb": "tt10872600", "Empty": "" },
        "PremiereDate": "2021-12-15T05:00:00.0000000Z",
        "CriticRating": 93
    })"));

    // Ids arrive as JSON numbers here and as strings elsewhere; both must land
    // as the string the query builder sends.
    QCOMPARE(details.genreItems.size(), 2);
    QCOMPARE(details.genreItems[0].id, QStringLiteral("8122"));
    QCOMPARE(details.studios.size(), 1);
    QCOMPARE(details.studios[0].id, QStringLiteral("8901"));

    // A link with no URL is not a link.
    QCOMPARE(details.externalLinks.size(), 2);
    QCOMPARE(details.externalLinks[0].name, QStringLiteral("IMDb"));
    // Taken verbatim from the server, never rebuilt: TVDB's movie form is
    // /dereferrer/movie/<id> and its series form is not.
    QCOMPARE(details.externalLinks[1].url,
             QStringLiteral("https://thetvdb.com/dereferrer/movie/132448"));

    QCOMPARE(details.providerIds.value(QStringLiteral("Imdb")), QStringLiteral("tt10872600"));
    QVERIFY(!details.providerIds.contains(QStringLiteral("Empty")));
    QCOMPARE(details.criticRating, 93.0);
    QVERIFY(details.premiereDate.startsWith(QStringLiteral("2021-12-15")));
}

void ItemDetailsTest::numericIdsAndAbsentEnrichment()
{
    // A payload that predates GenreItems, and person ids sent as numbers.
    const ItemDetails details = emby::parseItemDetails(parse(R"({
        "Id": "7", "Name": "Old Payload", "Type": "Movie",
        "Genres": ["Drama"],
        "People": [ { "Name": "Someone", "Id": 4242, "Type": "Actor" } ]
    })"));

    QCOMPARE(details.people[0].id, QStringLiteral("4242"));
    QVERIFY(details.genreItems.isEmpty()); // caller falls back to `genres`
    QCOMPARE(details.genres, QStringList({QStringLiteral("Drama")}));
    QVERIFY(details.studios.isEmpty());
    QVERIFY(details.externalLinks.isEmpty());
    QVERIFY(details.providerIds.isEmpty());
    QCOMPARE(details.criticRating, 0.0);
    QVERIFY(details.premiereDate.isEmpty());
}

void ItemDetailsTest::mediaSourcesAndStreams()
{
    const ItemDetails details = emby::parseItemDetails(parse(R"({
        "Id": "abc",
        "Name": "Arrival",
        "Type": "Movie",
        "MediaSources": [
            {
                "Id": "src-4k",
                "Name": "4K Remux",
                "Container": "mkv",
                "Size": 64424509440,
                "Bitrate": 78000000,
                "SupportsDirectPlay": true,
                "MediaStreams": [
                    { "Index": 0, "Type": "Video", "Codec": "hevc",
                      "Width": 3840, "Height": 2160, "VideoRange": "HDR10" },
                    { "Index": 1, "Type": "Audio", "Codec": "truehd",
                      "Language": "eng", "Channels": 8, "ChannelLayout": "7.1" },
                    { "Index": 2, "Type": "Subtitle", "Codec": "pgssub",
                      "Language": "eng", "IsForced": true }
                ]
            },
            {
                "Id": "src-1080",
                "Name": "1080p",
                "Container": "mp4",
                "SupportsTranscoding": true,
                "MediaStreams": [
                    { "Index": 0, "Type": "Video", "Codec": "h264",
                      "Width": 1920, "Height": 1080, "VideoRange": "SDR" }
                ]
            }
        ]
    })"));

    QCOMPARE(details.item.name, QStringLiteral("Arrival"));
    QCOMPARE(details.mediaSources.size(), 2);

    const MediaSource &uhd = details.mediaSources.at(0);
    QCOMPARE(uhd.id, QStringLiteral("src-4k"));
    QCOMPARE(uhd.displayName(), QStringLiteral("4K Remux"));
    QCOMPARE(uhd.resolutionLabel(), QStringLiteral("4K"));
    QVERIFY(uhd.isHdr());
    QVERIFY(uhd.supportsDirectPlay);
    QCOMPARE(uhd.size, Q_INT64_C(64424509440));
    QCOMPARE(uhd.bitrate, Q_INT64_C(78000000));
    QCOMPARE(uhd.streams.size(), 3);
    QVERIFY(uhd.videoStream() != nullptr);
    QCOMPARE(uhd.videoStream()->codec, QStringLiteral("hevc"));
    QCOMPARE(uhd.audioStreams().size(), 1);
    QCOMPARE(uhd.audioStreams().first().channelLayout, QStringLiteral("7.1"));
    QCOMPARE(uhd.subtitleStreams().size(), 1);
    QVERIFY(uhd.subtitleStreams().first().isForced);

    const MediaSource &hd = details.mediaSources.at(1);
    QCOMPARE(hd.resolutionLabel(), QStringLiteral("1080p"));
    QVERIFY(!hd.isHdr());

    // The QML-facing shape a version picker consumes.
    const QVariantMap map = uhd.toVariantMap();
    QCOMPARE(map.value(QStringLiteral("id")).toString(), QStringLiteral("src-4k"));
}

void ItemDetailsTest::chaptersWithBothImageTagShapes()
{
    const ItemDetails details = emby::parseItemDetails(parse(R"({
        "Id": "abc",
        "Chapters": [
            { "Name": "Opening", "StartPositionTicks": 0, "ImageTag": "tag0" },
            { "Name": "Arrival", "StartPositionTicks": 6000000000,
              "ImageTags": { "Primary": "tag1" } },
            { "StartPositionTicks": 12000000000 }
        ]
    })"));

    QCOMPARE(details.chapters.size(), 3);
    QCOMPARE(details.chapters.at(0).name, QStringLiteral("Opening"));
    QCOMPARE(details.chapters.at(0).startMs(), Q_INT64_C(0));
    QCOMPARE(details.chapters.at(0).imageTag, QStringLiteral("tag0"));

    // 6e9 ticks = 600 s: what a scrubber marker actually needs.
    QCOMPARE(details.chapters.at(1).startMs(), Q_INT64_C(600000));
    // Older builds put the thumbnail under ImageTags/Primary like every other item.
    QCOMPARE(details.chapters.at(1).imageTag, QStringLiteral("tag1"));

    // A nameless, imageless chapter is legal and must still carry its position.
    QVERIFY(details.chapters.at(2).name.isEmpty());
    QCOMPARE(details.chapters.at(2).startMs(), Q_INT64_C(1200000));

    const QVariantMap map = details.chapters.at(1).toVariantMap();
    QCOMPARE(map.value(QStringLiteral("startMs")).toLongLong(), Q_INT64_C(600000));
    QCOMPARE(map.value(QStringLiteral("imageTag")).toString(), QStringLiteral("tag1"));
}

void ItemDetailsTest::absentSectionsAreEmptyNotAnError()
{
    // The overwhelmingly common payload: no Fields= on the request.
    const ItemDetails details = emby::parseItemDetails(parse(R"({
        "Id": "abc", "Name": "Arrival", "Taglines": ["Why are they here?"],
        "Genres": ["Sci-Fi"],
        "People": [ { "Type": "Actor", "Name": "Amy Adams" },
                    { "Type": "Director", "Name": "Denis Villeneuve" } ]
    })"));

    QCOMPARE(details.tagline, QStringLiteral("Why are they here?"));
    QCOMPARE(details.genres, QStringList{QStringLiteral("Sci-Fi")});
    QCOMPARE(details.cast, QStringList{QStringLiteral("Amy Adams")});
    QCOMPARE(details.directors, QStringList{QStringLiteral("Denis Villeneuve")});
    QVERIFY(details.mediaSources.isEmpty());
    QVERIFY(details.chapters.isEmpty());
}

void ItemDetailsTest::malformedEntriesAreSkipped()
{
    // AGENTS.md: Emby drift must never crash the app.
    const ItemDetails details = emby::parseItemDetails(parse(R"({
        "MediaSources": ["not an object", 42, { "Id": "ok" }],
        "Chapters": "not an array"
    })"));

    QCOMPARE(details.mediaSources.size(), 1);
    QCOMPARE(details.mediaSources.first().id, QStringLiteral("ok"));
    QVERIFY(details.chapters.isEmpty());

    QVERIFY(emby::parseItemDetails({}).mediaSources.isEmpty());
    QVERIFY(emby::parseItemDetails({}).chapters.isEmpty());
}

QTEST_GUILESS_MAIN(ItemDetailsTest)
#include "tst_item_details.moc"
