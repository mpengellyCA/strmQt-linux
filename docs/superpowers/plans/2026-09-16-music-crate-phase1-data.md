# Music Crate, Phase 1: Data Layer

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the music DTOs, the mapper, the composition repository, the typed models, the user-data relay, queue source labels, `MusicPlayback` and the Crate tokens. This phase changes nothing in the UI.

**Architecture:** see the index, `docs/superpowers/plans/2026-09-16-music-crate.md`. **Read its Global Constraints and Shared vocabulary before any task.** Every task in this file implicitly includes them.

**Spec:** `docs/superpowers/specs/2026-09-16-music-crate-design.md` §3 (data layer), §2 (tokens), §5.2–5.3 (filters and sorts the translator implements), §9 (verifications).

## Waves

| Wave | Tasks | Notes |
|---|---|---|
| 1a | 1 | Everything else needs `getJson`/`itemsParams` and the new `ItemsQuery` axes |
| 1b | 2 | Needs the live server; writes `MusicServerCapabilities.h`. Run by the orchestrator, not a subagent, because it may need the user to log in |
| 1c | 3, 7, 8, 16, 18 | Independent: DTOs, mock query routes, cache/fan-out helpers, `PlayQueue.sourceLabel`, Crate tokens (five agents) |
| 1d | 4→5→6 ‖ 9 | Agent A: the mapper, in order. Agent B: translator and `MusicFormat` (needs 1, 2, 3) |
| 1e | 10→11→12 ‖ 13→14 | Agent A: the repository, in order. Agent B: the models (need 3, 9) |
| 1f | 15 ‖ 17 | Relay and `MusicPlayback` (both need 12 and 14; 17 also needs 16) |
| 1g | 19 | Application wiring, then the phase gate |

Agents in one wave all append to `src/CMakeLists.txt` and `tests/CMakeLists.txt`. At the wave gate the orchestrator merges those hunks. They only add lines, so the merge is mechanical.

**Commits in parallel waves:** agents share one working tree, so an agent in a wave with more than one agent does **not** run its task's commit step. It reports the files it touched. At the gate, the orchestrator:
1. builds and tests the integrated tree;
2. runs each task's `git add … && git commit` in task order, with that task's message;
3. runs `rm -rf /tmp/w<wave>*` in the same step as the last commit (AGENTS.md).

A task run by a single agent (waves 1a, 1b, 1g, or any serial execution) commits its own step.

Build commands used in every task (the default dev tree; parallel agents substitute their `/tmp/w<wave><agent>` directory as the index explains):

```bash
cmake --preset dev && cmake --build --preset dev
ctest --preset dev -R <test_name> --output-on-failure
```

---

### Task 1: `getJson`, public `itemsParams`/`artistParams`, new `ItemsQuery` axes, CLI `get`

**Files:**
- Modify: `src/server/dto/ItemsQuery.h`
- Modify: `src/server/emby/EmbyClient.h` (public section near `items()`, line ~76)
- Modify: `src/server/emby/EmbyClient.cpp` (`items()` lines 415–471; the anonymous `artistParams` lines 622–657)
- Modify: `src/cli/main.cpp`
- Test: `tests/integration/tst_emby_client.cpp`

**Interfaces:**
- Produces:
  - `QFuture<Result<QJsonDocument>> EmbyClient::getJson(const QString &path, const QUrlQuery &query, RequestHandle *handle = nullptr)`
  - `static QUrlQuery EmbyClient::itemsParams(const ItemsQuery &query)`
  - `static QUrlQuery EmbyClient::artistParams(const QString &userId, const ItemsQuery &query)`
  - `ItemsQuery::years` (`QList<int>`), `audioCodecs` (`QStringList`), `minDateCreated`, `minPremiereDate`, `maxPremiereDate` (`QString`, ISO-8601), `ids` (`QStringList`)

- [x] **Step 1: Write the failing tests**

Add three slots to `EmbyClientTest` in `tests/integration/tst_emby_client.cpp`: declare them under `private slots:`, then add the definitions before the `QTEST_MAIN` line.

```cpp
    void getJsonSubstitutesUserAndKeepsQuery();
    void getJsonFailsWithoutSession();
    void itemsSendsMusicAxes();
```

```cpp
void EmbyClientTest::getJsonSubstitutesUserAndKeepsQuery()
{
    m_mock->addRoute(QStringLiteral("GET"),
                     QStringLiteral("/Users/%1/Items/Latest").arg(kUserId), 200,
                     R"({"Items":[{"Id":"x1"}]})");
    m_client->setSession(kToken, kUserId);

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("Limit"), QStringLiteral("7"));
    query.addQueryItem(QStringLiteral("UserId"), QStringLiteral("{uid}"));
    const auto result = waitFor(m_client->getJson(QStringLiteral("/Users/{uid}/Items/Latest"), query));

    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.value.object().value(QStringLiteral("Items")).toArray().size(), 1);
    const auto request = m_mock->lastRequestFor(QStringLiteral("GET"), QStringLiteral("/Users/%1/Items/Latest").arg(kUserId));
    QCOMPARE(QUrlQuery(request.query).queryItemValue(QStringLiteral("Limit")), QStringLiteral("7"));
    QCOMPARE(QUrlQuery(request.query).queryItemValue(QStringLiteral("UserId")), kUserId);
}

void EmbyClientTest::getJsonFailsWithoutSession()
{
    const auto result = waitFor(m_client->getJson(QStringLiteral("/Users/{uid}/Items"), {}));
    QVERIFY(!result.ok());
    QCOMPARE(result.error, QStringLiteral("not authenticated"));
    QCOMPARE(m_mock->requestCount(), 0);
}

void EmbyClientTest::itemsSendsMusicAxes()
{
    ItemsQuery query;
    query.years = {1970, 1971};
    query.audioCodecs = {QStringLiteral("flac"), QStringLiteral("alac")};
    query.minDateCreated = QStringLiteral("2026-09-09T00:00:00Z");
    query.minPremiereDate = QStringLiteral("1970-01-01");
    query.maxPremiereDate = QStringLiteral("1979-12-31");
    query.ids = {QStringLiteral("a1"), QStringLiteral("a2")};

    const QUrlQuery params = EmbyClient::itemsParams(query);
    QCOMPARE(params.queryItemValue(QStringLiteral("Years")), QStringLiteral("1970,1971"));
    QCOMPARE(params.queryItemValue(QStringLiteral("AudioCodecs")), QStringLiteral("flac,alac"));
    QCOMPARE(params.queryItemValue(QStringLiteral("MinDateCreated")), QStringLiteral("2026-09-09T00:00:00Z"));
    QCOMPARE(params.queryItemValue(QStringLiteral("MinPremiereDate")), QStringLiteral("1970-01-01"));
    QCOMPARE(params.queryItemValue(QStringLiteral("MaxPremiereDate")), QStringLiteral("1979-12-31"));
    QCOMPARE(params.queryItemValue(QStringLiteral("Ids")), QStringLiteral("a1,a2"));

    // Unset axes are not sent at all.
    const QUrlQuery empty = EmbyClient::itemsParams(ItemsQuery{});
    QVERIFY(!empty.hasQueryItem(QStringLiteral("Years")));
    QVERIFY(!empty.hasQueryItem(QStringLiteral("Ids")));
    QCOMPARE(empty.queryItemValue(QStringLiteral("Limit")), QStringLiteral("100"));
}
```

- [x] **Step 2: Run the tests to verify they fail**

Run: `cmake --build --preset dev --target tst_emby_client`
Expected: a compile error (`getJson`, `itemsParams`, `years` and the other new members do not exist).

- [x] **Step 3: Extend `ItemsQuery`**

In `src/server/dto/ItemsQuery.h`, add `#include <QList>` and insert these members before `bool recursive`:

```cpp
    // Music Crate axes (docs/superpowers/plans/2026-09-16-music-crate-verifications.md
    // records which ones Emby 4.9.5 honours; unset values are never sent).
    QList<int> years;
    QStringList audioCodecs;
    // ISO-8601 bounds, sent verbatim.
    QString minDateCreated;
    QString minPremiereDate;
    QString maxPremiereDate;
    // Explicit id list ("Ids"); the server returns them in its own sort order.
    QStringList ids;
```

- [x] **Step 4: Extract `itemsParams`, publish `artistParams`, add `getJson`**

In `EmbyClient.h`, in the public section right after `items(...)`:

```cpp
    // Raw JSON escape hatch for composed music queries (MusicRepository). `path`
    // and query values may contain "{uid}", replaced with the session's user id. Same auth,
    // cancellation, size limit and epoch rules as every typed call.
    QFuture<Result<QJsonDocument>> getJson(const QString &path, const QUrlQuery &query,
                                           RequestHandle *handle = nullptr);
    // The query string items() sends. Public so composed callers send exactly
    // the same parameters as the typed call.
    static QUrlQuery itemsParams(const ItemsQuery &query);
    // The subset of ItemsQuery the /Artists endpoints honour (measured).
    static QUrlQuery artistParams(const QString &userId, const ItemsQuery &query);
```

Add `#include <QJsonDocument>` and `#include <QUrlQuery>` to the header if they are not already included.

In `EmbyClient.cpp`:
1. Move the body of `artistParams` out of the anonymous namespace. It becomes `QUrlQuery EmbyClient::artistParams(const QString &userId, const ItemsQuery &query)`, with the comment kept. Delete the now-empty `namespace { … }` if nothing else is in it. Callers already written as `artistParams(m_userId, query)` still compile because the call is inside a member function.
2. Replace everything from `QUrlQuery params;` down to `params.addQueryItem(QStringLiteral("Limit"), …);` inside `items()` with `const QUrlQuery params = itemsParams(query);`. Then paste that block into a new function placed just above `items()`, and add the new axes before `Recursive`:

```cpp
QUrlQuery EmbyClient::itemsParams(const ItemsQuery &query)
{
    QUrlQuery params;
    // … the block moved verbatim from items(), from ParentId through ListItemIds …
    if (!query.years.isEmpty()) {
        QStringList years;
        years.reserve(query.years.size());
        for (int year : query.years)
            years.append(QString::number(year));
        params.addQueryItem(QStringLiteral("Years"), years.join(QLatin1Char(',')));
    }
    if (!query.audioCodecs.isEmpty())
        params.addQueryItem(QStringLiteral("AudioCodecs"), query.audioCodecs.join(QLatin1Char(',')));
    if (!query.minDateCreated.isEmpty())
        params.addQueryItem(QStringLiteral("MinDateCreated"), query.minDateCreated);
    if (!query.minPremiereDate.isEmpty())
        params.addQueryItem(QStringLiteral("MinPremiereDate"), query.minPremiereDate);
    if (!query.maxPremiereDate.isEmpty())
        params.addQueryItem(QStringLiteral("MaxPremiereDate"), query.maxPremiereDate);
    if (!query.ids.isEmpty())
        params.addQueryItem(QStringLiteral("Ids"), query.ids.join(QLatin1Char(',')));
    if (query.recursive)
        params.addQueryItem(QStringLiteral("Recursive"), QStringLiteral("true"));
    params.addQueryItem(QStringLiteral("StartIndex"), QString::number(query.startIndex));
    params.addQueryItem(QStringLiteral("Limit"), QString::number(query.limit));
    return params;
}
```

Then add `getJson` below `items()`:

```cpp
QFuture<Result<QJsonDocument>> EmbyClient::getJson(const QString &path, const QUrlQuery &query,
                                                   RequestHandle *handle)
{
    if (handle)
        handle->cancel();
    if (!hasSession())
        return failedFuture<QJsonDocument>(QStringLiteral("not authenticated"));
    QString resolved = path;
    resolved.replace(QStringLiteral("{uid}"), m_userId);
    QUrlQuery resolvedQuery;
    for (auto item : query.queryItems(QUrl::FullyDecoded)) {
        item.second.replace(QStringLiteral("{uid}"), m_userId);
        resolvedQuery.addQueryItem(item.first, item.second);
    }
    QNetworkReply *reply = startGet(resolved, resolvedQuery);
    return finishDocument(reply, handle);
}
```

`failedFuture` is a member template defined in the .cpp. Its instantiation for `QJsonDocument` compiles as long as `getJson` comes after the template definition (line ~344), which it does.

- [x] **Step 5: Run the tests to verify they pass**

Run: `cmake --build --preset dev --target tst_emby_client && ctest --preset dev -R 'tst_emby_client|tst_item_actions|tst_music_query|tst_library' --output-on-failure`
Expected: PASS. The regex also covers the existing `items()` and artist callers, so it catches a regression in the refactor.

- [x] **Step 6: Add the CLI `get` probe**

In `src/cli/main.cpp`, add this above the closing `} // namespace` that precedes `main`:

```cpp
// Raw GET for server measurements: get /Users/{uid}/Items Key=Value ...
int commandGet(emby::EmbyClient &client, const QStringList &args)
{
    if (args.size() < 2) {
        err() << "usage: get PATH [Key=Value ...]   (PATH may contain {uid})\n";
        return 2;
    }
    QUrlQuery query;
    for (qsizetype i = 2; i < args.size(); ++i) {
        const qsizetype eq = args.at(i).indexOf(QLatin1Char('='));
        if (eq <= 0) {
            err() << "error: expected Key=Value, got " << args.at(i) << "\n";
            return 2;
        }
        query.addQueryItem(args.at(i).left(eq), args.at(i).mid(eq + 1));
    }
    const auto result = await(client.getJson(args.at(1), query));
    if (!result.ok()) {
        err() << "error: " << result.error << "\n";
        return 1;
    }
    out() << result.value.toJson(QJsonDocument::Indented);
    return 0;
}
```

Include `<QUrlQuery>` and `<QJsonDocument>` if they are missing. Append ` | get PATH [Key=Value...]` to the "Commands:" description string. Then add dispatch after `nextup`:

```cpp
    if (command == QLatin1String("get"))
        return commandGet(client, args);
```

Run: `cmake --build --preset dev --target strmqt-cli && ./build/dev/strmqt-cli --help | grep get`
Expected: the help text lists `get PATH [Key=Value...]`.

- [x] **Step 7: Commit**

```bash
git add src/server/dto/ItemsQuery.h src/server/emby/EmbyClient.h src/server/emby/EmbyClient.cpp src/cli/main.cpp tests/integration/tst_emby_client.cpp
git commit -m "feat(emby): add getJson, public query builders and music query axes

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: Live server verifications and the capabilities header

This task measures the real server. It does not write application logic, but later tasks branch on what it records. **Do not guess outcomes.**

**Files:**
- Create: `docs/superpowers/plans/2026-09-16-music-crate-verifications.md`
- Create: `src/server/emby/MusicServerCapabilities.h`
- Modify: `src/CMakeLists.txt` (add the header to `strmqt_core`)
- Modify: `ARCHITECTURE.md` §2 table "Emby behaviour worth knowing"

**Interfaces:**
- Produces, in namespace `strmqt::emby::caps`, all `inline constexpr`:
  - `bool kAudioCodecsFiltersAudio`, `kAudioCodecsFiltersAlbums`, `kHiResFilter`, `kYearsAcceptsDecade`, `kLyricsAvailable`, `kSimilarArtists`, `kMinDateCreated`, `kAlbumPlayCountSort`, `kGenreItemCounts`, `kMediaStreamsOnLists`, `kAlbumRuntimeOnLists`
  - `const char *kReleaseTypeField` (empty string means the server provides no release type)
  - `const char *kSimilarArtistsPath` (contains `{id}`; empty when `kSimilarArtists` is false)
  - `const char *kHiResQueryKey`, `kHiResQueryValue` (the measured hi-res parameter; both empty when `kHiResFilter` is false)

- [x] **Step 1: Check the session**

Run: `./build/dev/strmqt-cli libraries`
Expected: a library list, with music library `1868998` or another music library. If it prints `error: not logged in. Run: strmqt-cli login --user NAME`, **stop** and ask the user to run `! ./build/dev/strmqt-cli login --user <their name>`, then continue. Use whichever music library id is listed as `$LIB` below. Never write the server URL, user name or token into any file.

- [x] **Step 2: Collect sample ids**

```bash
CLI=./build/dev/strmqt-cli; LIB=<music library id>
$CLI get /Users/{uid}/Items ParentId=$LIB IncludeItemTypes=MusicAlbum Recursive=true Limit=3 Fields=ChildCount,CumulativeRunTimeTicks,Genres,Studios,Tags,ExtraType > /tmp/claude-albums.json
$CLI get /Users/{uid}/Items ParentId=$LIB IncludeItemTypes=Audio Recursive=true Limit=3 Fields=MediaStreams > /tmp/claude-tracks.json
```

Note one album id `$ALBUM`, one track id `$TRACK` and one album-artist id `$ARTIST` from the output. They are measurement inputs only. Do not commit them.

- [x] **Step 3: Run V1–V10**

For each check, run the commands and record the verdict. Compare `TotalRecordCount` values with `| grep TotalRecordCount`.

| # | Question | Commands | Verdict rule |
|---|---|---|---|
| V1a | Does `AudioCodecs` filter Audio? | `get /Users/{uid}/Items ParentId=$LIB IncludeItemTypes=Audio Recursive=true Limit=0` vs the same with `AudioCodecs=flac` | true if the second total is smaller and non-zero (or zero when the library has no flac) |
| V1b | Does `AudioCodecs` filter MusicAlbum? | the same pair with `IncludeItemTypes=MusicAlbum` | true if the totals differ |
| V1c | Is there a hi-res filter? | Audio with `AudioCodecs=flac` vs the same plus `MinAudioBitDepth=24`, then `MinBitDepth=24`, then `MinSampleRate=88200` | `kHiResFilter` true if any lowers the total; record the first working pair in `kHiResQueryKey`/`kHiResQueryValue` (else both `""`) |
| V2 | Does `Years` accept a decade list? | MusicAlbum `Years=1970,1971,1972,1973,1974,1975,1976,1977,1978,1979 Limit=0` vs no Years | true if the total is smaller and non-zero |
| V3 | Is there a release-type field? | inspect `/tmp/claude-albums.json` for `AlbumType`, `ReleaseType`, `ExtraType`, or Tags such as "EP"/"Single" | the key name when one exists, else `""` |
| V4 | Are lyrics available? | `get /Audio/$TRACK/Lyrics` and `get /Items/$TRACK/Lyrics`; also check `MediaStreams` in the tracks file for `"Type":"Subtitle"` or `"Lyrics"` | true only when one endpoint returns lines with timestamps or text; record the working path and shape |
| V5 | Does Similar work for artists? | `get /Artists/$ARTIST/Similar UserId={uid} Limit=8` then `get /Items/$ARTIST/Similar UserId={uid} Limit=8` | true if either returns MusicArtist items; put the first working path, with `{id}` in place of the artist id, in `kSimilarArtistsPath` |
| V6 | Does `MinDateCreated` work? | MusicAlbum `Limit=0` vs the same with `MinDateCreated=<ISO now minus 30 days>` | true if the second total is smaller |
| V7 | Is album PlayCount sort meaningful? | MusicAlbum `SortBy=PlayCount SortOrder=Descending Limit=5 Fields=UserData` | true if the UserData.PlayCount values are non-zero and descending |
| V8 | Does `/MusicGenres` return counts? | `get /MusicGenres UserId={uid} ParentId=$LIB Limit=3 Fields=ItemCounts` | true if `AlbumCount` or `ChildCount` > 0 appears |
| V9 | Are MediaStreams present on list queries? | inspect `/tmp/claude-tracks.json` | `kMediaStreamsOnLists` true if each item has `MediaStreams` with an Audio stream; otherwise check `MediaSources[0].MediaStreams` and set false |
| V10 | Is album runtime present on lists? | inspect `/tmp/claude-albums.json` for `RunTimeTicks` or `CumulativeRunTimeTicks` | true if either is non-zero |

- [x] **Step 4: Record the results**

Create `docs/superpowers/plans/2026-09-16-music-crate-verifications.md` with one section per check, using this format:

```markdown
## V2 Years accepts a decade list
- Emby 4.9.5, measured 2026-09-16
- Albums, no filter: 1240; Years=1970..1979: 212
- Verdict: honoured → `kYearsAcceptsDecade = true`
```

Use the measured numbers. Library totals are fine to record; ids, names and URLs are not.

Create `src/server/emby/MusicServerCapabilities.h` with the measured values:

```cpp
#pragma once

// What Emby 4.9.5 was measured to honour for music (Phase 1 Task 2 of the
// Crate plan; evidence in docs/superpowers/plans/2026-09-16-music-crate-verifications.md).
// Code branches on these instead of probing at runtime. Re-measure before
// changing one.
namespace strmqt::emby::caps {

inline constexpr bool kAudioCodecsFiltersAudio = true;   // V1a
inline constexpr bool kAudioCodecsFiltersAlbums = false; // V1b
inline constexpr bool kHiResFilter = false;              // V1c
inline constexpr const char *kHiResQueryKey = "";       // V1c, e.g. "MinBitDepth"
inline constexpr const char *kHiResQueryValue = "";     // V1c, e.g. "24"
inline constexpr bool kYearsAcceptsDecade = true;        // V2
inline constexpr const char *kReleaseTypeField = "";     // V3 ("" = none)
inline constexpr bool kLyricsAvailable = false;          // V4
inline constexpr bool kSimilarArtists = true;            // V5
inline constexpr const char *kSimilarArtistsPath = "/Artists/{id}/Similar"; // V5 ("" when false)
inline constexpr bool kMinDateCreated = true;            // V6
inline constexpr bool kAlbumPlayCountSort = false;       // V7
inline constexpr bool kGenreItemCounts = false;          // V8
inline constexpr bool kMediaStreamsOnLists = true;       // V9
inline constexpr bool kAlbumRuntimeOnLists = true;       // V10

} // namespace strmqt::emby::caps
```

The literal values shown are an example layout only. Every value must be replaced with the verdict measured in Step 3 before you commit.

Add `server/emby/MusicServerCapabilities.h` to the `strmqt_core` list in `src/CMakeLists.txt`, after `server/emby/EmbyDtoMapper.h …`.

Add one row per measured behaviour to the `| Behaviour | Consequence |` table in ARCHITECTURE.md §2. For example: `| AudioCodecs filters tracks but not albums (4.9.5) | the Format pill is shown for Songs only; MusicQueryTranslator never sends AudioCodecs for albums |`.

If V4 is true, also write the working lyrics path and response shape into the verifications file. Phase 5 Task "Lyrics" reads it from there.

- [x] **Step 5: Build and commit**

Run: `cmake --build --preset dev`
Expected: the build succeeds.

```bash
rm -f /tmp/claude-albums.json /tmp/claude-tracks.json
git add docs/superpowers/plans/2026-09-16-music-crate-verifications.md src/server/emby/MusicServerCapabilities.h src/CMakeLists.txt ARCHITECTURE.md
git commit -m "docs(music): record Emby 4.9.5 music query measurements

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---
### Task 3: Music DTOs, `MusicQuery` and `toMediaItem`

**Files:**
- Create: `src/server/dto/music/MusicTypes.h`
- Create: `src/server/dto/music/MusicQuery.h`
- Create: `src/server/dto/music/MusicMediaItem.h`, `src/server/dto/music/MusicMediaItem.cpp`
- Modify: `src/CMakeLists.txt` (`strmqt_core` list)
- Test: `tests/unit/tst_music_mapper.cpp` (created here; Tasks 4–6 extend it), `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `strmqt::MediaItem` (`src/server/dto/MediaItem.h`), `kTicksPerMs`.
- Produces: every DTO in the index's Shared vocabulary, exactly as written below, plus `Playlist`.

- [x] **Step 1: Write the failing test**

Create `tests/unit/tst_music_mapper.cpp`:

```cpp
#include <QtTest>

#include "server/dto/music/MusicMediaItem.h"
#include "server/dto/music/MusicQuery.h"
#include "server/dto/music/MusicTypes.h"

using namespace strmqt;
using namespace strmqt::music;

class MusicMapperTest : public QObject
{
    Q_OBJECT

private slots:
    void trackConvertsToAudioMediaItem();
    void trackCoverRoundTripsThroughCoverSource();
    void albumAndArtistConvert();
    void queryEqualityAndFilters();
};

void MusicMapperTest::trackConvertsToAudioMediaItem()
{
    Track track;
    track.id = QStringLiteral("t1");
    track.title = QStringLiteral("Time");
    track.artists = {{QStringLiteral("ar1"), QStringLiteral("Pink Floyd")},
                     {QStringLiteral("ar2"), QStringLiteral("Clare Torry")}};
    track.albumArtists = {{QStringLiteral("ar1"), QStringLiteral("Pink Floyd")}};
    track.albumId = QStringLiteral("al1");
    track.albumTitle = QStringLiteral("The Dark Side of the Moon");
    track.discNumber = 1;
    track.trackNumber = 4;
    track.runtimeMs = 413'000;
    track.positionMs = 1'000;
    track.playCount = 3;
    track.played = true;
    track.favourite = true;
    track.playlistItemId = QStringLiteral("pl-entry-9");

    const MediaItem item = toMediaItem(track);
    QCOMPARE(item.id, QStringLiteral("t1"));
    QCOMPARE(item.type, QStringLiteral("Audio"));
    QCOMPARE(item.name, QStringLiteral("Time"));
    QCOMPARE(item.artists, QStringList({QStringLiteral("Pink Floyd"), QStringLiteral("Clare Torry")}));
    QCOMPARE(item.artistIds, QStringList({QStringLiteral("ar1"), QStringLiteral("ar2")}));
    QCOMPARE(item.albumArtist, QStringLiteral("Pink Floyd"));
    QCOMPARE(item.album, QStringLiteral("The Dark Side of the Moon"));
    QCOMPARE(item.albumId, QStringLiteral("al1"));
    QCOMPARE(item.parentIndexNumber, 1);
    QCOMPARE(item.indexNumber, 4);
    QCOMPARE(item.runtimeTicks, Q_INT64_C(413000) * kTicksPerMs);
    QCOMPARE(item.playbackPositionTicks, Q_INT64_C(1000) * kTicksPerMs);
    QCOMPARE(item.playCount, 3);
    QVERIFY(item.played);
    QVERIFY(item.favorite);
    QCOMPARE(item.playlistItemId, QStringLiteral("pl-entry-9"));
}

void MusicMapperTest::trackCoverRoundTripsThroughCoverSource()
{
    Track onAlbum;
    onAlbum.id = QStringLiteral("t1");
    onAlbum.albumId = QStringLiteral("al1");
    onAlbum.coverRef = {QStringLiteral("al1"), QStringLiteral("Primary"), QStringLiteral("tagA")};
    const auto a = toMediaItem(onAlbum).coverSource();
    QCOMPARE(a.itemId, QStringLiteral("al1"));
    QCOMPARE(a.tag, QStringLiteral("tagA"));

    Track own;
    own.id = QStringLiteral("t2");
    own.coverRef = {QStringLiteral("t2"), QStringLiteral("Primary"), QStringLiteral("tagO")};
    const auto o = toMediaItem(own).coverSource();
    QCOMPARE(o.itemId, QStringLiteral("t2"));
    QCOMPARE(o.tag, QStringLiteral("tagO"));

    Track parent;
    parent.id = QStringLiteral("t3");
    parent.albumId = QStringLiteral("al3");
    parent.coverRef = {QStringLiteral("folder9"), QStringLiteral("Primary"), QStringLiteral("tagP")};
    const auto p = toMediaItem(parent).coverSource();
    QCOMPARE(p.itemId, QStringLiteral("folder9"));
    QCOMPARE(p.tag, QStringLiteral("tagP"));

    QVERIFY(!toMediaItem(Track{}).coverSource().isValid());
}

void MusicMapperTest::albumAndArtistConvert()
{
    Album album;
    album.id = QStringLiteral("al1");
    album.title = QStringLiteral("Wish You Were Here");
    album.albumArtists = {{QStringLiteral("ar1"), QStringLiteral("Pink Floyd")}};
    album.year = 1975;
    album.trackCount = 5;
    album.runtimeMs = 2'640'000;
    album.favourite = true;
    album.coverRef = {QStringLiteral("al1"), QStringLiteral("Primary"), QStringLiteral("c")};
    const MediaItem a = toMediaItem(album);
    QCOMPARE(a.type, QStringLiteral("MusicAlbum"));
    QCOMPARE(a.name, album.title);
    QCOMPARE(a.albumArtist, QStringLiteral("Pink Floyd"));
    QCOMPARE(a.artistIds, QStringList({QStringLiteral("ar1")}));
    QCOMPARE(a.productionYear, 1975);
    QCOMPARE(a.childCount, 5);
    QVERIFY(a.favorite);
    QCOMPARE(a.coverSource().tag, QStringLiteral("c"));

    Artist artist;
    artist.id = QStringLiteral("ar1");
    artist.name = QStringLiteral("Pink Floyd");
    artist.albumCount = 15;
    artist.coverRef = {QStringLiteral("ar1"), QStringLiteral("Primary"), QStringLiteral("p")};
    const MediaItem r = toMediaItem(artist);
    QCOMPARE(r.type, QStringLiteral("MusicArtist"));
    QCOMPARE(r.childCount, 15);
    QCOMPARE(r.primaryImageTag, QStringLiteral("p"));
}

void MusicMapperTest::queryEqualityAndFilters()
{
    MusicQuery a;
    QVERIFY(!a.hasFilters());
    MusicQuery b = a;
    QVERIFY(a == b);
    b.decade = 1970;
    QVERIFY(b.hasFilters());
    QVERIFY(!(a == b));
    MusicQuery c;
    c.sortKey = QStringLiteral("added"); // sort is not a filter
    QVERIFY(!c.hasFilters());
    c.format = FormatFilter::Lossless;
    QVERIFY(c.hasFilters());
}

QTEST_GUILESS_MAIN(MusicMapperTest)
#include "tst_music_mapper.moc"
```

Register it in `tests/CMakeLists.txt` after `strmqt_add_test(tst_emby_dto unit/tst_emby_dto.cpp)`:

```cmake
strmqt_add_test(tst_music_mapper unit/tst_music_mapper.cpp)
```

- [x] **Step 2: Run the test to verify it fails**

Run: `cmake --preset dev && cmake --build --preset dev --target tst_music_mapper`
Expected: a compile error (`server/dto/music/MusicMediaItem.h` not found).

- [x] **Step 3: Write `MusicTypes.h`**

```cpp
#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include "server/dto/MediaItem.h"

// Music DTOs (Crate spec §3.2). Value types shaped for the music UI, not for the
// wire: EmbyMusicMapper builds them, MusicRepository composes them. Every field
// has a default so a partial server answer still yields a drawable record.
namespace strmqt::music {

using ImageRef = MediaItem::ImageRef;

struct NamedRef
{
    QString id;
    QString name;
    bool operator==(const NamedRef &) const = default;
};
using ArtistRef = NamedRef;
using GenreRef = NamedRef;

struct AudioFormat
{
    QString codec;      // lower-case wire codec: "flac", "mp3", "dsd_lsbf"
    int bitDepth = 0;
    int sampleRate = 0; // Hz
    int bitrate = 0;    // bits per second
    int channels = 0;
    QString badge;      // "FLAC 24/96", "MP3 320", "DSD64"; empty = draw nothing
    bool isLossless = false;
    bool isHiRes = false;

    bool isValid() const { return !codec.isEmpty(); }
    bool operator==(const AudioFormat &) const = default;
};

enum class ReleaseType { Album, EP, Single, Compilation };

inline QString releaseTypeName(ReleaseType type)
{
    switch (type) {
    case ReleaseType::EP: return QStringLiteral("EP");
    case ReleaseType::Single: return QStringLiteral("Single");
    case ReleaseType::Compilation: return QStringLiteral("Compilation");
    case ReleaseType::Album: break;
    }
    return QStringLiteral("Album");
}

struct Track
{
    QString id;
    QString title;
    QList<ArtistRef> artists;
    QList<ArtistRef> albumArtists;
    QString albumId;
    QString albumTitle;
    int discNumber = 0;
    int trackNumber = 0;
    qint64 runtimeMs = 0;
    qint64 positionMs = 0;
    bool favourite = false;
    bool played = false;
    int playCount = 0;
    QDateTime lastPlayed;
    QDateTime dateAdded;
    AudioFormat format;
    ImageRef coverRef;
    QString playlistItemId; // set only on playlist entries
    // Derived by the mapper:
    QList<ArtistRef> featured;         // "feat." names and extra track artists
    QString displayTitle;              // title with the "(feat. …)" suffix removed
    bool differsFromAlbumArtist = false;
};

struct Album
{
    QString id;
    QString title;
    QList<ArtistRef> albumArtists;
    int year = 0;
    QDateTime premiereDate;
    QList<GenreRef> genres;
    QStringList studios;
    QDateTime dateAdded;
    int trackCount = 0;
    qint64 runtimeMs = 0;
    bool favourite = false;
    int playCount = 0;
    QDateTime lastPlayed;
    ImageRef coverRef;
    ReleaseType releaseType = ReleaseType::Album;
    bool releaseTypeFromServer = false; // true: never re-classified
    int discCount = 0;                  // 0 until tracks are known
    QString formatSummary;              // dominant badge, once tracks are known
};

struct Disc
{
    int number = 1;
    qint64 runtimeMs = 0;
    QList<Track> tracks;
};

struct AlbumSleeve
{
    Album album;
    QList<Disc> discs;
    QList<Album> moreByArtist;
};

struct Artist
{
    QString id;
    QString name;
    ImageRef coverRef;
    ImageRef backdropRef;
    int albumCount = 0;
    int trackCount = 0;
    bool favourite = false;
};

struct ArtistProfile
{
    Artist artist;
    QList<Album> albums;        // Album and Compilation releases filed under the artist
    QList<Album> epsAndSingles;
    QList<Album> appearsOn;     // albums they perform on but are not filed under
    QList<Track> topTracks;
    QList<Artist> similar;
};

struct GenreBin
{
    QString id;
    QString name;
    int recordCount = 0;
    QList<ImageRef> covers; // up to 3
};

enum class StationKind { HeavyRotation, Favourites, DeepCuts, MoreLike, ShuffleAll };

struct Station
{
    StationKind kind = StationKind::ShuffleAll;
    QString label;
    QString seedId; // MoreLike only: the artist id
    QList<ImageRef> covers;
};

struct ContinueListening
{
    Album album;
    Track resumeTrack;
    int resumeIndex = -1;
    double progress = 0.0; // 0..1 through the album
    bool isValid() const { return !album.id.isEmpty() && resumeIndex >= 0; }
};

struct Playlist
{
    QString id;
    QString name;
    int trackCount = 0;
    qint64 runtimeMs = 0;
    QDateTime dateAdded;
    ImageRef coverRef; // Emby renders a collage Primary for playlists
};

template<class T>
struct Page
{
    QList<T> items;
    int totalRecordCount = 0;
    int startIndex = 0;
};

} // namespace strmqt::music
```

- [x] **Step 4: Write `MusicQuery.h`**

```cpp
#pragma once

#include <QString>
#include <QStringList>

// Browse query state (Crate spec §3.4). The UI's words, not the server's:
// MusicQueryTranslator turns it into an ItemsQuery.
namespace strmqt::music {

enum class Section { Albums, Artists, Songs, Genres, Playlists };
enum class FormatFilter { Any, Lossless, Lossy, HiRes };
enum class ArtistMode { AlbumArtists, Everyone };

inline constexpr int kDecadeAny = 0;
inline constexpr int kDecadeEarlier = -1; // before 1950

struct MusicQuery
{
    QString libraryId;
    Section section = Section::Albums;
    QString sortKey = QStringLiteral("name"); // see MusicQueryTranslator::sortKeysFor
    bool descending = false;
    QString letter;          // "", "#", "A".."Z"
    QStringList genreIds;
    int decade = kDecadeAny; // 1950, 1960, … 2020, kDecadeAny or kDecadeEarlier
    FormatFilter format = FormatFilter::Any;
    bool favouritesOnly = false;
    bool unplayedOnly = false;
    ArtistMode artistMode = ArtistMode::AlbumArtists;

    // Filters narrow the set; sort, letter and section do not count.
    bool hasFilters() const
    {
        return !genreIds.isEmpty() || decade != kDecadeAny || format != FormatFilter::Any
               || favouritesOnly || unplayedOnly;
    }
    bool operator==(const MusicQuery &) const = default;
};

} // namespace strmqt::music
```

- [x] **Step 5: Write `MusicMediaItem.h/.cpp`**

`MusicMediaItem.h`:

```cpp
#pragma once

#include "server/dto/MediaItem.h"
#include "server/dto/music/MusicTypes.h"

// Bridge to the playback path (ItemActions, PlayQueue, MPRIS), which speaks
// MediaItem. Spec §3.6's Track::toMediaItem(), as free overloads so the DTOs
// stay plain structs.
namespace strmqt::music {

MediaItem toMediaItem(const Track &track);
MediaItem toMediaItem(const Album &album);
MediaItem toMediaItem(const Artist &artist);

} // namespace strmqt::music
```

`MusicMediaItem.cpp`:

```cpp
#include "server/dto/music/MusicMediaItem.h"

namespace strmqt::music {

namespace {

void applyArtists(MediaItem &item, const QList<ArtistRef> &artists)
{
    for (const ArtistRef &artist : artists) {
        item.artists.append(artist.name);
        item.artistIds.append(artist.id);
    }
}

} // namespace

MediaItem toMediaItem(const Track &track)
{
    MediaItem item;
    item.id = track.id;
    item.name = track.title;
    item.type = QStringLiteral("Audio");
    applyArtists(item, track.artists);
    if (!track.albumArtists.isEmpty())
        item.albumArtist = track.albumArtists.first().name;
    item.album = track.albumTitle;
    item.albumId = track.albumId;
    item.parentIndexNumber = track.discNumber > 0 ? track.discNumber : -1;
    item.indexNumber = track.trackNumber > 0 ? track.trackNumber : -1;
    item.runtimeTicks = track.runtimeMs * kTicksPerMs;
    item.playbackPositionTicks = track.positionMs * kTicksPerMs;
    item.playCount = track.playCount;
    item.played = track.played;
    item.favorite = track.favourite;
    item.playlistItemId = track.playlistItemId;
    // MediaItem::coverSource() prefers album tag, then parent, then own for
    // Audio; place the ref in the slot that reproduces it.
    if (!track.coverRef.tag.isEmpty()) {
        if (track.coverRef.itemId == track.id) {
            item.primaryImageTag = track.coverRef.tag;
        } else if (!track.albumId.isEmpty() && track.coverRef.itemId == track.albumId) {
            item.albumPrimaryImageTag = track.coverRef.tag;
        } else {
            item.parentPrimaryImageItemId = track.coverRef.itemId;
            item.parentPrimaryImageTag = track.coverRef.tag;
        }
    }
    return item;
}

MediaItem toMediaItem(const Album &album)
{
    MediaItem item;
    item.id = album.id;
    item.name = album.title;
    item.type = QStringLiteral("MusicAlbum");
    applyArtists(item, album.albumArtists);
    if (!album.albumArtists.isEmpty())
        item.albumArtist = album.albumArtists.first().name;
    item.productionYear = album.year;
    item.childCount = album.trackCount;
    item.runtimeTicks = album.runtimeMs * kTicksPerMs;
    item.playCount = album.playCount;
    item.favorite = album.favourite;
    if (album.coverRef.itemId == album.id) {
        item.primaryImageTag = album.coverRef.tag;
    } else if (!album.coverRef.tag.isEmpty()) {
        item.parentPrimaryImageItemId = album.coverRef.itemId;
        item.parentPrimaryImageTag = album.coverRef.tag;
    }
    return item;
}

MediaItem toMediaItem(const Artist &artist)
{
    MediaItem item;
    item.id = artist.id;
    item.name = artist.name;
    item.type = QStringLiteral("MusicArtist");
    item.childCount = artist.albumCount;
    item.favorite = artist.favourite;
    if (artist.coverRef.itemId == artist.id)
        item.primaryImageTag = artist.coverRef.tag;
    if (artist.backdropRef.isValid())
        item.backdropImageTags = {artist.backdropRef.tag};
    return item;
}

} // namespace strmqt::music
```

Add these to `strmqt_core` in `src/CMakeLists.txt`, after `server/dto/ItemsQuery.h`:

```cmake
    server/dto/music/MusicTypes.h
    server/dto/music/MusicQuery.h
    server/dto/music/MusicMediaItem.h server/dto/music/MusicMediaItem.cpp
```

- [x] **Step 6: Run the test to verify it passes**

Run: `cmake --build --preset dev --target tst_music_mapper && ctest --preset dev -R tst_music_mapper --output-on-failure`
Expected: PASS (4 tests).

- [x] **Step 7: Commit**

```bash
git add src/server/dto/music src/CMakeLists.txt tests/unit/tst_music_mapper.cpp tests/CMakeLists.txt
git commit -m "feat(music): add music DTOs, browse query and MediaItem bridge

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: Mapper, part 1: audio format and featured artists

**Files:**
- Create: `src/server/emby/EmbyMusicMapper.h`, `src/server/emby/EmbyMusicMapper.cpp`
- Modify: `src/CMakeLists.txt` (`strmqt_core`)
- Test: `tests/unit/tst_music_mapper.cpp`

**Interfaces:**
- Consumes: `music::AudioFormat`, `music::ArtistRef` (Task 3).
- Produces (namespace `strmqt::emby`):
  - `music::AudioFormat deriveAudioFormat(const QString &codec, int bitDepth, int sampleRate, int bitrate, int channels)`
  - `music::AudioFormat parseAudioFormat(const QJsonObject &item)`: reads `MediaStreams`, falling back to `MediaSources[0].MediaStreams`
  - `struct FeaturedSplit { QString title; QStringList names; }` and `FeaturedSplit splitFeatured(const QString &title)`

- [x] **Step 1: Write the failing tests**

Add `#include "server/emby/EmbyMusicMapper.h"`, `#include <QJsonDocument>` and `#include <QJsonObject>` to `tst_music_mapper.cpp`. Add `using namespace strmqt::emby;`, plus this helper in an anonymous namespace above the class:

```cpp
namespace {
QJsonObject json(const char *text)
{
    return QJsonDocument::fromJson(QByteArray(text)).object();
}
} // namespace
```

Add these slots and definitions:

```cpp
    void formatBadges_data();
    void formatBadges();
    void formatReadsListStreamsAndSourceFallback();
    void featuredSplit_data();
    void featuredSplit();
```

```cpp
void MusicMapperTest::formatBadges_data()
{
    QTest::addColumn<QString>("codec");
    QTest::addColumn<int>("bitDepth");
    QTest::addColumn<int>("sampleRate");
    QTest::addColumn<int>("bitrate");
    QTest::addColumn<QString>("badge");
    QTest::addColumn<bool>("lossless");
    QTest::addColumn<bool>("hiRes");

    QTest::newRow("flac cd") << "flac" << 16 << 44100 << 900000 << "FLAC 16/44.1" << true << false;
    QTest::newRow("flac hires") << "FLAC" << 24 << 96000 << 0 << "FLAC 24/96" << true << true;
    QTest::newRow("alac 24/48") << "alac" << 24 << 48000 << 0 << "ALAC 24/48" << true << true;
    QTest::newRow("flac 16/88.2") << "flac" << 16 << 88200 << 0 << "FLAC 16/88.2" << true << true;
    QTest::newRow("mp3") << "mp3" << 0 << 44100 << 320000 << "MP3 320" << false << false;
    QTest::newRow("aac vbr") << "aac" << 0 << 44100 << 256400 << "AAC 256" << false << false;
    QTest::newRow("vorbis") << "vorbis" << 0 << 44100 << 192000 << "OGG 192" << false << false;
    QTest::newRow("lossy no bitrate") << "opus" << 0 << 48000 << 0 << "OPUS" << false << false;
    QTest::newRow("dsd64") << "dsd_lsbf" << 1 << 2822400 << 0 << "DSD64" << true << true;
    QTest::newRow("pcm") << "pcm_s24le" << 24 << 192000 << 0 << "PCM 24/192" << true << true;
    QTest::newRow("wavpack") << "wavpack" << 16 << 44100 << 0 << "WV 16/44.1" << true << false;
    QTest::newRow("lossless no depth") << "flac" << 0 << 0 << 0 << "FLAC" << true << false;
    QTest::newRow("empty") << "" << 24 << 96000 << 0 << "" << false << false;
}

void MusicMapperTest::formatBadges()
{
    QFETCH(QString, codec);
    QFETCH(int, bitDepth);
    QFETCH(int, sampleRate);
    QFETCH(int, bitrate);
    QFETCH(QString, badge);
    QFETCH(bool, lossless);
    QFETCH(bool, hiRes);

    const auto format = deriveAudioFormat(codec, bitDepth, sampleRate, bitrate, 2);
    QCOMPARE(format.badge, badge);
    QCOMPARE(format.isLossless, lossless);
    QCOMPARE(format.isHiRes, hiRes);
    QCOMPARE(format.isValid(), !codec.isEmpty());
    if (!codec.isEmpty())
        QCOMPARE(format.codec, codec.toLower());
}

void MusicMapperTest::formatReadsListStreamsAndSourceFallback()
{
    const auto direct = parseAudioFormat(json(R"({"MediaStreams":[
        {"Type":"Video","Codec":"mjpeg"},
        {"Type":"Audio","Codec":"flac","BitDepth":24,"SampleRate":96000,"Channels":2}]})"));
    QCOMPARE(direct.badge, QStringLiteral("FLAC 24/96"));
    QCOMPARE(direct.channels, 2);

    const auto nested = parseAudioFormat(json(R"({"MediaSources":[{"Container":"mp3",
        "MediaStreams":[{"Type":"Audio","BitRate":320000,"SampleRate":44100}]}]})"));
    QCOMPARE(nested.badge, QStringLiteral("MP3 320")); // codec falls back to Container

    QVERIFY(!parseAudioFormat(json(R"({"MediaStreams":"garbage"})")).isValid());
    QVERIFY(!parseAudioFormat(QJsonObject{}).isValid());
}

void MusicMapperTest::featuredSplit_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("title");
    QTest::addColumn<QStringList>("names");

    QTest::newRow("none") << "Time" << "Time" << QStringList{};
    QTest::newRow("paren feat.") << "Get Lucky (feat. Pharrell Williams)" << "Get Lucky"
                                 << QStringList{"Pharrell Williams"};
    QTest::newRow("bracket ft") << "Song [ft. A & B]" << "Song" << QStringList{"A", "B"};
    QTest::newRow("bare featuring") << "Song featuring A, B & C" << "Song"
                                    << QStringList{"A", "B", "C"};
    QTest::newRow("case") << "Song (FEAT. X)" << "Song" << QStringList{"X"};
    QTest::newRow("not a word boundary") << "Defeat the Feature" << "Defeat the Feature"
                                         << QStringList{};
    QTest::newRow("name with and kept") << "Song (feat. Simon and Garfunkel)" << "Song"
                                        << QStringList{"Simon and Garfunkel"};
}

void MusicMapperTest::featuredSplit()
{
    QFETCH(QString, input);
    QFETCH(QString, title);
    QFETCH(QStringList, names);
    const FeaturedSplit split = splitFeatured(input);
    QCOMPARE(split.title, title);
    QCOMPARE(split.names, names);
}
```

- [x] **Step 2: Run the tests to verify they fail**

Run: `cmake --build --preset dev --target tst_music_mapper`
Expected: a compile error (`server/emby/EmbyMusicMapper.h` not found).

- [x] **Step 3: Write the header**

`src/server/emby/EmbyMusicMapper.h`:

```cpp
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "server/dto/music/MusicTypes.h"

// Emby JSON → music DTOs (Crate spec §3.3). Tolerant like EmbyDtoMapper: unknown
// keys are ignored, wrong types read as defaults, nothing throws.
namespace strmqt::emby {

music::AudioFormat deriveAudioFormat(const QString &codec, int bitDepth, int sampleRate,
                                     int bitrate, int channels);
music::AudioFormat parseAudioFormat(const QJsonObject &item);

struct FeaturedSplit
{
    QString title;
    QStringList names;
};
FeaturedSplit splitFeatured(const QString &title);

} // namespace strmqt::emby
```

- [x] **Step 4: Write the implementation**

`src/server/emby/EmbyMusicMapper.cpp`:

```cpp
#include "server/emby/EmbyMusicMapper.h"

#include <QRegularExpression>

namespace strmqt::emby {

using namespace music;

namespace {

int integer(const QJsonValue &value)
{
    return static_cast<int>(value.toVariant().toLongLong());
}

// "44.1" for 44100, "96" for 96000, "88.2" for 88200.
QString khz(int hz)
{
    const int tenths = qRound(hz / 100.0);
    if (tenths % 10 == 0)
        return QString::number(tenths / 10);
    return QStringLiteral("%1.%2").arg(tenths / 10).arg(tenths % 10);
}

bool isDsd(const QString &codec) { return codec.startsWith(QLatin1String("dsd")); }
bool isPcm(const QString &codec)
{
    return codec.startsWith(QLatin1String("pcm_")) || codec == QLatin1String("wav");
}

QString codecLabel(const QString &codec)
{
    if (isDsd(codec))
        return QStringLiteral("DSD");
    if (isPcm(codec))
        return QStringLiteral("PCM");
    if (codec == QLatin1String("wavpack"))
        return QStringLiteral("WV");
    if (codec == QLatin1String("vorbis"))
        return QStringLiteral("OGG");
    return codec.toUpper();
}

} // namespace

AudioFormat deriveAudioFormat(const QString &codecIn, int bitDepth, int sampleRate, int bitrate,
                              int channels)
{
    const QString codec = codecIn.trimmed().toLower();
    if (codec.isEmpty())
        return {};

    static const QStringList kLossless = {
        QStringLiteral("flac"), QStringLiteral("alac"), QStringLiteral("ape"),
        QStringLiteral("wavpack"), QStringLiteral("tta"), QStringLiteral("truehd"),
        QStringLiteral("mlp")};

    AudioFormat format;
    format.codec = codec;
    format.bitDepth = qMax(0, bitDepth);
    format.sampleRate = qMax(0, sampleRate);
    format.bitrate = qMax(0, bitrate);
    format.channels = qMax(0, channels);
    format.isLossless = isDsd(codec) || isPcm(codec) || kLossless.contains(codec);
    format.isHiRes = format.isLossless && (format.bitDepth > 16 || format.sampleRate > 48000);

    const QString label = codecLabel(codec);
    if (isDsd(codec)) {
        format.isHiRes = true;
        format.badge = format.sampleRate > 0
                           ? QStringLiteral("DSD%1").arg(qRound(format.sampleRate / 44100.0))
                           : label;
    } else if (format.isLossless) {
        format.badge = (format.bitDepth > 0 && format.sampleRate > 0)
                           ? QStringLiteral("%1 %2/%3").arg(label).arg(format.bitDepth).arg(khz(format.sampleRate))
                           : label;
    } else {
        format.badge = format.bitrate > 0
                           ? QStringLiteral("%1 %2").arg(label).arg(qRound(format.bitrate / 1000.0))
                           : label;
    }
    return format;
}

AudioFormat parseAudioFormat(const QJsonObject &item)
{
    auto fromStreams = [](const QJsonArray &streams, const QString &container) -> AudioFormat {
        for (const QJsonValue &value : streams) {
            const QJsonObject stream = value.toObject();
            if (stream.value(QStringLiteral("Type")).toString() != QLatin1String("Audio"))
                continue;
            QString codec = stream.value(QStringLiteral("Codec")).toString();
            if (codec.isEmpty())
                codec = container;
            return deriveAudioFormat(codec, integer(stream.value(QStringLiteral("BitDepth"))),
                                     integer(stream.value(QStringLiteral("SampleRate"))),
                                     integer(stream.value(QStringLiteral("BitRate"))),
                                     integer(stream.value(QStringLiteral("Channels"))));
        }
        return {};
    };

    const QString container = item.value(QStringLiteral("Container")).toString();
    AudioFormat format = fromStreams(item.value(QStringLiteral("MediaStreams")).toArray(), container);
    if (format.isValid())
        return format;
    const QJsonArray sources = item.value(QStringLiteral("MediaSources")).toArray();
    const QJsonObject source = sources.isEmpty() ? QJsonObject{} : sources.at(0).toObject();
    QString sourceContainer = source.value(QStringLiteral("Container")).toString();
    if (sourceContainer.isEmpty())
        sourceContainer = container;
    return fromStreams(source.value(QStringLiteral("MediaStreams")).toArray(), sourceContainer);
}

FeaturedSplit splitFeatured(const QString &title)
{
    static const QRegularExpression kFeat(
        QStringLiteral(R"(\s*[\(\[]\s*(?:feat\.?|ft\.?|featuring)\s+([^\)\]]+)[\)\]]\s*$|\s+(?:feat\.|ft\.|featuring)\s+(.+)$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kSeparators(QStringLiteral(R"(\s*(?:,|&)\s*)"));

    const QRegularExpressionMatch match = kFeat.match(title);
    if (!match.hasMatch())
        return {title, {}};
    const QString names = match.captured(1).isEmpty() ? match.captured(2) : match.captured(1);
    QStringList list;
    for (const QString &name : names.split(kSeparators, Qt::SkipEmptyParts)) {
        const QString trimmed = name.trimmed();
        if (!trimmed.isEmpty())
            list.append(trimmed);
    }
    return {title.left(match.capturedStart()).trimmed(), list};
}

} // namespace strmqt::emby
```

Add `server/emby/EmbyMusicMapper.h server/emby/EmbyMusicMapper.cpp` to `strmqt_core` after `EmbyDtoMapper`.

- [x] **Step 5: Run the tests to verify they pass**

Run: `cmake --build --preset dev --target tst_music_mapper && ctest --preset dev -R tst_music_mapper --output-on-failure`
Expected: PASS.

- [x] **Step 6: Commit**

```bash
git add src/server/emby/EmbyMusicMapper.h src/server/emby/EmbyMusicMapper.cpp src/CMakeLists.txt tests/unit/tst_music_mapper.cpp
git commit -m "feat(music): derive audio format badges and split featured artists

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 5: Mapper, part 2: tracks, albums, artists, genres, playlists

**Files:**
- Modify: `src/server/emby/EmbyMusicMapper.h`, `src/server/emby/EmbyMusicMapper.cpp`
- Test: `tests/unit/tst_music_mapper.cpp`

**Interfaces:**
- Consumes: Task 3 DTOs, Task 4 helpers, `caps::kReleaseTypeField` (Task 2).
- Produces (namespace `strmqt::emby`):
  - `QDateTime parseEmbyDate(const QString &text)`
  - `std::optional<music::ReleaseType> releaseTypeFromTag(const QString &tag)`
  - `music::Track parseTrack(const QJsonObject &)`, `QList<music::Track> parseTracks(const QJsonArray &)`
  - `music::Album parseAlbum(const QJsonObject &)`, `QList<music::Album> parseAlbums(const QJsonArray &)`
  - `music::Artist parseArtist(const QJsonObject &)`, `QList<music::Artist> parseArtists(const QJsonArray &)`
  - `music::GenreBin parseGenreBin(const QJsonObject &)`
  - `music::Playlist parsePlaylist(const QJsonObject &)`, `QList<music::Playlist> parsePlaylists(const QJsonArray &)`

- [x] **Step 1: Write the failing tests**

Add these slots and definitions:

```cpp
    void datesTrimToMilliseconds();
    void trackParsesEverything();
    void trackFeaturedAndArtistDifference();
    void trackToleratesJunk();
    void albumParses();
    void artistGenrePlaylistParse();
```

```cpp
void MusicMapperTest::datesTrimToMilliseconds()
{
    const QDateTime date = parseEmbyDate(QStringLiteral("2024-03-01T12:34:56.1234567Z"));
    QVERIFY(date.isValid());
    QCOMPARE(date.toUTC().time().msec(), 123);
    QVERIFY(parseEmbyDate(QStringLiteral("2024-03-01T12:34:56Z")).isValid());
    QVERIFY(!parseEmbyDate(QStringLiteral("yesterday")).isValid());
    QVERIFY(!parseEmbyDate(QString()).isValid());
}

void MusicMapperTest::trackParsesEverything()
{
    const Track t = parseTrack(json(R"({
        "Id": 12345, "Name": "Money", "Type": "Audio",
        "ArtistItems": [{"Name":"Pink Floyd","Id":"ar1"}],
        "AlbumArtists": [{"Name":"Pink Floyd","Id":"ar1"}],
        "AlbumId": "al1", "Album": "The Dark Side of the Moon",
        "ParentIndexNumber": 1, "IndexNumber": 6,
        "RunTimeTicks": 3830000000,
        "DateCreated": "2023-01-02T03:04:05.0000000Z",
        "AlbumPrimaryImageTag": "atag",
        "ImageTags": {"Primary": "own"},
        "UserData": {"IsFavorite": true, "Played": true, "PlayCount": 7,
                     "PlaybackPositionTicks": 10000000,
                     "LastPlayedDate": "2026-09-01T10:00:00.0000000Z"},
        "MediaStreams": [{"Type":"Audio","Codec":"flac","BitDepth":16,"SampleRate":44100}]
    })"));
    QCOMPARE(t.id, QStringLiteral("12345"));
    QCOMPARE(t.title, QStringLiteral("Money"));
    QCOMPARE(t.displayTitle, QStringLiteral("Money"));
    QCOMPARE(t.artists.size(), 1);
    QCOMPARE(t.artists.first(), (ArtistRef{QStringLiteral("ar1"), QStringLiteral("Pink Floyd")}));
    QCOMPARE(t.albumArtists.first().id, QStringLiteral("ar1"));
    QCOMPARE(t.albumId, QStringLiteral("al1"));
    QCOMPARE(t.albumTitle, QStringLiteral("The Dark Side of the Moon"));
    QCOMPARE(t.discNumber, 1);
    QCOMPARE(t.trackNumber, 6);
    QCOMPARE(t.runtimeMs, Q_INT64_C(383000));
    QCOMPARE(t.positionMs, Q_INT64_C(1000));
    QVERIFY(t.favourite);
    QVERIFY(t.played);
    QCOMPARE(t.playCount, 7);
    QVERIFY(t.lastPlayed.isValid());
    QVERIFY(t.dateAdded.isValid());
    QCOMPARE(t.format.badge, QStringLiteral("FLAC 16/44.1"));
    QCOMPARE(t.coverRef.itemId, QStringLiteral("al1")); // album tag beats own
    QCOMPARE(t.coverRef.tag, QStringLiteral("atag"));
    QVERIFY(!t.differsFromAlbumArtist);
    QVERIFY(t.featured.isEmpty());
}

void MusicMapperTest::trackFeaturedAndArtistDifference()
{
    const Track guest = parseTrack(json(R"({
        "Id":"t1","Name":"Get Lucky (feat. Pharrell Williams)",
        "ArtistItems":[{"Name":"Daft Punk","Id":"dp"},{"Name":"Nile Rodgers","Id":"nr"}],
        "AlbumArtists":[{"Name":"Daft Punk","Id":"dp"}]})"));
    QCOMPARE(guest.displayTitle, QStringLiteral("Get Lucky"));
    QCOMPARE(guest.featured.size(), 2);
    QCOMPARE(guest.featured.at(0).name, QStringLiteral("Pharrell Williams"));
    QCOMPARE(guest.featured.at(1), (ArtistRef{QStringLiteral("nr"), QStringLiteral("Nile Rodgers")}));
    QVERIFY(!guest.differsFromAlbumArtist);

    const Track compilation = parseTrack(json(R"({
        "Id":"t2","Name":"Heroes","Artists":["David Bowie"],
        "AlbumArtist":"Various Artists"})"));
    QCOMPARE(compilation.artists.first().name, QStringLiteral("David Bowie"));
    QCOMPARE(compilation.albumArtists.first().name, QStringLiteral("Various Artists"));
    QVERIFY(compilation.differsFromAlbumArtist);
    QVERIFY(compilation.featured.isEmpty());
}

void MusicMapperTest::trackToleratesJunk()
{
    const Track t = parseTrack(json(R"({"Id":"t","ArtistItems":"nope","UserData":[],
        "RunTimeTicks":"abc","MediaStreams":{},"ImageTags":7})"));
    QCOMPARE(t.id, QStringLiteral("t"));
    QVERIFY(t.artists.isEmpty());
    QCOMPARE(t.runtimeMs, Q_INT64_C(0));
    QVERIFY(!t.format.isValid());
    QVERIFY(!t.coverRef.isValid());
    QVERIFY(parseTracks(QJsonArray{}).isEmpty());
}

void MusicMapperTest::albumParses()
{
    const Album a = parseAlbum(json(R"({
        "Id":"al1","Name":"Wish You Were Here","Type":"MusicAlbum",
        "AlbumArtists":[{"Name":"Pink Floyd","Id":"ar1"}],
        "PremiereDate":"1975-09-12T00:00:00.0000000Z",
        "GenreItems":[{"Name":"Progressive Rock","Id":"g1"}],
        "Studios":[{"Name":"Harvest","Id":"s1"}],
        "ChildCount":5, "CumulativeRunTimeTicks":26400000000,
        "DateCreated":"2020-01-01T00:00:00Z",
        "ImageTags":{"Primary":"cover"},
        "UserData":{"IsFavorite":true,"PlayCount":4}})"));
    QCOMPARE(a.id, QStringLiteral("al1"));
    QCOMPARE(a.year, 1975); // from PremiereDate when ProductionYear is absent
    QCOMPARE(a.genres.first(), (GenreRef{QStringLiteral("g1"), QStringLiteral("Progressive Rock")}));
    QCOMPARE(a.studios, QStringList{QStringLiteral("Harvest")});
    QCOMPARE(a.trackCount, 5);
    QCOMPARE(a.runtimeMs, Q_INT64_C(2640000));
    QVERIFY(a.favourite);
    QCOMPARE(a.playCount, 4);
    QCOMPARE(a.coverRef.itemId, QStringLiteral("al1"));
    QCOMPARE(a.releaseType, ReleaseType::Album);

    const Album fallback = parseAlbum(json(R"({"Id":"al2","ProductionYear":1999,
        "Genres":["Trip Hop"],"AlbumArtist":"Massive Attack","RunTimeTicks":600000000})"));
    QCOMPARE(fallback.year, 1999);
    QCOMPARE(fallback.genres.first().name, QStringLiteral("Trip Hop"));
    QCOMPARE(fallback.albumArtists.first().name, QStringLiteral("Massive Attack"));
    QCOMPARE(fallback.runtimeMs, Q_INT64_C(60000));
    QVERIFY(!fallback.releaseTypeFromServer);

    QCOMPARE(releaseTypeFromTag(QStringLiteral("EP")), std::optional(ReleaseType::EP));
    QCOMPARE(releaseTypeFromTag(QStringLiteral("single")), std::optional(ReleaseType::Single));
    QCOMPARE(releaseTypeFromTag(QStringLiteral("Compilation")), std::optional(ReleaseType::Compilation));
    QCOMPARE(releaseTypeFromTag(QStringLiteral("album")), std::optional(ReleaseType::Album));
    QVERIFY(!releaseTypeFromTag(QStringLiteral("live")).has_value());
}

void MusicMapperTest::artistGenrePlaylistParse()
{
    const Artist artist = parseArtist(json(R"({"Id":"ar1","Name":"Björk",
        "AlbumCount":9,"SongCount":120,"ImageTags":{"Primary":"p"},
        "BackdropImageTags":["b0","b1"],"UserData":{"IsFavorite":true}})"));
    QCOMPARE(artist.name, QStringLiteral("Björk"));
    QCOMPARE(artist.albumCount, 9);
    QCOMPARE(artist.trackCount, 120);
    QCOMPARE(artist.coverRef.tag, QStringLiteral("p"));
    QCOMPARE(artist.backdropRef.imageType, QStringLiteral("Backdrop"));
    QCOMPARE(artist.backdropRef.tag, QStringLiteral("b0"));
    QVERIFY(artist.favourite);
    QCOMPARE(parseArtist(json(R"({"Id":"x","ChildCount":3})")).albumCount, 3);

    const GenreBin genre = parseGenreBin(json(R"({"Id":"g1","Name":"Jazz","AlbumCount":42})"));
    QCOMPARE(genre.recordCount, 42);
    QCOMPARE(parseGenreBin(json(R"({"Id":"g2","ChildCount":5})")).recordCount, 5);

    const Playlist playlist = parsePlaylist(json(R"({"Id":"pl1","Name":"Road trip",
        "ChildCount":31,"CumulativeRunTimeTicks":72000000000,"ImageTags":{"Primary":"col"},
        "DateCreated":"2025-05-05T00:00:00Z"})"));
    QCOMPARE(playlist.trackCount, 31);
    QCOMPARE(playlist.runtimeMs, Q_INT64_C(7200000));
    QCOMPARE(playlist.coverRef.tag, QStringLiteral("col"));
    QVERIFY(playlist.dateAdded.isValid());
    QCOMPARE(parseArtists(QJsonArray{QJsonObject{{"Id", "a"}}, QJsonObject{{"Id", "b"}}}).size(), 2);
}
```

- [x] **Step 2: Run the tests to verify they fail**

Run: `cmake --build --preset dev --target tst_music_mapper`
Expected: a compile error (`parseTrack` and the other new functions are undeclared).

- [x] **Step 3: Extend the header**

Add `#include <QDateTime>` and `#include <optional>`, then these declarations after `splitFeatured`:

```cpp
QDateTime parseEmbyDate(const QString &text);
std::optional<music::ReleaseType> releaseTypeFromTag(const QString &tag);

music::Track parseTrack(const QJsonObject &json);
QList<music::Track> parseTracks(const QJsonArray &json);
music::Album parseAlbum(const QJsonObject &json);
QList<music::Album> parseAlbums(const QJsonArray &json);
music::Artist parseArtist(const QJsonObject &json);
QList<music::Artist> parseArtists(const QJsonArray &json);
music::GenreBin parseGenreBin(const QJsonObject &json);
music::Playlist parsePlaylist(const QJsonObject &json);
QList<music::Playlist> parsePlaylists(const QJsonArray &json);
```

- [x] **Step 4: Implement**

In `EmbyMusicMapper.cpp`, add `#include "server/emby/MusicServerCapabilities.h"` and `#include "server/dto/MediaItem.h"`. Add these helpers inside the existing anonymous namespace:

```cpp
QString text(const QJsonValue &value)
{
    if (value.isString())
        return value.toString();
    if (value.isDouble())
        return value.toVariant().toString();
    return {};
}

qint64 ticksToMs(const QJsonValue &value)
{
    return qMax<qint64>(0, value.toVariant().toLongLong() / kTicksPerMs);
}

QList<NamedRef> refs(const QJsonValue &value)
{
    QList<NamedRef> list;
    for (const QJsonValue &entry : value.toArray()) {
        const QJsonObject object = entry.toObject();
        const QString name = text(object.value(QStringLiteral("Name")));
        if (!name.isEmpty())
            list.append({text(object.value(QStringLiteral("Id"))), name});
    }
    return list;
}

QList<NamedRef> namesOnly(const QJsonValue &value)
{
    QList<NamedRef> list;
    for (const QJsonValue &entry : value.toArray()) {
        const QString name = text(entry);
        if (!name.isEmpty())
            list.append({QString(), name});
    }
    return list;
}

QString primaryTag(const QJsonObject &json)
{
    return text(json.value(QStringLiteral("ImageTags")).toObject().value(QStringLiteral("Primary")));
}

ImageRef ownPrimary(const QJsonObject &json)
{
    const QString tag = primaryTag(json);
    if (tag.isEmpty())
        return {};
    return {text(json.value(QStringLiteral("Id"))), QStringLiteral("Primary"), tag};
}

bool sameArtist(const NamedRef &a, const NamedRef &b)
{
    if (!a.id.isEmpty() && !b.id.isEmpty())
        return a.id == b.id;
    return a.name.compare(b.name, Qt::CaseInsensitive) == 0;
}

bool containsArtist(const QList<NamedRef> &list, const NamedRef &artist)
{
    return std::any_of(list.cbegin(), list.cend(),
                       [&](const NamedRef &entry) { return sameArtist(entry, artist); });
}

struct UserDataFields
{
    bool favourite = false;
    bool played = false;
    int playCount = 0;
    qint64 positionMs = 0;
    QDateTime lastPlayed;
};

UserDataFields userData(const QJsonObject &json)
{
    const QJsonObject data = json.value(QStringLiteral("UserData")).toObject();
    UserDataFields fields;
    fields.favourite = data.value(QStringLiteral("IsFavorite")).toBool();
    fields.played = data.value(QStringLiteral("Played")).toBool();
    fields.playCount = qMax(0, integer(data.value(QStringLiteral("PlayCount"))));
    fields.positionMs = ticksToMs(data.value(QStringLiteral("PlaybackPositionTicks")));
    fields.lastPlayed = parseEmbyDate(text(data.value(QStringLiteral("LastPlayedDate"))));
    return fields;
}

std::optional<ReleaseType> serverReleaseType(const QJsonObject &json)
{
    const QString field = QString::fromLatin1(caps::kReleaseTypeField);
    if (field.isEmpty())
        return std::nullopt;
    const QJsonValue value = json.value(field);
    if (value.isArray()) {
        for (const QJsonValue &entry : value.toArray()) {
            if (auto type = releaseTypeFromTag(text(entry)))
                return type;
        }
        return std::nullopt;
    }
    return releaseTypeFromTag(text(value));
}
```

Add `#include <algorithm>` at the top for `std::any_of`. The helpers call `releaseTypeFromTag` and `parseEmbyDate` before their definitions; that compiles because the header, included first, declares both in the enclosing `strmqt::emby` namespace.

Then add the public functions after `splitFeatured`:

```cpp
QDateTime parseEmbyDate(const QString &input)
{
    if (input.isEmpty())
        return {};
    static const QRegularExpression kFraction(QStringLiteral(R"((\.\d{3})\d+)"));
    QString trimmed = input;
    trimmed.replace(kFraction, QStringLiteral("\\1"));
    return QDateTime::fromString(trimmed, Qt::ISODateWithMs);
}

std::optional<ReleaseType> releaseTypeFromTag(const QString &tag)
{
    const QString key = tag.trimmed().toLower();
    if (key == QLatin1String("album"))
        return ReleaseType::Album;
    if (key == QLatin1String("ep"))
        return ReleaseType::EP;
    if (key == QLatin1String("single"))
        return ReleaseType::Single;
    if (key == QLatin1String("compilation"))
        return ReleaseType::Compilation;
    return std::nullopt;
}

Track parseTrack(const QJsonObject &json)
{
    Track track;
    track.id = text(json.value(QStringLiteral("Id")));
    track.title = text(json.value(QStringLiteral("Name")));
    track.artists = refs(json.value(QStringLiteral("ArtistItems")));
    if (track.artists.isEmpty())
        track.artists = namesOnly(json.value(QStringLiteral("Artists")));
    track.albumArtists = refs(json.value(QStringLiteral("AlbumArtists")));
    if (track.albumArtists.isEmpty()) {
        const QString name = text(json.value(QStringLiteral("AlbumArtist")));
        if (!name.isEmpty())
            track.albumArtists.append({QString(), name});
    }
    track.albumId = text(json.value(QStringLiteral("AlbumId")));
    track.albumTitle = text(json.value(QStringLiteral("Album")));
    track.discNumber = qMax(0, integer(json.value(QStringLiteral("ParentIndexNumber"))));
    track.trackNumber = qMax(0, integer(json.value(QStringLiteral("IndexNumber"))));
    track.runtimeMs = ticksToMs(json.value(QStringLiteral("RunTimeTicks")));
    track.dateAdded = parseEmbyDate(text(json.value(QStringLiteral("DateCreated"))));
    track.playlistItemId = text(json.value(QStringLiteral("PlaylistItemId")));

    const UserDataFields data = userData(json);
    track.favourite = data.favourite;
    track.played = data.played;
    track.playCount = data.playCount;
    track.positionMs = data.positionMs;
    track.lastPlayed = data.lastPlayed;

    track.format = parseAudioFormat(json);

    const QString albumTag = text(json.value(QStringLiteral("AlbumPrimaryImageTag")));
    const QString parentId = text(json.value(QStringLiteral("ParentPrimaryImageItemId")));
    const QString parentTag = text(json.value(QStringLiteral("ParentPrimaryImageTag")));
    if (!albumTag.isEmpty() && !track.albumId.isEmpty())
        track.coverRef = {track.albumId, QStringLiteral("Primary"), albumTag};
    else if (!parentTag.isEmpty() && !parentId.isEmpty())
        track.coverRef = {parentId, QStringLiteral("Primary"), parentTag};
    else
        track.coverRef = ownPrimary(json);

    const FeaturedSplit split = splitFeatured(track.title);
    track.displayTitle = split.title;
    for (const QString &name : split.names) {
        NamedRef ref{QString(), name};
        for (const NamedRef &artist : track.artists) {
            if (sameArtist(artist, ref))
                ref.id = artist.id;
        }
        if (!containsArtist(track.featured, ref))
            track.featured.append(ref);
    }
    const bool albumArtistPerforms = std::any_of(
        track.albumArtists.cbegin(), track.albumArtists.cend(),
        [&](const NamedRef &albumArtist) { return containsArtist(track.artists, albumArtist); });
    if (albumArtistPerforms) {
        for (const NamedRef &artist : track.artists) {
            if (!containsArtist(track.albumArtists, artist) && !containsArtist(track.featured, artist))
                track.featured.append(artist);
        }
    }
    track.differsFromAlbumArtist =
        !track.artists.isEmpty() && !track.albumArtists.isEmpty() && !albumArtistPerforms;
    return track;
}

QList<Track> parseTracks(const QJsonArray &json)
{
    QList<Track> list;
    list.reserve(json.size());
    for (const QJsonValue &value : json)
        list.append(parseTrack(value.toObject()));
    return list;
}

Album parseAlbum(const QJsonObject &json)
{
    Album album;
    album.id = text(json.value(QStringLiteral("Id")));
    album.title = text(json.value(QStringLiteral("Name")));
    album.albumArtists = refs(json.value(QStringLiteral("AlbumArtists")));
    if (album.albumArtists.isEmpty()) {
        const QString name = text(json.value(QStringLiteral("AlbumArtist")));
        if (!name.isEmpty())
            album.albumArtists.append({QString(), name});
    }
    album.premiereDate = parseEmbyDate(text(json.value(QStringLiteral("PremiereDate"))));
    album.year = integer(json.value(QStringLiteral("ProductionYear")));
    if (album.year <= 0 && album.premiereDate.isValid())
        album.year = album.premiereDate.toUTC().date().year();
    album.genres = refs(json.value(QStringLiteral("GenreItems")));
    if (album.genres.isEmpty())
        album.genres = namesOnly(json.value(QStringLiteral("Genres")));
    for (const NamedRef &studio : refs(json.value(QStringLiteral("Studios"))))
        album.studios.append(studio.name);
    album.dateAdded = parseEmbyDate(text(json.value(QStringLiteral("DateCreated"))));
    album.trackCount = qMax(0, integer(json.value(QStringLiteral("ChildCount"))));
    album.runtimeMs = ticksToMs(json.value(QStringLiteral("RunTimeTicks")));
    if (album.runtimeMs == 0)
        album.runtimeMs = ticksToMs(json.value(QStringLiteral("CumulativeRunTimeTicks")));
    const UserDataFields data = userData(json);
    album.favourite = data.favourite;
    album.playCount = data.playCount;
    album.lastPlayed = data.lastPlayed;
    album.coverRef = ownPrimary(json);
    if (const auto type = serverReleaseType(json)) {
        album.releaseType = *type;
        album.releaseTypeFromServer = true;
    }
    return album;
}

QList<Album> parseAlbums(const QJsonArray &json)
{
    QList<Album> list;
    list.reserve(json.size());
    for (const QJsonValue &value : json)
        list.append(parseAlbum(value.toObject()));
    return list;
}

Artist parseArtist(const QJsonObject &json)
{
    Artist artist;
    artist.id = text(json.value(QStringLiteral("Id")));
    artist.name = text(json.value(QStringLiteral("Name")));
    artist.coverRef = ownPrimary(json);
    const QJsonArray backdrops = json.value(QStringLiteral("BackdropImageTags")).toArray();
    const QString backdrop = backdrops.isEmpty() ? QString() : text(backdrops.at(0));
    if (!backdrop.isEmpty())
        artist.backdropRef = {artist.id, QStringLiteral("Backdrop"), backdrop};
    artist.albumCount = qMax(0, integer(json.value(QStringLiteral("AlbumCount"))));
    if (artist.albumCount == 0)
        artist.albumCount = qMax(0, integer(json.value(QStringLiteral("ChildCount"))));
    artist.trackCount = qMax(0, integer(json.value(QStringLiteral("SongCount"))));
    artist.favourite = userData(json).favourite;
    return artist;
}

QList<Artist> parseArtists(const QJsonArray &json)
{
    QList<Artist> list;
    list.reserve(json.size());
    for (const QJsonValue &value : json)
        list.append(parseArtist(value.toObject()));
    return list;
}

GenreBin parseGenreBin(const QJsonObject &json)
{
    GenreBin genre;
    genre.id = text(json.value(QStringLiteral("Id")));
    genre.name = text(json.value(QStringLiteral("Name")));
    genre.recordCount = qMax(0, integer(json.value(QStringLiteral("AlbumCount"))));
    if (genre.recordCount == 0)
        genre.recordCount = qMax(0, integer(json.value(QStringLiteral("ChildCount"))));
    return genre;
}

Playlist parsePlaylist(const QJsonObject &json)
{
    Playlist playlist;
    playlist.id = text(json.value(QStringLiteral("Id")));
    playlist.name = text(json.value(QStringLiteral("Name")));
    playlist.trackCount = qMax(0, integer(json.value(QStringLiteral("ChildCount"))));
    playlist.runtimeMs = ticksToMs(json.value(QStringLiteral("CumulativeRunTimeTicks")));
    if (playlist.runtimeMs == 0)
        playlist.runtimeMs = ticksToMs(json.value(QStringLiteral("RunTimeTicks")));
    playlist.dateAdded = parseEmbyDate(text(json.value(QStringLiteral("DateCreated"))));
    playlist.coverRef = ownPrimary(json);
    return playlist;
}

QList<Playlist> parsePlaylists(const QJsonArray &json)
{
    QList<Playlist> list;
    list.reserve(json.size());
    for (const QJsonValue &value : json)
        list.append(parsePlaylist(value.toObject()));
    return list;
}
```

`text()` reads a numeric `Id` such as `12345` as `"12345"`: `toVariant().toString()` on a double prints it without a decimal point for integral values. The `trackParsesEverything` test covers that case.

- [x] **Step 5: Run the tests to verify they pass**

Run: `cmake --build --preset dev --target tst_music_mapper && ctest --preset dev -R tst_music_mapper --output-on-failure`
Expected: PASS.

- [x] **Step 6: Commit**

```bash
git add src/server/emby/EmbyMusicMapper.h src/server/emby/EmbyMusicMapper.cpp tests/unit/tst_music_mapper.cpp
git commit -m "feat(music): map Emby tracks, albums, artists, genres and playlists

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 6: Mapper, part 3: release classification, discs and album refinement

**Files:**
- Modify: `src/server/emby/EmbyMusicMapper.h`, `src/server/emby/EmbyMusicMapper.cpp`
- Test: `tests/unit/tst_music_mapper.cpp`

**Interfaces:**
- Consumes: Task 5 (`releaseTypeFromTag`, DTOs).
- Produces (namespace `strmqt::emby`):
  - `struct ReleaseEvidence { QString serverTag; QStringList albumArtistNames; int trackCount = 0; qint64 runtimeMs = 0; QStringList trackPrimaryArtists; }`
  - `music::ReleaseType classifyRelease(const ReleaseEvidence &evidence)`
  - `QList<music::Disc> groupDiscs(const QList<music::Track> &tracks)`
  - `QString dominantFormat(const QList<music::Track> &tracks)`
  - `void refineAlbumFromTracks(music::Album &album, const QList<music::Track> &tracks)`

- [x] **Step 1: Write the failing tests**

```cpp
    void classifyRelease_data();
    void classifyRelease();
    void discsGroupAndSum();
    void refineAlbumFromTracks();
```

```cpp
void MusicMapperTest::classifyRelease_data()
{
    QTest::addColumn<QString>("tag");
    QTest::addColumn<QStringList>("albumArtists");
    QTest::addColumn<int>("tracks");
    QTest::addColumn<qint64>("runtimeMs");
    QTest::addColumn<QStringList>("performers");
    QTest::addColumn<int>("expected");

    const qint64 min = 60'000;
    QTest::newRow("server tag wins") << "EP" << QStringList{"A"} << 12 << 60 * min
                                     << QStringList{"A"} << int(ReleaseType::EP);
    QTest::newRow("various artists") << "" << QStringList{"Various Artists"} << 20 << 70 * min
                                     << QStringList{"A", "B"} << int(ReleaseType::Compilation);
    QTest::newRow("four guests") << "" << QStringList{"DJ"} << 10 << 50 * min
                                 << QStringList{"A", "B", "C", "D"} << int(ReleaseType::Compilation);
    QTest::newRow("three guests is not") << "" << QStringList{"DJ"} << 10 << 50 * min
                                         << QStringList{"A", "B", "C"} << int(ReleaseType::Album);
    QTest::newRow("album artist among four") << "" << QStringList{"A"} << 10 << 50 * min
                                             << QStringList{"A", "B", "C", "D"} << int(ReleaseType::Album);
    QTest::newRow("single") << "" << QStringList{"A"} << 2 << 8 * min << QStringList{"A"}
                            << int(ReleaseType::Single);
    QTest::newRow("long 3-track is not single") << "" << QStringList{"A"} << 3 << 25 * min
                                                << QStringList{"A"} << int(ReleaseType::EP);
    QTest::newRow("ep") << "" << QStringList{"A"} << 6 << 24 * min << QStringList{"A"}
                        << int(ReleaseType::EP);
    QTest::newRow("unknown runtime stays album") << "" << QStringList{"A"} << 2 << qint64(0)
                                                 << QStringList{"A"} << int(ReleaseType::Album);
    QTest::newRow("album") << "" << QStringList{"A"} << 9 << 42 * min << QStringList{"A"}
                           << int(ReleaseType::Album);
}

void MusicMapperTest::classifyRelease()
{
    QFETCH(QString, tag);
    QFETCH(QStringList, albumArtists);
    QFETCH(int, tracks);
    QFETCH(qint64, runtimeMs);
    QFETCH(QStringList, performers);
    QFETCH(int, expected);
    const ReleaseEvidence evidence{tag, albumArtists, tracks, runtimeMs, performers};
    QCOMPARE(int(strmqt::emby::classifyRelease(evidence)), expected);
}

namespace {
Track makeTrack(QString id, int disc, int number, qint64 ms, QString codec, int depth, int rate,
                QString performer = QStringLiteral("A"))
{
    Track track;
    track.id = std::move(id);
    track.discNumber = disc;
    track.trackNumber = number;
    track.runtimeMs = ms;
    track.format = deriveAudioFormat(codec, depth, rate, 0, 2);
    track.artists = {{QString(), std::move(performer)}};
    return track;
}
} // namespace

void MusicMapperTest::discsGroupAndSum()
{
    const QList<Track> tracks = {
        makeTrack("a", 2, 1, 1000, "flac", 16, 44100),
        makeTrack("b", 1, 1, 2000, "flac", 16, 44100),
        makeTrack("c", 0, 2, 3000, "flac", 16, 44100), // unknown disc → disc 1
        makeTrack("d", 2, 2, 4000, "flac", 16, 44100),
    };
    const QList<Disc> discs = groupDiscs(tracks);
    QCOMPARE(discs.size(), 2);
    QCOMPARE(discs.at(0).number, 1);
    QCOMPARE(discs.at(0).tracks.size(), 2);
    QCOMPARE(discs.at(0).tracks.at(0).id, QStringLiteral("b")); // input order kept
    QCOMPARE(discs.at(0).runtimeMs, Q_INT64_C(5000));
    QCOMPARE(discs.at(1).number, 2);
    QCOMPARE(discs.at(1).runtimeMs, Q_INT64_C(5000));
    QVERIFY(groupDiscs({}).isEmpty());
}

void MusicMapperTest::refineAlbumFromTracks()
{
    Album album;
    album.albumArtists = {{QString(), QStringLiteral("A")}};
    const QList<Track> tracks = {
        makeTrack("1", 1, 1, 200'000, "flac", 24, 96000),
        makeTrack("2", 1, 2, 200'000, "flac", 24, 96000),
        makeTrack("3", 2, 1, 200'000, "mp3", 0, 44100),
    };
    strmqt::emby::refineAlbumFromTracks(album, tracks);
    QCOMPARE(album.trackCount, 3);
    QCOMPARE(album.runtimeMs, Q_INT64_C(600000));
    QCOMPARE(album.discCount, 2);
    QCOMPARE(album.formatSummary, QStringLiteral("FLAC 24/96"));
    QCOMPARE(album.releaseType, ReleaseType::Single); // 3 tracks, 10 min

    Album tagged;
    tagged.releaseType = ReleaseType::Compilation;
    tagged.releaseTypeFromServer = true;
    strmqt::emby::refineAlbumFromTracks(tagged, tracks);
    QCOMPARE(tagged.releaseType, ReleaseType::Compilation);
    QCOMPARE(dominantFormat({}), QString());
}
```

In `makeTrack`, the `"a"` literals convert to `QString` implicitly; this project does not define `QT_NO_CAST_FROM_ASCII`.

- [x] **Step 2: Run the tests to verify they fail**

Run: `cmake --build --preset dev --target tst_music_mapper`
Expected: a compile error (`ReleaseEvidence` is undeclared).

- [x] **Step 3: Extend the header**

```cpp
struct ReleaseEvidence
{
    QString serverTag;
    QStringList albumArtistNames;
    int trackCount = 0;
    qint64 runtimeMs = 0;
    QStringList trackPrimaryArtists; // first performer of each track
};

// Crate spec §3.3: server tag → "Various Artists" → ≥4 other performers →
// Single (1–3 tracks, <20 min) → EP (≤7 tracks, <35 min) → Album.
music::ReleaseType classifyRelease(const ReleaseEvidence &evidence);
QList<music::Disc> groupDiscs(const QList<music::Track> &tracks);
QString dominantFormat(const QList<music::Track> &tracks);
// Fills trackCount, runtimeMs, discCount and formatSummary from the tracks, and
// re-classifies the release unless the server supplied its type.
void refineAlbumFromTracks(music::Album &album, const QList<music::Track> &tracks);
```

- [x] **Step 4: Implement**

Add `#include <QHash>`, `#include <QMap>` and `#include <QSet>` to the `.cpp`, then:

```cpp
ReleaseType classifyRelease(const ReleaseEvidence &evidence)
{
    if (auto type = releaseTypeFromTag(evidence.serverTag))
        return *type;

    for (const QString &name : evidence.albumArtistNames) {
        if (name.compare(QLatin1String("Various Artists"), Qt::CaseInsensitive) == 0)
            return ReleaseType::Compilation;
    }

    QSet<QString> performers;
    for (const QString &name : evidence.trackPrimaryArtists) {
        if (!name.isEmpty())
            performers.insert(name.toLower());
    }
    bool albumArtistPerforms = false;
    for (const QString &name : evidence.albumArtistNames)
        albumArtistPerforms = albumArtistPerforms || performers.contains(name.toLower());
    if (performers.size() >= 4 && !albumArtistPerforms)
        return ReleaseType::Compilation;

    const qint64 minute = 60'000;
    if (evidence.runtimeMs > 0) {
        if (evidence.trackCount >= 1 && evidence.trackCount <= 3 && evidence.runtimeMs < 20 * minute)
            return ReleaseType::Single;
        if (evidence.trackCount >= 1 && evidence.trackCount <= 7 && evidence.runtimeMs < 35 * minute)
            return ReleaseType::EP;
    }
    return ReleaseType::Album;
}

QList<Disc> groupDiscs(const QList<Track> &tracks)
{
    QMap<int, Disc> byNumber;
    for (const Track &track : tracks) {
        const int number = track.discNumber > 0 ? track.discNumber : 1;
        Disc &disc = byNumber[number];
        disc.number = number;
        disc.runtimeMs += track.runtimeMs;
        disc.tracks.append(track);
    }
    return byNumber.values();
}

QString dominantFormat(const QList<Track> &tracks)
{
    QHash<QString, int> counts;
    QString best;
    int bestCount = 0;
    for (const Track &track : tracks) {
        if (track.format.badge.isEmpty())
            continue;
        const int count = ++counts[track.format.badge];
        if (count > bestCount) {
            best = track.format.badge;
            bestCount = count;
        }
    }
    return best;
}

void refineAlbumFromTracks(Album &album, const QList<Track> &tracks)
{
    if (tracks.isEmpty())
        return;
    album.trackCount = static_cast<int>(tracks.size());
    qint64 runtime = 0;
    QSet<int> discs;
    QStringList performers;
    for (const Track &track : tracks) {
        runtime += track.runtimeMs;
        discs.insert(track.discNumber > 0 ? track.discNumber : 1);
        if (!track.artists.isEmpty())
            performers.append(track.artists.first().name);
    }
    album.runtimeMs = runtime;
    album.discCount = static_cast<int>(discs.size());
    album.formatSummary = dominantFormat(tracks);
    if (album.releaseTypeFromServer)
        return;
    QStringList albumArtistNames;
    for (const NamedRef &artist : album.albumArtists)
        albumArtistNames.append(artist.name);
    album.releaseType = classifyRelease({QString(), albumArtistNames, album.trackCount,
                                         album.runtimeMs, performers});
}
```

`parseAlbum` (Task 5) keeps `ReleaseType::Album` when there is no server tag. Grids classify only from list evidence, so also add this at the end of `parseAlbum`, before `return album;`:

```cpp
    if (!album.releaseTypeFromServer) {
        QStringList names;
        for (const NamedRef &artist : album.albumArtists)
            names.append(artist.name);
        album.releaseType = classifyRelease({QString(), names, album.trackCount, album.runtimeMs, {}});
    }
```

With the album-list fields from Task 10 (`ChildCount`, `CumulativeRunTimeTicks`), a 2-track, 8-minute release shows "Single" in the grid before its sleeve is opened. The `albumParses` test from Task 5 still passes: its first album has 5 tracks and 44 minutes (Album), and its second has 0 tracks, so no count rule fires.

- [x] **Step 5: Run the tests to verify they pass**

Run: `cmake --build --preset dev --target tst_music_mapper && ctest --preset dev -R tst_music_mapper --output-on-failure`
Expected: PASS.

- [x] **Step 6: Commit**

```bash
git add src/server/emby/EmbyMusicMapper.h src/server/emby/EmbyMusicMapper.cpp tests/unit/tst_music_mapper.cpp
git commit -m "feat(music): classify releases and group album discs

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 7: `MockEmbyServer::addQueryRoute`

The repository sends several requests to the same path (`/Users/{uid}/Items`) that differ only by query. Tests need to answer each one differently.

**Files:**
- Modify: `tests/mocks/MockEmbyServer.h`, `tests/mocks/MockEmbyServer.cpp`
- Test: `tests/integration/tst_emby_client.cpp`

**Interfaces:**
- Produces: `void MockEmbyServer::addQueryRoute(const QString &method, const QString &path, const QList<QPair<QString, QString>> &required, int status, const QByteArray &body)`. A request matches when every `required` key has exactly that (fully decoded) value. More specific routes (more required keys) win. A matching query route takes priority over queued, gated and static routes on the same path. `addQueryRoute` with the same method, path and required set replaces the earlier body.

- [x] **Step 1: Write the failing test**

Add a slot `void queryRoutesMatchMostSpecific();` to `tst_emby_client.cpp`:

```cpp
void EmbyClientTest::queryRoutesMatchMostSpecific()
{
    const QString path = QStringLiteral("/Users/%1/Items").arg(kUserId);
    m_mock->addRoute(QStringLiteral("GET"), path, 200, R"({"Items":[],"TotalRecordCount":0})");
    m_mock->addQueryRoute(QStringLiteral("GET"), path,
                          {{QStringLiteral("IncludeItemTypes"), QStringLiteral("MusicAlbum")}}, 200,
                          R"({"Items":[{"Id":"album"}],"TotalRecordCount":1})");
    m_mock->addQueryRoute(QStringLiteral("GET"), path,
                          {{QStringLiteral("IncludeItemTypes"), QStringLiteral("MusicAlbum")},
                           {QStringLiteral("Filters"), QStringLiteral("IsFavorite")}},
                          200, R"({"Items":[{"Id":"fav"}],"TotalRecordCount":1})");
    m_client->setSession(kToken, kUserId);

    auto firstId = [&](QList<QPair<QString, QString>> items) {
        QUrlQuery query;
        query.setQueryItems(items);
        const auto result = waitFor(m_client->getJson(QStringLiteral("/Users/{uid}/Items"), query));
        const QJsonArray array = result.value.object().value(QStringLiteral("Items")).toArray();
        return array.isEmpty() ? QString() : array.at(0).toObject().value(QStringLiteral("Id")).toString();
    };

    QCOMPARE(firstId({{QStringLiteral("IncludeItemTypes"), QStringLiteral("MusicAlbum")},
                      {QStringLiteral("Limit"), QStringLiteral("5")}}),
             QStringLiteral("album"));
    QCOMPARE(firstId({{QStringLiteral("Filters"), QStringLiteral("IsFavorite")},
                      {QStringLiteral("IncludeItemTypes"), QStringLiteral("MusicAlbum")}}),
             QStringLiteral("fav"));
    QCOMPARE(firstId({{QStringLiteral("IncludeItemTypes"), QStringLiteral("Audio")}}), QString());
}
```

- [x] **Step 2: Run the test to verify it fails**

Run: `cmake --build --preset dev --target tst_emby_client`
Expected: a compile error (`addQueryRoute` is not a member).

- [x] **Step 3: Implement**

In `MockEmbyServer.h`, add `#include <QPair>` and declare the method public, after `addFieldsGatedRoute`:

```cpp
    // Answers only requests whose query carries every `required` Key=Value
    // (fully decoded). The most specific match wins, ahead of queued, gated
    // and plain routes on the same path.
    void addQueryRoute(const QString &method, const QString &path,
                       const QList<QPair<QString, QString>> &required, int status,
                       const QByteArray &body);
```

Add this to the private section, next to `m_fieldsGatedRoutes`:

```cpp
    struct QueryRoute
    {
        QList<QPair<QString, QString>> required;
        int status = 200;
        QByteArray body;
    };
    QHash<QString, QList<QueryRoute>> m_queryRoutes; // key: "METHOD /path"
```

In `MockEmbyServer.cpp`, add `#include <algorithm>` and:

```cpp
void MockEmbyServer::addQueryRoute(const QString &method, const QString &path,
                                   const QList<QPair<QString, QString>> &required, int status,
                                   const QByteArray &body)
{
    const QString key = method.toUpper() + QLatin1Char(' ') + path;
    QList<QueryRoute> &routes = m_queryRoutes[key];
    routes.removeIf([&](const QueryRoute &route) { return route.required == required; });
    routes.append(QueryRoute{required, status, body});
    std::stable_sort(routes.begin(), routes.end(), [](const QueryRoute &a, const QueryRoute &b) {
        return a.required.size() > b.required.size();
    });
}
```

In `handleConnection`, directly after `const QString key = request.method + QLatin1Char(' ') + request.path;`, find the matching query route and give it priority. Replace the `if (!m_queuedRoutes.value(key).isEmpty() || m_routes.contains(key) || m_fieldsGatedRoutes.contains(key)) {` condition and its first branch like this:

```cpp
            const QueryRoute *queryRoute = nullptr;
            if (const auto it = m_queryRoutes.constFind(key); it != m_queryRoutes.cend()) {
                const QUrlQuery received(request.query);
                for (const QueryRoute &candidate : *it) {
                    const bool matches = std::all_of(
                        candidate.required.cbegin(), candidate.required.cend(),
                        [&](const QPair<QString, QString> &pair) {
                            return received.hasQueryItem(pair.first)
                                   && received.queryItemValue(pair.first, QUrl::FullyDecoded) == pair.second;
                        });
                    if (matches) {
                        queryRoute = &candidate;
                        break;
                    }
                }
            }
            QByteArray response;
            int delayMs = 0;
            if (queryRoute || !m_queuedRoutes.value(key).isEmpty() || m_routes.contains(key) ||
                m_fieldsGatedRoutes.contains(key)) {
                Route route;
                if (queryRoute) {
                    route = Route{queryRoute->status, queryRoute->body, "application/json", 0, false};
                } else if (!m_queuedRoutes.value(key).isEmpty()) {
```

The rest of the chain (`else if (m_fieldsGatedRoutes…)`, `else`) is unchanged. Delete the original `QByteArray response;` and `int delayMs = 0;` lines above the old `if` so they are not declared twice. `Route`'s field order is `{status, body, contentType, delayMs, chunked}`. If `contentType` is a `QByteArray` in this file, the literal converts implicitly.

- [x] **Step 4: Run the tests to verify they pass**

Run: `cmake --build --preset dev && ctest --preset dev -R 'tst_emby_client|tst_item_actions|tst_home|tst_library' --output-on-failure`
Expected: PASS (the new test passes and the existing mock users are unaffected).

- [x] **Step 5: Commit**

```bash
git add tests/mocks/MockEmbyServer.h tests/mocks/MockEmbyServer.cpp tests/integration/tst_emby_client.cpp
git commit -m "test(mock): route Emby requests by query parameters

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 8: `TtlCache` and `Fanout`

**Files:**
- Create: `src/app/music/TtlCache.h`, `src/app/music/Fanout.h`
- Modify: `src/CMakeLists.txt` (`strmqt_app`)
- Test: `tests/unit/tst_music_cache.cpp`, `tests/CMakeLists.txt`

**Interfaces:**
- Produces (namespace `strmqt::music`):
  - `template<class T> class TtlCache`:
    - `explicit TtlCache(std::chrono::milliseconds ttl)`, where a negative ttl means entries never expire
    - `std::optional<T> get(const QString &key, const QDateTime &now) const`
    - `void put(const QString &key, T value, const QDateTime &now)`
    - `void markStale(const QString &prefix = {})`
    - `void remove(const QString &key)`
    - `template<class Pred> void removeIf(Pred pred)`, where `pred(const QString &key, const T &value) → bool`
    - `void clear()`, `int size() const`
  - `class Fanout`:
    - `static std::shared_ptr<Fanout> create(QObject *context, std::function<void()> done)`
    - `template<class T> void add(QFuture<Result<T>> future, std::function<void(Result<T>)> sink)`
    - `void seal()`

  `done` runs exactly once, queued on `context`'s thread, after `seal()` has been called and every added future has delivered to its sink. A cancelled future delivers `Result<T>::failure("cancelled")`. If `context` is destroyed first, nothing runs.

- [x] **Step 1: Write the failing tests**

`tests/unit/tst_music_cache.cpp`:

```cpp
#include <QPromise>
#include <QtTest>

#include "app/music/Fanout.h"
#include "app/music/TtlCache.h"

using namespace strmqt;
using namespace strmqt::music;
using namespace std::chrono_literals;

class MusicCacheTest : public QObject
{
    Q_OBJECT

private slots:
    void cacheExpiresAndGoesStale();
    void cacheRemovesByPredicate();
    void fanoutWaitsForAllAndSeal();
    void fanoutEmptyFiresOnSeal();
    void fanoutReportsCancellation();
    void fanoutDropsWhenContextDies();
};

void MusicCacheTest::cacheExpiresAndGoesStale()
{
    const QDateTime t0 = QDateTime::fromSecsSinceEpoch(1'000'000);
    TtlCache<int> cache(5min);
    cache.put(QStringLiteral("home:recent"), 1, t0);
    cache.put(QStringLiteral("sleeve:a"), 2, t0);
    QCOMPARE(cache.get(QStringLiteral("home:recent"), t0.addSecs(299)), std::optional(1));
    QVERIFY(!cache.get(QStringLiteral("home:recent"), t0.addSecs(301)).has_value());

    cache.markStale(QStringLiteral("home:"));
    QVERIFY(!cache.get(QStringLiteral("home:recent"), t0).has_value());
    QCOMPARE(cache.get(QStringLiteral("sleeve:a"), t0), std::optional(2));
    cache.put(QStringLiteral("home:recent"), 3, t0); // a fresh put clears staleness
    QCOMPARE(cache.get(QStringLiteral("home:recent"), t0), std::optional(3));

    cache.markStale();
    QVERIFY(!cache.get(QStringLiteral("sleeve:a"), t0).has_value());

    TtlCache<int> session(-1ms);
    session.put(QStringLiteral("k"), 9, t0);
    QCOMPARE(session.get(QStringLiteral("k"), t0.addYears(1)), std::optional(9));
    session.clear();
    QCOMPARE(session.size(), 0);
}

void MusicCacheTest::cacheRemovesByPredicate()
{
    const QDateTime t0 = QDateTime::currentDateTimeUtc();
    TtlCache<QStringList> cache(10min);
    cache.put(QStringLiteral("a"), {QStringLiteral("t1"), QStringLiteral("t2")}, t0);
    cache.put(QStringLiteral("b"), {QStringLiteral("t3")}, t0);
    cache.removeIf([](const QString &, const QStringList &ids) {
        return ids.contains(QStringLiteral("t2"));
    });
    QCOMPARE(cache.size(), 1);
    QVERIFY(cache.get(QStringLiteral("b"), t0).has_value());
    cache.remove(QStringLiteral("b"));
    QCOMPARE(cache.size(), 0);
}

void MusicCacheTest::fanoutWaitsForAllAndSeal()
{
    QObject context;
    QPromise<Result<int>> first;
    QPromise<Result<QString>> second;
    first.start();
    second.start();
    int total = 0;
    QString text;
    int doneCount = 0;

    auto fan = Fanout::create(&context, [&] { ++doneCount; });
    fan->add<int>(first.future(), [&](Result<int> r) { total = r.value; });
    fan->add<QString>(second.future(), [&](Result<QString> r) { text = r.error; });
    fan->seal();

    first.addResult(Result<int>::success(4));
    first.finish();
    QTest::qWait(20);
    QCOMPARE(doneCount, 0);
    second.addResult(Result<QString>::failure(QStringLiteral("boom")));
    second.finish();
    QTRY_COMPARE(doneCount, 1);
    QCOMPARE(total, 4);
    QCOMPARE(text, QStringLiteral("boom"));
    QTest::qWait(20);
    QCOMPARE(doneCount, 1);
}

void MusicCacheTest::fanoutEmptyFiresOnSeal()
{
    QObject context;
    int doneCount = 0;
    auto fan = Fanout::create(&context, [&] { ++doneCount; });
    QCOMPARE(doneCount, 0);
    fan->seal();
    QCOMPARE(doneCount, 0); // always queued, never re-entrant
    QTRY_COMPARE(doneCount, 1);
}

void MusicCacheTest::fanoutReportsCancellation()
{
    QObject context;
    QString error;
    bool done = false;
    auto fan = Fanout::create(&context, [&] { done = true; });
    {
        QPromise<Result<int>> promise;
        promise.start();
        fan->add<int>(promise.future(), [&](Result<int> r) { error = r.error; });
        promise.future().cancel();
        promise.finish();
    }
    fan->seal();
    QTRY_VERIFY(done);
    QCOMPARE(error, QStringLiteral("cancelled"));
}

void MusicCacheTest::fanoutDropsWhenContextDies()
{
    bool done = false;
    QPromise<Result<int>> promise;
    promise.start();
    {
        QObject context;
        auto fan = Fanout::create(&context, [&] { done = true; });
        fan->add<int>(promise.future(), [](Result<int>) {});
        fan->seal();
    }
    promise.addResult(Result<int>::success(1));
    promise.finish();
    QTest::qWait(50);
    QVERIFY(!done);
}

QTEST_GUILESS_MAIN(MusicCacheTest)
#include "tst_music_cache.moc"
```

Register it in `tests/CMakeLists.txt` after `tst_music_mapper`:

```cmake
strmqt_add_test(tst_music_cache unit/tst_music_cache.cpp)
```

- [x] **Step 2: Run the tests to verify they fail**

Run: `cmake --preset dev && cmake --build --preset dev --target tst_music_cache`
Expected: a compile error (`app/music/Fanout.h` not found).

- [x] **Step 3: Write `TtlCache.h`**

```cpp
#pragma once

#include <QDateTime>
#include <QHash>
#include <QString>

#include <chrono>
#include <optional>

namespace strmqt::music {

// Per-account in-memory cache with a time-to-live and explicit staleness
// (Crate spec §3.5). Keys are "<kind>:<id>" so markStale("home:") invalidates
// one family. A negative TTL never expires (session caches).
template<class T>
class TtlCache
{
public:
    explicit TtlCache(std::chrono::milliseconds ttl) : m_ttl(ttl) {}

    std::optional<T> get(const QString &key, const QDateTime &now) const
    {
        const auto it = m_entries.constFind(key);
        if (it == m_entries.cend() || it->stale)
            return std::nullopt;
        if (m_ttl.count() >= 0 && it->storedAt.msecsTo(now) > m_ttl.count())
            return std::nullopt;
        return it->value;
    }

    void put(const QString &key, T value, const QDateTime &now)
    {
        m_entries.insert(key, Entry{std::move(value), now, false});
    }

    void markStale(const QString &prefix = {})
    {
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
            if (prefix.isEmpty() || it.key().startsWith(prefix))
                it->stale = true;
        }
    }

    void remove(const QString &key) { m_entries.remove(key); }

    template<class Pred>
    void removeIf(Pred pred)
    {
        for (auto it = m_entries.begin(); it != m_entries.end();) {
            if (pred(it.key(), it->value))
                it = m_entries.erase(it);
            else
                ++it;
        }
    }

    void clear() { m_entries.clear(); }
    int size() const { return static_cast<int>(m_entries.size()); }

private:
    struct Entry
    {
        T value;
        QDateTime storedAt;
        bool stale = false;
    };
    QHash<QString, Entry> m_entries;
    std::chrono::milliseconds m_ttl;
};

} // namespace strmqt::music
```

- [x] **Step 4: Write `Fanout.h`**

```cpp
#pragma once

#include <QFuture>
#include <QMetaObject>
#include <QObject>
#include <QPointer>

#include <functional>
#include <memory>

#include "core/Result.h"

namespace strmqt::music {

// Parallel request join (Crate spec §3.5). Each add() delivers its result to a
// sink on the context's thread; done runs once, queued, after seal() and the
// last delivery. Partial failure is the sinks' business: Fanout never fails.
class Fanout : public std::enable_shared_from_this<Fanout>
{
public:
    static std::shared_ptr<Fanout> create(QObject *context, std::function<void()> done)
    {
        return std::shared_ptr<Fanout>(new Fanout(context, std::move(done)));
    }

    template<class T>
    void add(QFuture<Result<T>> future, std::function<void(Result<T>)> sink)
    {
        if (!m_context)
            return;
        ++m_pending;
        auto self = shared_from_this();
        auto delivered = std::make_shared<bool>(false);
        future
            .then(m_context.data(),
                  [self, sink, delivered](Result<T> result) {
                      *delivered = true;
                      sink(std::move(result));
                      self->finishOne();
                  })
            .onCanceled(m_context.data(), [self, sink, delivered] {
                if (*delivered)
                    return;
                *delivered = true;
                sink(Result<T>::failure(QStringLiteral("cancelled")));
                self->finishOne();
            });
    }

    void seal()
    {
        m_sealed = true;
        maybeFire();
    }

private:
    Fanout(QObject *context, std::function<void()> done)
        : m_context(context), m_done(std::move(done))
    {
    }

    void finishOne()
    {
        --m_pending;
        maybeFire();
    }

    void maybeFire()
    {
        if (!m_sealed || m_pending > 0 || m_fired || !m_context)
            return;
        m_fired = true;
        auto self = shared_from_this();
        QMetaObject::invokeMethod(
            m_context.data(), [self] { self->m_done(); }, Qt::QueuedConnection);
    }

    QPointer<QObject> m_context;
    std::function<void()> m_done;
    int m_pending = 0;
    bool m_sealed = false;
    bool m_fired = false;
};

} // namespace strmqt::music
```

Add the headers to `strmqt_app` in `src/CMakeLists.txt`, after `app/PlayQueue.h app/PlayQueue.cpp`:

```cmake
    app/music/TtlCache.h
    app/music/Fanout.h
```

- [x] **Step 5: Run the tests to verify they pass**

Run: `cmake --build --preset dev --target tst_music_cache && ctest --preset dev -R tst_music_cache --output-on-failure`
Expected: PASS (6 tests). If `fanoutReportsCancellation` delivers a default value instead of "cancelled", Qt ran the `.then` continuation on a cancelled future that already held no result. Change the `.then` lambda to take `QFuture<Result<T>> f` and deliver `failure("cancelled")` when `f.isCanceled() || f.resultCount() == 0`, otherwise `f.result()`. Keep that version.

- [x] **Step 6: Commit**

```bash
git add src/app/music/TtlCache.h src/app/music/Fanout.h src/CMakeLists.txt tests/unit/tst_music_cache.cpp tests/CMakeLists.txt
git commit -m "feat(music): add TTL cache and request fan-out helpers

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 9: `MusicQueryTranslator` and `MusicFormat`

**Files:**
- Create: `src/app/music/MusicQueryTranslator.h/.cpp`, `src/app/music/MusicFormat.h/.cpp`
- Modify: `src/CMakeLists.txt` (`strmqt_app`)
- Test: `tests/unit/tst_music_query_translator.cpp`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `MusicQuery`, `Section`, `FormatFilter` (Task 3); `ItemsQuery` axes (Task 1); `caps::*` (Task 2); `embyImageSource` (`app/models/MediaItemModel.h`).
- Produces, all in namespace `strmqt::music`:
  - `struct SortKey { QString key; QString label; bool defaultDescending; }`
  - `struct LetterRange { QString greaterOrEqual; QString lessThan; }`
  - `namespace MusicQueryTranslator`:
    - `QList<SortKey> sortKeysFor(Section)`
    - `QString sortFields(Section, const QString &key)`: the Emby `SortBy` value, empty for client-sorted keys
    - `LetterRange letterRange(const QString &letter)`
    - `QStringList codecsFor(FormatFilter)`
    - `bool formatFilterable(Section)`
    - `QList<FormatFilter> formatOptions(Section)`
    - `ItemsQuery toItemsQuery(const MusicQuery &query, int startIndex, int limit)`
  - Formatting functions:
    - `QString formatDuration(qint64 ms)`: "3:07", "1:02:03"; "" for ≤ 0
    - `QString formatRuntime(qint64 ms)`: "48 min", "1 h 12 min"; "" for ≤ 0
    - `QString formatRecordCount(int)`: "1 record", "12 records"
    - `QString formatTrackCount(int)`: "1 track", "9 tracks"
    - `QString joinNames(const QList<NamedRef> &)`: "A", "A & B", "A, B & C"
    - `QString coverUrl(const ImageRef &)`: `embyImageSource(ref.itemId, ref.imageType, ref.tag)`, or "" when the ref is invalid

Sort tables (spec §5.3). Every key is lower-case and stable; the UI stores keys, never labels.

| Section | key → SortBy (default direction) |
|---|---|
| Albums | `name` → `SortName` (asc); `artist` → `AlbumArtist,SortName` (asc); `year` → `ProductionYear,PremiereDate,SortName` (desc); `added` → `DateCreated` (desc); `plays` → `PlayCount` (desc; **only when `caps::kAlbumPlayCountSort`**); `random` → `Random` |
| Artists | `name` → `SortName`; `plays` → `PlayCount` (desc); `added` → `DateCreated` (desc); `random` → `Random` |
| Songs | `name` → `SortName`; `artist` → `AlbumArtist,Album,ParentIndexNumber,IndexNumber,SortName`; `album` → `Album,ParentIndexNumber,IndexNumber,SortName`; `added` → `DateCreated` (desc); `plays` → `PlayCount` (desc); `duration` → `Runtime` (desc); `random` → `Random` |
| Genres | `size` (client-side, desc); `name` (client-side, asc) |
| Playlists | `name` → `SortName`; `added` → `DateCreated` (desc) |

- [x] **Step 1: Write the failing tests**

`tests/unit/tst_music_query_translator.cpp`:

```cpp
#include <QtTest>

#include "app/models/MediaItemModel.h"
#include "app/music/MusicFormat.h"
#include "app/music/MusicQueryTranslator.h"
#include "server/emby/MusicServerCapabilities.h"

using namespace strmqt;
using namespace strmqt::music;
namespace T = strmqt::music::MusicQueryTranslator;

class MusicQueryTranslatorTest : public QObject
{
    Q_OBJECT

private slots:
    void sortTables();
    void letterRanges();
    void albumsQuery();
    void songsQueryWithFilters();
    void decadeTranslation();
    void formatOnlyWhereFilterable();
    void letterOnlyForNameSort();
    void formatting();
};

void MusicQueryTranslatorTest::sortTables()
{
    QStringList albumKeys;
    for (const SortKey &key : T::sortKeysFor(Section::Albums))
        albumKeys.append(key.key);
    QStringList expected{"name", "artist", "year", "added"};
    if (emby::caps::kAlbumPlayCountSort)
        expected.append(QStringLiteral("plays"));
    expected.append(QStringLiteral("random"));
    QCOMPARE(albumKeys, expected);

    QCOMPARE(T::sortFields(Section::Songs, QStringLiteral("album")),
             QStringLiteral("Album,ParentIndexNumber,IndexNumber,SortName"));
    QCOMPARE(T::sortFields(Section::Genres, QStringLiteral("size")), QString());
    QCOMPARE(T::sortFields(Section::Albums, QStringLiteral("bogus")), QStringLiteral("SortName"));
    QVERIFY(T::sortKeysFor(Section::Albums).at(2).defaultDescending); // year
    QCOMPARE(T::sortKeysFor(Section::Playlists).size(), 2);
}

void MusicQueryTranslatorTest::letterRanges()
{
    QCOMPARE(T::letterRange(QStringLiteral("C")).greaterOrEqual, QStringLiteral("C"));
    QCOMPARE(T::letterRange(QStringLiteral("C")).lessThan, QStringLiteral("D"));
    QCOMPARE(T::letterRange(QStringLiteral("#")).greaterOrEqual, QString());
    QCOMPARE(T::letterRange(QStringLiteral("#")).lessThan, QStringLiteral("A"));
    QCOMPARE(T::letterRange(QStringLiteral("Z")).greaterOrEqual, QStringLiteral("Z"));
    QCOMPARE(T::letterRange(QStringLiteral("Z")).lessThan, QString());
    QCOMPARE(T::letterRange(QString()).lessThan, QString());
}

void MusicQueryTranslatorTest::albumsQuery()
{
    MusicQuery query;
    query.libraryId = QStringLiteral("1868998");
    const ItemsQuery items = T::toItemsQuery(query, 100, 50);
    QCOMPARE(items.parentId, QStringLiteral("1868998"));
    QCOMPARE(items.includeItemTypes, QStringList{QStringLiteral("MusicAlbum")});
    QVERIFY(items.recursive);
    QCOMPARE(items.sortBy, QStringLiteral("SortName"));
    QVERIFY(!items.sortDescending);
    QCOMPARE(items.startIndex, 100);
    QCOMPARE(items.limit, 50);
    QVERIFY(items.fields.contains(QStringLiteral("ChildCount")));
    QVERIFY(items.filters.isEmpty());
}

void MusicQueryTranslatorTest::songsQueryWithFilters()
{
    MusicQuery query;
    query.libraryId = QStringLiteral("L");
    query.section = Section::Songs;
    query.sortKey = QStringLiteral("plays");
    query.descending = true;
    query.genreIds = {QStringLiteral("g1"), QStringLiteral("g2")};
    query.favouritesOnly = true;
    query.unplayedOnly = true;
    const ItemsQuery items = T::toItemsQuery(query, 0, 100);
    QCOMPARE(items.includeItemTypes, QStringList{QStringLiteral("Audio")});
    QCOMPARE(items.sortBy, QStringLiteral("PlayCount"));
    QVERIFY(items.sortDescending);
    QCOMPARE(items.genreIds, query.genreIds);
    QCOMPARE(items.filters, QStringList({QStringLiteral("IsFavorite"), QStringLiteral("IsUnplayed")}));
    QVERIFY(items.fields.contains(QStringLiteral("MediaStreams")));
}

void MusicQueryTranslatorTest::decadeTranslation()
{
    MusicQuery query;
    query.decade = 1970;
    const ItemsQuery items = T::toItemsQuery(query, 0, 10);
    if (emby::caps::kYearsAcceptsDecade) {
        QCOMPARE(items.years.size(), 10);
        QCOMPARE(items.years.first(), 1970);
        QCOMPARE(items.years.last(), 1979);
        QVERIFY(items.minPremiereDate.isEmpty());
    } else {
        QVERIFY(items.years.isEmpty());
        QCOMPARE(items.minPremiereDate, QStringLiteral("1970-01-01T00:00:00Z"));
        QCOMPARE(items.maxPremiereDate, QStringLiteral("1979-12-31T23:59:59Z"));
    }
    query.decade = kDecadeEarlier;
    const ItemsQuery earlier = T::toItemsQuery(query, 0, 10);
    QVERIFY(earlier.years.isEmpty());
    QCOMPARE(earlier.maxPremiereDate, QStringLiteral("1949-12-31T23:59:59Z"));
}

void MusicQueryTranslatorTest::formatOnlyWhereFilterable()
{
    MusicQuery query;
    query.format = FormatFilter::Lossless;
    query.section = Section::Songs;
    const ItemsQuery songs = T::toItemsQuery(query, 0, 10);
    QCOMPARE(!songs.audioCodecs.isEmpty(), emby::caps::kAudioCodecsFiltersAudio);
    if (!songs.audioCodecs.isEmpty())
        QVERIFY(songs.audioCodecs.contains(QStringLiteral("flac")));
    query.section = Section::Albums;
    QCOMPARE(!T::toItemsQuery(query, 0, 10).audioCodecs.isEmpty(), emby::caps::kAudioCodecsFiltersAlbums);
    QVERIFY(T::formatOptions(Section::Artists).isEmpty());
    QCOMPARE(T::formatOptions(Section::Songs).contains(FormatFilter::HiRes),
             emby::caps::kAudioCodecsFiltersAudio && emby::caps::kHiResFilter);
    QVERIFY(T::codecsFor(FormatFilter::Lossy).contains(QStringLiteral("mp3")));
    QVERIFY(T::codecsFor(FormatFilter::Any).isEmpty());
}

void MusicQueryTranslatorTest::letterOnlyForNameSort()
{
    MusicQuery query;
    query.letter = QStringLiteral("M");
    ItemsQuery items = T::toItemsQuery(query, 0, 10);
    QCOMPARE(items.nameStartsWithOrGreater, QStringLiteral("M"));
    QCOMPARE(items.nameLessThan, QStringLiteral("N"));
    query.sortKey = QStringLiteral("added");
    items = T::toItemsQuery(query, 0, 10);
    QVERIFY(items.nameStartsWithOrGreater.isEmpty());
    QVERIFY(items.nameLessThan.isEmpty());
    QVERIFY(items.nameStartsWith.isEmpty()); // never the slow LIKE form
}

void MusicQueryTranslatorTest::formatting()
{
    QCOMPARE(formatDuration(187'000), QStringLiteral("3:07"));
    QCOMPARE(formatDuration(3'723'000), QStringLiteral("1:02:03"));
    QCOMPARE(formatDuration(0), QString());
    QCOMPARE(formatRuntime(48 * 60'000 + 20'000), QStringLiteral("48 min"));
    QCOMPARE(formatRuntime(72 * 60'000), QStringLiteral("1 h 12 min"));
    QCOMPARE(formatRuntime(120 * 60'000), QStringLiteral("2 h"));
    QCOMPARE(formatRuntime(20'000), QStringLiteral("1 min"));
    QCOMPARE(formatRecordCount(1), QStringLiteral("1 record"));
    QCOMPARE(formatRecordCount(12), QStringLiteral("12 records"));
    QCOMPARE(formatTrackCount(9), QStringLiteral("9 tracks"));
    QCOMPARE(joinNames({{"1", "A"}}), QStringLiteral("A"));
    QCOMPARE(joinNames({{"1", "A"}, {"2", "B"}}), QStringLiteral("A & B"));
    QCOMPARE(joinNames({{"1", "A"}, {"2", "B"}, {"3", "C"}}), QStringLiteral("A, B & C"));
    setEmbyImageSourceNamespace(QStringLiteral("test"));
    QCOMPARE(coverUrl({QStringLiteral("al1"), QStringLiteral("Primary"), QStringLiteral("t")}),
             QStringLiteral("image://emby/test/al1/Primary/t"));
    QCOMPARE(coverUrl({}), QString());
}

QTEST_GUILESS_MAIN(MusicQueryTranslatorTest)
#include "tst_music_query_translator.moc"
```

Register:

```cmake
strmqt_add_test(tst_music_query_translator unit/tst_music_query_translator.cpp)
```

- [x] **Step 2: Run the tests to verify they fail**

Run: `cmake --preset dev && cmake --build --preset dev --target tst_music_query_translator`
Expected: a compile error (headers not found).

- [x] **Step 3: Write `MusicFormat.h/.cpp`**

```cpp
#pragma once

#include <QString>

#include "server/dto/music/MusicTypes.h"

// Display strings for the music UI. QML never formats (Crate plan, Global Constraints).
namespace strmqt::music {

QString formatDuration(qint64 ms);
QString formatRuntime(qint64 ms);
QString formatRecordCount(int count);
QString formatTrackCount(int count);
QString joinNames(const QList<NamedRef> &names);
QString coverUrl(const ImageRef &ref);

} // namespace strmqt::music
```

```cpp
#include "app/music/MusicFormat.h"

#include "app/models/MediaItemModel.h"

namespace strmqt::music {

QString formatDuration(qint64 ms)
{
    if (ms <= 0)
        return {};
    const qint64 total = (ms + 500) / 1000;
    const qint64 hours = total / 3600;
    const qint64 minutes = (total % 3600) / 60;
    const qint64 seconds = total % 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3").arg(hours).arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
}

QString formatRuntime(qint64 ms)
{
    if (ms <= 0)
        return {};
    const qint64 minutes = qMax<qint64>(1, (ms + 30'000) / 60'000);
    if (minutes < 60)
        return QStringLiteral("%1 min").arg(minutes);
    if (minutes % 60 == 0)
        return QStringLiteral("%1 h").arg(minutes / 60);
    return QStringLiteral("%1 h %2 min").arg(minutes / 60).arg(minutes % 60);
}

QString formatRecordCount(int count)
{
    return count == 1 ? QStringLiteral("1 record") : QStringLiteral("%1 records").arg(count);
}

QString formatTrackCount(int count)
{
    return count == 1 ? QStringLiteral("1 track") : QStringLiteral("%1 tracks").arg(count);
}

QString joinNames(const QList<NamedRef> &names)
{
    QStringList list;
    for (const NamedRef &ref : names) {
        if (!ref.name.isEmpty())
            list.append(ref.name);
    }
    if (list.size() <= 1)
        return list.value(0);
    const QString last = list.takeLast();
    return list.join(QStringLiteral(", ")) + QStringLiteral(" & ") + last;
}

QString coverUrl(const ImageRef &ref)
{
    if (!ref.isValid())
        return {};
    return embyImageSource(ref.itemId, ref.imageType.isEmpty() ? QStringLiteral("Primary") : ref.imageType,
                           ref.tag);
}

} // namespace strmqt::music
```

`formatRuntime(48 min 20 s)` rounds to 48 min; `formatRuntime(20 s)` floors to "1 min" so a short track never reads as "0 min".

- [x] **Step 4: Write `MusicQueryTranslator.h/.cpp`**

```cpp
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "server/dto/ItemsQuery.h"
#include "server/dto/music/MusicQuery.h"

// MusicQuery (the UI's words) → ItemsQuery (Emby's), per Crate spec §5.2–5.3 and
// the measured capabilities in server/emby/MusicServerCapabilities.h.
namespace strmqt::music {

struct SortKey
{
    QString key;
    QString label;
    bool defaultDescending = false;
};

struct LetterRange
{
    QString greaterOrEqual;
    QString lessThan;
};

namespace MusicQueryTranslator {

QList<SortKey> sortKeysFor(Section section);
QString sortFields(Section section, const QString &key);
LetterRange letterRange(const QString &letter);
QStringList codecsFor(FormatFilter format);
bool formatFilterable(Section section);
QList<FormatFilter> formatOptions(Section section);
ItemsQuery toItemsQuery(const MusicQuery &query, int startIndex, int limit);

} // namespace MusicQueryTranslator
} // namespace strmqt::music
```

```cpp
#include "app/music/MusicQueryTranslator.h"

#include "server/emby/MusicServerCapabilities.h"

namespace strmqt::music::MusicQueryTranslator {

namespace {

struct SortRow
{
    SortKey key;
    QString fields; // empty: sorted client-side
};

QList<SortRow> rows(Section section)
{
    auto row = [](const char *key, const char *label, bool desc, const char *fields) {
        return SortRow{{QString::fromLatin1(key), QString::fromLatin1(label), desc},
                       QString::fromLatin1(fields)};
    };
    switch (section) {
    case Section::Albums: {
        QList<SortRow> list{row("name", "Name", false, "SortName"),
                            row("artist", "Artist", false, "AlbumArtist,SortName"),
                            row("year", "Year", true, "ProductionYear,PremiereDate,SortName"),
                            row("added", "Date added", true, "DateCreated")};
        if (emby::caps::kAlbumPlayCountSort)
            list.append(row("plays", "Most played", true, "PlayCount"));
        list.append(row("random", "Random", false, "Random"));
        return list;
    }
    case Section::Artists:
        return {row("name", "Name", false, "SortName"), row("plays", "Most played", true, "PlayCount"),
                row("added", "Date added", true, "DateCreated"), row("random", "Random", false, "Random")};
    case Section::Songs:
        return {row("name", "Name", false, "SortName"),
                row("artist", "Artist", false, "AlbumArtist,Album,ParentIndexNumber,IndexNumber,SortName"),
                row("album", "Album", false, "Album,ParentIndexNumber,IndexNumber,SortName"),
                row("added", "Date added", true, "DateCreated"),
                row("plays", "Most played", true, "PlayCount"),
                row("duration", "Duration", true, "Runtime"),
                row("random", "Random", false, "Random")};
    case Section::Genres:
        return {row("size", "Size", true, ""), row("name", "Name", false, "")};
    case Section::Playlists:
        return {row("name", "Name", false, "SortName"), row("added", "Date added", true, "DateCreated")};
    }
    return {};
}

bool supportsLetter(Section section)
{
    return section == Section::Albums || section == Section::Artists || section == Section::Songs;
}

} // namespace

QList<SortKey> sortKeysFor(Section section)
{
    QList<SortKey> keys;
    for (const SortRow &row : rows(section))
        keys.append(row.key);
    return keys;
}

QString sortFields(Section section, const QString &key)
{
    const QList<SortRow> list = rows(section);
    for (const SortRow &row : list) {
        if (row.key.key == key)
            return row.fields;
    }
    return list.isEmpty() ? QString() : list.first().fields;
}

LetterRange letterRange(const QString &letter)
{
    if (letter.isEmpty())
        return {};
    if (letter == QLatin1String("#"))
        return {QString(), QStringLiteral("A")};
    const QChar c = letter.at(0).toUpper();
    if (c == QLatin1Char('Z'))
        return {QStringLiteral("Z"), QString()};
    return {QString(c), QString(QChar(c.unicode() + 1))};
}

QStringList codecsFor(FormatFilter format)
{
    switch (format) {
    case FormatFilter::Lossless:
    case FormatFilter::HiRes:
        return {QStringLiteral("flac"), QStringLiteral("alac"), QStringLiteral("ape"),
                QStringLiteral("wavpack"), QStringLiteral("wav"), QStringLiteral("pcm_s16le"),
                QStringLiteral("pcm_s24le"), QStringLiteral("pcm_s32le"), QStringLiteral("dsd_lsbf"),
                QStringLiteral("dsd_msbf")};
    case FormatFilter::Lossy:
        return {QStringLiteral("mp3"), QStringLiteral("aac"), QStringLiteral("opus"),
                QStringLiteral("vorbis"), QStringLiteral("wma")};
    case FormatFilter::Any:
        break;
    }
    return {};
}

bool formatFilterable(Section section)
{
    if (section == Section::Songs)
        return emby::caps::kAudioCodecsFiltersAudio;
    if (section == Section::Albums)
        return emby::caps::kAudioCodecsFiltersAlbums;
    return false;
}

QList<FormatFilter> formatOptions(Section section)
{
    if (!formatFilterable(section))
        return {};
    QList<FormatFilter> options{FormatFilter::Lossless, FormatFilter::Lossy};
    if (emby::caps::kHiResFilter)
        options.append(FormatFilter::HiRes);
    return options;
}

ItemsQuery toItemsQuery(const MusicQuery &query, int startIndex, int limit)
{
    ItemsQuery items;
    items.parentId = query.libraryId;
    items.recursive = true;
    items.startIndex = startIndex;
    items.limit = limit;
    switch (query.section) {
    case Section::Albums:
        items.includeItemTypes = {QStringLiteral("MusicAlbum")};
        items.fields = {QStringLiteral("ChildCount"), QStringLiteral("DateCreated"),
                        QStringLiteral("PremiereDate"), QStringLiteral("ProductionYear"),
                        QStringLiteral("Genres"), QStringLiteral("CumulativeRunTimeTicks")};
        break;
    case Section::Songs:
        items.includeItemTypes = {QStringLiteral("Audio")};
        items.fields = {QStringLiteral("MediaStreams"), QStringLiteral("DateCreated")};
        break;
    case Section::Playlists:
        items.includeItemTypes = {QStringLiteral("Playlist")};
        items.fields = {QStringLiteral("ChildCount"), QStringLiteral("CumulativeRunTimeTicks"),
                        QStringLiteral("DateCreated")};
        items.recursive = false;
        break;
    case Section::Artists:
        items.fields = {QStringLiteral("ItemCounts"), QStringLiteral("DateCreated")};
        break;
    case Section::Genres:
        break;
    }

    items.sortBy = sortFields(query.section, query.sortKey);
    items.sortDescending = query.descending;

    if (query.section == Section::Playlists || query.section == Section::Genres)
        return items; // playlists and genres take no filters

    items.genreIds = query.genreIds;
    if (query.favouritesOnly)
        items.filters.append(QStringLiteral("IsFavorite"));
    if (query.unplayedOnly)
        items.filters.append(QStringLiteral("IsUnplayed"));

    if (query.decade == kDecadeEarlier) {
        items.maxPremiereDate = QStringLiteral("1949-12-31T23:59:59Z");
    } else if (query.decade > 0 && query.section != Section::Artists) {
        if (emby::caps::kYearsAcceptsDecade) {
            for (int year = query.decade; year < query.decade + 10; ++year)
                items.years.append(year);
        } else {
            items.minPremiereDate = QStringLiteral("%1-01-01T00:00:00Z").arg(query.decade);
            items.maxPremiereDate = QStringLiteral("%1-12-31T23:59:59Z").arg(query.decade + 9);
        }
    }

    if (query.format != FormatFilter::Any && formatFilterable(query.section))
        items.audioCodecs = codecsFor(query.format);

    if (query.sortKey == QLatin1String("name") && supportsLetter(query.section)) {
        const LetterRange range = letterRange(query.letter);
        items.nameStartsWithOrGreater = range.greaterOrEqual;
        items.nameLessThan = range.lessThan;
    }
    return items;
}

} // namespace strmqt::music::MusicQueryTranslator
```

In the `decadeTranslation` test, `kDecadeEarlier` on an Albums query must set only `maxPremiereDate`, which the code does. For Artists, a decade is not sent, because the `/Artists` endpoints ignore it (`artistParams` does not forward years). The repository uses `artistParams` for that section.

Add to `strmqt_app` in `src/CMakeLists.txt`:

```cmake
    app/music/MusicFormat.h app/music/MusicFormat.cpp
    app/music/MusicQueryTranslator.h app/music/MusicQueryTranslator.cpp
```

- [x] **Step 5: Run the tests to verify they pass**

Run: `cmake --build --preset dev --target tst_music_query_translator && ctest --preset dev -R tst_music_query_translator --output-on-failure`
Expected: PASS (8 tests).

- [x] **Step 6: Commit**

```bash
git add src/app/music/MusicFormat.* src/app/music/MusicQueryTranslator.* src/CMakeLists.txt tests/unit/tst_music_query_translator.cpp tests/CMakeLists.txt
git commit -m "feat(music): translate browse queries and format music display strings

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 10: `MusicRepository`, part 1: skeleton, album tracks, sleeve, artist profile

**Files:**
- Create: `src/app/music/MusicRepository.h`, `src/app/music/MusicRepository.cpp`
- Modify: `src/CMakeLists.txt` (`strmqt_app`)
- Test: `tests/integration/tst_music_repository.cpp`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `EmbyClient::getJson`, `EmbyClient::itemsParams` (Task 1); `caps::*` (Task 2); DTOs (Task 3); mapper (Tasks 4–6); `MockEmbyServer::addQueryRoute` (Task 7); `TtlCache`, `Fanout` (Task 8).
- Produces (namespace `strmqt::music`):
  - `enum class Freshness { Listening, Favourites, Everything }`
  - `class MusicRepository : public QObject`:
    - `MusicRepository(emby::EmbyClient *client, QObject *parent = nullptr)`
    - `QFuture<Result<QList<Track>>> albumTracks(const QString &albumId)`
    - `QFuture<Result<AlbumSleeve>> albumSleeve(const QString &albumId)`
    - `QFuture<Result<ArtistProfile>> artistProfile(const QString &libraryId, const QString &artistId)`
    - `void clear()`
    - `void setClockForTests(std::function<QDateTime()> clock)`
    - `void setShuffleSeedForTests(quint32 seed)`
    - `static QStringList albumFields()`, `static QStringList trackFields()`

Composition rules for this task:
- **`albumTracks`**
  - Request: `ParentId=<album>`, `IncludeItemTypes=Audio`, `Recursive=true`, `SortBy=ParentIndexNumber,IndexNumber,SortName`, `Limit=1000`, `Fields=trackFields()`.
  - Cached for 10 min under `tracks:<id>`.
- **`albumSleeve`**
  - Core, fetched in parallel: the album item (`/Users/{uid}/Items/<id>`) and `albumTracks`. Either failing fails the sleeve.
  - Secondary, fetched after the core: "more by", with `AlbumArtistIds=<first album artist id>`, `IncludeItemTypes=MusicAlbum`, `Recursive=true`, `SortBy=ProductionYear,PremiereDate,SortName`, descending, `Limit=13`, `Fields=albumFields()`. The album itself is excluded and the list capped at 12. On failure it is empty and a warning is logged. Skipped when there is no album-artist id.
  - Cached for 10 min under `sleeve:<id>`.
- **`artistProfile`**
  - Core: the artist item.
  - Secondary, in parallel:
    - filed albums (`AlbumArtistIds`, limit 200, newest first)
    - albums they appear on (`ArtistIds`, limit 200, newest first)
    - top tracks (`Audio`, `ArtistIds`, `SortBy=PlayCount,SortName` descending, limit 5)
    - similar artists (only when `caps::kSimilarArtists`: `caps::kSimilarArtistsPath` with `{id}`, `UserId={uid}`, `Limit=8`)
  - Grouping: filed Album/Compilation → `albums`; filed EP/Single → `epsAndSingles`; appearing albums not filed under the artist → `appearsOn`.
  - Not cached.
- Cache writes are skipped when the session epoch changed while the request was in flight (`identityChanged` → `clear()` bumps the epoch).

- [x] **Step 1: Write the failing tests**

`tests/integration/tst_music_repository.cpp`:

```cpp
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>
#include <QtTest>

#include "MockEmbyServer.h"
#include "app/music/MusicRepository.h"
#include "server/emby/EmbyClient.h"
#include "server/emby/MusicServerCapabilities.h"

using namespace strmqt;
using namespace strmqt::music;
using emby::EmbyClient;

namespace {

const auto kUserId = QStringLiteral("a1b2c3d4e5f60718293a4b5c6d7e8f90");
const auto kToken = QStringLiteral("not-a-real-token-fixture-only");
const auto kLibrary = QStringLiteral("1868998");

QString itemsPath() { return QStringLiteral("/Users/%1/Items").arg(kUserId); }
QString itemPath(const QString &id) { return QStringLiteral("/Users/%1/Items/%2").arg(kUserId, id); }

QByteArray page(const QJsonArray &items, int total = -1)
{
    return QJsonDocument(QJsonObject{{QStringLiteral("Items"), items},
                                     {QStringLiteral("TotalRecordCount"), total < 0 ? items.size() : total}})
        .toJson(QJsonDocument::Compact);
}

QByteArray object(const QJsonObject &json) { return QJsonDocument(json).toJson(QJsonDocument::Compact); }

QJsonObject albumJson(const QString &id, const QString &artistId = QStringLiteral("ar1"),
                      int tracks = 10, qint64 minutes = 45)
{
    return {{"Id", id}, {"Name", "Album " + id}, {"Type", "MusicAlbum"},
            {"AlbumArtists", QJsonArray{QJsonObject{{"Id", artistId}, {"Name", "Artist " + artistId}}}},
            {"ChildCount", tracks}, {"CumulativeRunTimeTicks", minutes * 60 * 10'000'000LL},
            {"ImageTags", QJsonObject{{"Primary", "tag-" + id}}}};
}

QJsonObject trackJson(const QString &id, const QString &albumId, int disc, int number,
                      const QString &artistId = QStringLiteral("ar1"))
{
    return {{"Id", id}, {"Name", "Track " + id}, {"Type", "Audio"}, {"AlbumId", albumId},
            {"Album", "Album " + albumId}, {"ParentIndexNumber", disc}, {"IndexNumber", number},
            {"RunTimeTicks", 4 * 60 * 10'000'000LL},
            {"ArtistItems", QJsonArray{QJsonObject{{"Id", artistId}, {"Name", "Artist " + artistId}}}},
            {"AlbumArtists", QJsonArray{QJsonObject{{"Id", "ar1"}, {"Name", "Artist ar1"}}}},
            {"MediaStreams", QJsonArray{QJsonObject{{"Type", "Audio"}, {"Codec", "flac"},
                                                    {"BitDepth", 16}, {"SampleRate", 44100}}}}};
}

template<class T> Result<T> waitFor(QFuture<Result<T>> future)
{
    if (!QTest::qWaitFor([&] { return future.isFinished(); }, 5000))
        return Result<T>::failure(QStringLiteral("timeout waiting for future"));
    return future.result();
}

using Q = QList<QPair<QString, QString>>;

} // namespace

class MusicRepositoryTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void sleeveComposesAlbumTracksAndMoreBy();
    void sleeveSurvivesMoreByFailure();
    void sleeveFailsWhenTheAlbumFails();
    void sleeveIsCachedUntilTtl();
    void identityChangeClearsCaches();
    void artistProfileGroupsReleases();
    void artistProfileFailsOnlyOnTheArtist();

private:
    void routeAlbum(const QString &albumId, int trackCount);

    MockEmbyServer *m_mock = nullptr;
    EmbyClient *m_client = nullptr;
    MusicRepository *m_repo = nullptr;
    QDateTime m_now = QDateTime::fromSecsSinceEpoch(1'800'000'000, QTimeZone::utc());
};

void MusicRepositoryTest::init()
{
    m_mock = new MockEmbyServer(this);
    QVERIFY(m_mock->start());
    m_client = new EmbyClient(this);
    m_client->setBaseUrl(m_mock->baseUrl());
    m_client->setDeviceId(QStringLiteral("test-device-id"));
    m_client->setDeviceName(QStringLiteral("test-host"));
    m_client->setSession(kToken, kUserId);
    m_repo = new MusicRepository(m_client, this);
    m_repo->setClockForTests([this] { return m_now; });
    m_repo->setShuffleSeedForTests(7);
}

void MusicRepositoryTest::cleanup()
{
    delete m_repo;
    delete m_client;
    delete m_mock;
    m_repo = nullptr;
    m_client = nullptr;
    m_mock = nullptr;
}

void MusicRepositoryTest::routeAlbum(const QString &albumId, int trackCount)
{
    m_mock->addRoute(QStringLiteral("GET"), itemPath(albumId), 200, object(albumJson(albumId)));
    QJsonArray tracks;
    for (int i = 1; i <= trackCount; ++i)
        tracks.append(trackJson(albumId + "-t" + QString::number(i), albumId, i > 6 ? 2 : 1, i));
    m_mock->addQueryRoute(QStringLiteral("GET"), itemsPath(),
                          Q{{"ParentId", albumId}, {"IncludeItemTypes", "Audio"}}, 200, page(tracks));
}

void MusicRepositoryTest::sleeveComposesAlbumTracksAndMoreBy()
{
    routeAlbum(QStringLiteral("al1"), 9);
    QJsonArray more{albumJson("al1"), albumJson("al2"), albumJson("al3")};
    m_mock->addQueryRoute(QStringLiteral("GET"), itemsPath(),
                          Q{{"AlbumArtistIds", "ar1"}, {"IncludeItemTypes", "MusicAlbum"}}, 200, page(more));

    const auto result = waitFor(m_repo->albumSleeve(QStringLiteral("al1")));
    QVERIFY2(result.ok(), qPrintable(result.error));
    const AlbumSleeve &sleeve = result.value;
    QCOMPARE(sleeve.album.id, QStringLiteral("al1"));
    QCOMPARE(sleeve.album.trackCount, 9);
    QCOMPARE(sleeve.album.discCount, 2);
    QCOMPARE(sleeve.album.formatSummary, QStringLiteral("FLAC 16/44.1"));
    QCOMPARE(sleeve.discs.size(), 2);
    QCOMPARE(sleeve.discs.at(0).tracks.size(), 6);
    QCOMPARE(sleeve.moreByArtist.size(), 2); // al1 itself excluded
    QCOMPARE(sleeve.moreByArtist.at(0).id, QStringLiteral("al2"));

    bool sawTracks = false;
    for (const auto &request : m_mock->requests()) {
        const QUrlQuery q(request.query);
        if (q.queryItemValue(QStringLiteral("ParentId")) == QLatin1String("al1")) {
            sawTracks = true;
            QCOMPARE(q.queryItemValue(QStringLiteral("SortBy")),
                     QStringLiteral("ParentIndexNumber,IndexNumber,SortName"));
            QCOMPARE(q.queryItemValue(QStringLiteral("Limit")), QStringLiteral("1000"));
        }
    }
    QVERIFY(sawTracks);
}

void MusicRepositoryTest::sleeveSurvivesMoreByFailure()
{
    routeAlbum(QStringLiteral("al1"), 3);
    m_mock->addQueryRoute(QStringLiteral("GET"), itemsPath(), Q{{"AlbumArtistIds", "ar1"}}, 500, "{}");
    const auto result = waitFor(m_repo->albumSleeve(QStringLiteral("al1")));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QVERIFY(result.value.moreByArtist.isEmpty());
    QCOMPARE(result.value.discs.first().tracks.size(), 3);
}

void MusicRepositoryTest::sleeveFailsWhenTheAlbumFails()
{
    m_mock->addRoute(QStringLiteral("GET"), itemPath(QStringLiteral("gone")), 404, "{}");
    m_mock->addQueryRoute(QStringLiteral("GET"), itemsPath(), Q{{"ParentId", "gone"}}, 200, page({}));
    const auto result = waitFor(m_repo->albumSleeve(QStringLiteral("gone")));
    QVERIFY(!result.ok());
}

void MusicRepositoryTest::sleeveIsCachedUntilTtl()
{
    routeAlbum(QStringLiteral("al1"), 2);
    QVERIFY(waitFor(m_repo->albumSleeve(QStringLiteral("al1"))).ok());
    const int afterFirst = m_mock->requestCount();

    m_now = m_now.addSecs(9 * 60);
    QVERIFY(waitFor(m_repo->albumSleeve(QStringLiteral("al1"))).ok());
    QCOMPARE(m_mock->requestCount(), afterFirst);

    m_now = m_now.addSecs(2 * 60); // 11 minutes after the first fetch
    QVERIFY(waitFor(m_repo->albumSleeve(QStringLiteral("al1"))).ok());
    QVERIFY(m_mock->requestCount() > afterFirst);
}

void MusicRepositoryTest::identityChangeClearsCaches()
{
    routeAlbum(QStringLiteral("al1"), 2);
    QVERIFY(waitFor(m_repo->albumTracks(QStringLiteral("al1"))).ok());
    const int afterFirst = m_mock->requestCount();
    QVERIFY(waitFor(m_repo->albumTracks(QStringLiteral("al1"))).ok());
    QCOMPARE(m_mock->requestCount(), afterFirst);

    m_client->setSession(kToken, QStringLiteral("ffffffffffffffffffffffffffffffff"));
    m_client->setSession(kToken, kUserId);
    QVERIFY(waitFor(m_repo->albumTracks(QStringLiteral("al1"))).ok());
    QVERIFY(m_mock->requestCount() > afterFirst);
}

void MusicRepositoryTest::artistProfileGroupsReleases()
{
    m_mock->addRoute(QStringLiteral("GET"), itemPath(QStringLiteral("ar1")), 200,
                     object({{"Id", "ar1"}, {"Name", "Artist ar1"}, {"Type", "MusicArtist"}}));
    QJsonArray filed{albumJson("lp", "ar1", 10, 45), albumJson("ep", "ar1", 5, 20),
                     albumJson("single", "ar1", 2, 7)};
    m_mock->addQueryRoute(QStringLiteral("GET"), itemsPath(),
                          Q{{"AlbumArtistIds", "ar1"}, {"IncludeItemTypes", "MusicAlbum"}}, 200, page(filed));
    QJsonArray appears{albumJson("lp", "ar1"), albumJson("guest", "ar9")};
    m_mock->addQueryRoute(QStringLiteral("GET"), itemsPath(),
                          Q{{"ArtistIds", "ar1"}, {"IncludeItemTypes", "MusicAlbum"}}, 200, page(appears));
    m_mock->addQueryRoute(QStringLiteral("GET"), itemsPath(),
                          Q{{"ArtistIds", "ar1"}, {"IncludeItemTypes", "Audio"}}, 200,
                          page({trackJson("hit", "lp", 1, 1)}));
    if (emby::caps::kSimilarArtists) {
        const QString similarPath = QString::fromLatin1(emby::caps::kSimilarArtistsPath)
                                        .replace(QStringLiteral("{id}"), QStringLiteral("ar1"));
        m_mock->addRoute(QStringLiteral("GET"), similarPath, 200,
                         page({QJsonObject{{"Id", "ar2"}, {"Name", "Kin"}, {"Type", "MusicArtist"}}}));
    }

    const auto result = waitFor(m_repo->artistProfile(kLibrary, QStringLiteral("ar1")));
    QVERIFY2(result.ok(), qPrintable(result.error));
    const ArtistProfile &profile = result.value;
    QCOMPARE(profile.artist.name, QStringLiteral("Artist ar1"));
    QCOMPARE(profile.albums.size(), 1);
    QCOMPARE(profile.albums.first().id, QStringLiteral("lp"));
    QCOMPARE(profile.epsAndSingles.size(), 2);
    QCOMPARE(profile.appearsOn.size(), 1);
    QCOMPARE(profile.appearsOn.first().id, QStringLiteral("guest"));
    QCOMPARE(profile.topTracks.size(), 1);
    QCOMPARE(profile.similar.size(), emby::caps::kSimilarArtists ? 1 : 0);
}

void MusicRepositoryTest::artistProfileFailsOnlyOnTheArtist()
{
    m_mock->addRoute(QStringLiteral("GET"), itemPath(QStringLiteral("ar1")), 200,
                     object({{"Id", "ar1"}, {"Name", "Solo"}}));
    // Every secondary request 404s (no routes): the profile still resolves.
    const auto partial = waitFor(m_repo->artistProfile(kLibrary, QStringLiteral("ar1")));
    QVERIFY2(partial.ok(), qPrintable(partial.error));
    QVERIFY(partial.value.albums.isEmpty());

    const auto missing = waitFor(m_repo->artistProfile(kLibrary, QStringLiteral("nobody")));
    QVERIFY(!missing.ok());
}

QTEST_MAIN(MusicRepositoryTest)
#include "tst_music_repository.moc"
```

Register in `tests/CMakeLists.txt`:

```cmake
strmqt_add_test(tst_music_repository
    integration/tst_music_repository.cpp
    mocks/MockEmbyServer.h mocks/MockEmbyServer.cpp
)
target_include_directories(tst_music_repository PRIVATE mocks)
```

- [x] **Step 2: Run the tests to verify they fail**

Run: `cmake --preset dev && cmake --build --preset dev --target tst_music_repository`
Expected: a compile error (`app/music/MusicRepository.h` not found).

- [x] **Step 3: Write the header**

```cpp
#pragma once

#include <QDateTime>
#include <QFuture>
#include <QJsonDocument>
#include <QObject>
#include <QRandomGenerator>
#include <QUrlQuery>

#include <functional>

#include "app/music/TtlCache.h"
#include "core/Result.h"
#include "server/dto/ItemsQuery.h"
#include "server/dto/music/MusicQuery.h"
#include "server/dto/music/MusicTypes.h"

namespace strmqt::emby {
class EmbyClient;
}

namespace strmqt::music {

enum class Freshness { Listening, Favourites, Everything };

// The only music unit that talks to Emby (Crate spec §3.3). Composes several
// requests into one music DTO, caches per account, and resolves on this
// object's thread. Core failures fail the result; secondary failures come back
// empty and are logged.
class MusicRepository : public QObject
{
    Q_OBJECT

public:
    explicit MusicRepository(emby::EmbyClient *client, QObject *parent = nullptr);

    QFuture<Result<QList<Track>>> albumTracks(const QString &albumId);
    QFuture<Result<AlbumSleeve>> albumSleeve(const QString &albumId);
    QFuture<Result<ArtistProfile>> artistProfile(const QString &libraryId, const QString &artistId);

    void clear();
    void setClockForTests(std::function<QDateTime()> clock);
    void setShuffleSeedForTests(quint32 seed);

    static QStringList albumFields();
    static QStringList trackFields();

private:
    QFuture<Result<QJsonDocument>> fetchItems(const ItemsQuery &query);
    QFuture<Result<QJsonDocument>> fetchItem(const QString &itemId);
    QFuture<Result<QList<Album>>> fetchAlbums(const ItemsQuery &query);
    QFuture<Result<QList<Track>>> fetchTracks(const ItemsQuery &query);
    QDateTime now() const;
    bool epochIs(quint64 epoch) const { return epoch == m_epoch; }

    emby::EmbyClient *m_client = nullptr;
    std::function<QDateTime()> m_clock;
    QRandomGenerator m_rng;
    quint64 m_epoch = 0;
    TtlCache<QList<Track>> m_trackCache{std::chrono::minutes(10)};
    TtlCache<AlbumSleeve> m_sleeveCache{std::chrono::minutes(10)};
};

} // namespace strmqt::music
```

- [x] **Step 4: Write the implementation**

```cpp
#include "app/music/MusicRepository.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QPromise>
#include <QSet>

#include <memory>

#include "app/music/Fanout.h"
#include "core/Log.h"
#include "server/emby/EmbyClient.h"
#include "server/emby/EmbyMusicMapper.h"
#include "server/emby/MusicServerCapabilities.h"

namespace strmqt::music {

namespace {

template<class T>
QFuture<Result<T>> ready(Result<T> result)
{
    return QtFuture::makeReadyValueFuture(std::move(result));
}

template<class T>
struct Pending
{
    std::shared_ptr<QPromise<Result<T>>> promise = std::make_shared<QPromise<Result<T>>>();
    Pending() { promise->start(); }
    QFuture<Result<T>> future() const { return promise->future(); }
    void resolve(Result<T> result) const
    {
        promise->addResult(std::move(result));
        promise->finish();
    }
};

QJsonArray itemsOf(const QJsonDocument &doc)
{
    return doc.object().value(QStringLiteral("Items")).toArray();
}

const QString kAudio = QStringLiteral("Audio");
const QString kAlbum = QStringLiteral("MusicAlbum");

} // namespace

MusicRepository::MusicRepository(emby::EmbyClient *client, QObject *parent)
    : QObject(parent), m_client(client), m_rng(QRandomGenerator::securelySeeded())
{
    connect(m_client, &emby::EmbyClient::identityChanged, this, &MusicRepository::clear);
}

QStringList MusicRepository::albumFields()
{
    return {QStringLiteral("ChildCount"), QStringLiteral("DateCreated"), QStringLiteral("PremiereDate"),
            QStringLiteral("ProductionYear"), QStringLiteral("Genres"), QStringLiteral("Studios"),
            QStringLiteral("CumulativeRunTimeTicks")};
}

QStringList MusicRepository::trackFields()
{
    return {emby::caps::kMediaStreamsOnLists ? QStringLiteral("MediaStreams")
                                             : QStringLiteral("MediaSources"),
            QStringLiteral("DateCreated")};
}

void MusicRepository::clear()
{
    ++m_epoch;
    m_trackCache.clear();
    m_sleeveCache.clear();
}

void MusicRepository::setClockForTests(std::function<QDateTime()> clock)
{
    m_clock = std::move(clock);
}

void MusicRepository::setShuffleSeedForTests(quint32 seed)
{
    m_rng.seed(seed);
}

QDateTime MusicRepository::now() const
{
    return m_clock ? m_clock() : QDateTime::currentDateTimeUtc();
}

QFuture<Result<QJsonDocument>> MusicRepository::fetchItems(const ItemsQuery &query)
{
    return m_client->getJson(QStringLiteral("/Users/{uid}/Items"), emby::EmbyClient::itemsParams(query));
}

QFuture<Result<QJsonDocument>> MusicRepository::fetchItem(const QString &itemId)
{
    return m_client->getJson(QStringLiteral("/Users/{uid}/Items/%1").arg(itemId), {});
}

QFuture<Result<QList<Album>>> MusicRepository::fetchAlbums(const ItemsQuery &query)
{
    return fetchItems(query).then(this, [](Result<QJsonDocument> result) {
        if (!result.ok())
            return Result<QList<Album>>::failure(result.error);
        return Result<QList<Album>>::success(emby::parseAlbums(itemsOf(result.value)));
    });
}

QFuture<Result<QList<Track>>> MusicRepository::fetchTracks(const ItemsQuery &query)
{
    return fetchItems(query).then(this, [](Result<QJsonDocument> result) {
        if (!result.ok())
            return Result<QList<Track>>::failure(result.error);
        return Result<QList<Track>>::success(emby::parseTracks(itemsOf(result.value)));
    });
}

QFuture<Result<QList<Track>>> MusicRepository::albumTracks(const QString &albumId)
{
    const QString key = QStringLiteral("tracks:") + albumId;
    if (auto cached = m_trackCache.get(key, now()))
        return ready(Result<QList<Track>>::success(*cached));

    ItemsQuery query;
    query.parentId = albumId;
    query.includeItemTypes = {kAudio};
    query.recursive = true;
    query.sortBy = QStringLiteral("ParentIndexNumber,IndexNumber,SortName");
    query.fields = trackFields();
    query.limit = 1000;
    const quint64 epoch = m_epoch;
    return fetchTracks(query).then(this, [this, key, epoch](Result<QList<Track>> result) {
        if (result.ok() && epochIs(epoch))
            m_trackCache.put(key, result.value, now());
        return result;
    });
}

QFuture<Result<AlbumSleeve>> MusicRepository::albumSleeve(const QString &albumId)
{
    const QString key = QStringLiteral("sleeve:") + albumId;
    if (auto cached = m_sleeveCache.get(key, now()))
        return ready(Result<AlbumSleeve>::success(*cached));

    struct State
    {
        Result<Album> album;
        Result<QList<Track>> tracks;
    };
    auto state = std::make_shared<State>();
    Pending<AlbumSleeve> pending;
    const quint64 epoch = m_epoch;

    auto fan = Fanout::create(this, [this, state, pending, albumId, key, epoch] {
        if (!state->album.ok())
            return pending.resolve(Result<AlbumSleeve>::failure(state->album.error));
        if (!state->tracks.ok())
            return pending.resolve(Result<AlbumSleeve>::failure(state->tracks.error));

        auto sleeve = std::make_shared<AlbumSleeve>();
        sleeve->album = state->album.value;
        emby::refineAlbumFromTracks(sleeve->album, state->tracks.value);
        sleeve->discs = emby::groupDiscs(state->tracks.value);

        auto finish = [this, sleeve, pending, key, epoch] {
            if (epochIs(epoch))
                m_sleeveCache.put(key, *sleeve, now());
            pending.resolve(Result<AlbumSleeve>::success(*sleeve));
        };
        const QString artistId =
            sleeve->album.albumArtists.isEmpty() ? QString() : sleeve->album.albumArtists.first().id;
        if (artistId.isEmpty())
            return finish();

        ItemsQuery more;
        more.albumArtistIds = {artistId};
        more.includeItemTypes = {kAlbum};
        more.recursive = true;
        more.sortBy = QStringLiteral("ProductionYear,PremiereDate,SortName");
        more.sortDescending = true;
        more.fields = albumFields();
        more.limit = 13;
        fetchAlbums(more).then(this, [sleeve, albumId, finish](Result<QList<Album>> result) {
            if (!result.ok()) {
                qCWarning(logApp) << "music: more-by failed for album" << albumId << result.error;
            } else {
                for (const Album &album : std::as_const(result.value)) {
                    if (album.id != albumId && sleeve->moreByArtist.size() < 12)
                        sleeve->moreByArtist.append(album);
                }
            }
            finish();
        });
    });

    fan->add<Album>(fetchItem(albumId).then(this, [](Result<QJsonDocument> result) {
        if (!result.ok())
            return Result<Album>::failure(result.error);
        return Result<Album>::success(emby::parseAlbum(result.value.object()));
    }), [state](Result<Album> result) { state->album = std::move(result); });
    fan->add<QList<Track>>(albumTracks(albumId),
                           [state](Result<QList<Track>> result) { state->tracks = std::move(result); });
    fan->seal();
    return pending.future();
}

QFuture<Result<ArtistProfile>> MusicRepository::artistProfile(const QString &libraryId,
                                                              const QString &artistId)
{
    struct State
    {
        Result<Artist> artist;
        QList<Album> filed;
        QList<Album> appearing;
        QList<Track> top;
        QList<Artist> similar;
    };
    auto state = std::make_shared<State>();
    Pending<ArtistProfile> pending;

    auto fan = Fanout::create(this, [state, pending] {
        if (!state->artist.ok())
            return pending.resolve(Result<ArtistProfile>::failure(state->artist.error));
        ArtistProfile profile;
        profile.artist = state->artist.value;
        QSet<QString> filedIds;
        for (const Album &album : std::as_const(state->filed)) {
            filedIds.insert(album.id);
            if (album.releaseType == ReleaseType::EP || album.releaseType == ReleaseType::Single)
                profile.epsAndSingles.append(album);
            else
                profile.albums.append(album);
        }
        for (const Album &album : std::as_const(state->appearing)) {
            if (!filedIds.contains(album.id))
                profile.appearsOn.append(album);
        }
        profile.topTracks = state->top;
        profile.similar = state->similar;
        pending.resolve(Result<ArtistProfile>::success(profile));
    });

    auto secondary = [artistId](const char *what) {
        return [artistId, what](const QString &error) {
            qCWarning(logApp) << "music: artist" << what << "failed for" << artistId << error;
        };
    };

    fan->add<Artist>(fetchItem(artistId).then(this, [](Result<QJsonDocument> result) {
        if (!result.ok())
            return Result<Artist>::failure(result.error);
        return Result<Artist>::success(emby::parseArtist(result.value.object()));
    }), [state](Result<Artist> result) { state->artist = std::move(result); });

    ItemsQuery albums;
    albums.parentId = libraryId;
    albums.includeItemTypes = {kAlbum};
    albums.recursive = true;
    albums.sortBy = QStringLiteral("ProductionYear,PremiereDate,SortName");
    albums.sortDescending = true;
    albums.fields = albumFields();
    albums.limit = 200;

    ItemsQuery filed = albums;
    filed.albumArtistIds = {artistId};
    fan->add<QList<Album>>(fetchAlbums(filed), [state, log = secondary("albums")](Result<QList<Album>> r) {
        if (r.ok())
            state->filed = r.value;
        else
            log(r.error);
    });

    ItemsQuery appearing = albums;
    appearing.artistIds = {artistId};
    fan->add<QList<Album>>(fetchAlbums(appearing), [state, log = secondary("appears-on")](Result<QList<Album>> r) {
        if (r.ok())
            state->appearing = r.value;
        else
            log(r.error);
    });

    ItemsQuery top;
    top.parentId = libraryId;
    top.includeItemTypes = {kAudio};
    top.recursive = true;
    top.artistIds = {artistId};
    top.sortBy = QStringLiteral("PlayCount,SortName");
    top.sortDescending = true;
    top.fields = trackFields();
    top.limit = 5;
    fan->add<QList<Track>>(fetchTracks(top), [state, log = secondary("top tracks")](Result<QList<Track>> r) {
        if (r.ok())
            state->top = r.value;
        else
            log(r.error);
    });

    if (emby::caps::kSimilarArtists) {
        QUrlQuery params;
        params.addQueryItem(QStringLiteral("UserId"), QStringLiteral("{uid}"));
        params.addQueryItem(QStringLiteral("Limit"), QStringLiteral("8"));
        const QString path = QString::fromLatin1(emby::caps::kSimilarArtistsPath)
                                 .replace(QStringLiteral("{id}"), artistId);
        fan->add<QJsonDocument>(m_client->getJson(path, params),
                                [state, log = secondary("similar")](Result<QJsonDocument> r) {
                                    if (r.ok())
                                        state->similar = emby::parseArtists(itemsOf(r.value));
                                    else
                                        log(r.error);
                                });
    }
    fan->seal();
    return pending.future();
}

} // namespace strmqt::music
```

Add `app/music/MusicRepository.h app/music/MusicRepository.cpp` to `strmqt_app`.

The test's `artistProfileFailsOnlyOnTheArtist` gets a 404 on the artist `nobody`, which is a failure `Result` from `finishDocument`. Its "Solo" artist succeeds, and every secondary request 404s through the mock.

- [x] **Step 5: Run the tests to verify they pass**

Run: `cmake --build --preset dev --target tst_music_repository && ctest --preset dev -R tst_music_repository --output-on-failure`
Expected: PASS (7 tests).

- [x] **Step 6: Commit**

```bash
git add src/app/music/MusicRepository.* src/CMakeLists.txt tests/integration/tst_music_repository.cpp tests/CMakeLists.txt
git commit -m "feat(music): compose album sleeves and artist profiles in MusicRepository

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 11: `MusicRepository`, part 2: Home shelves, genres, stations

**Files:**
- Modify: `src/app/music/MusicRepository.h`, `src/app/music/MusicRepository.cpp`
- Test: `tests/integration/tst_music_repository.cpp`

**Interfaces:**
- Consumes: Task 10 (`fetchItems`, `fetchAlbums`, `fetchTracks`, `albumTracks`, `Pending`, `ready`, `albumFields`, `trackFields`, `m_rng`, `m_epoch`).
- Produces (public on `MusicRepository`):
  - `struct NewAlbums { QList<Album> albums; int addedThisWeek = -1; }` in namespace `strmqt::music`, where -1 means unknown
  - `QFuture<Result<ContinueListening>> continueListening(const QString &libraryId)`
  - `QFuture<Result<QList<Album>>> recentAlbums(const QString &libraryId, int limit = 20)`
  - `QFuture<Result<NewAlbums>> newAlbums(const QString &libraryId, int limit = 20)`
  - `QFuture<Result<QList<GenreBin>>> allGenres(const QString &libraryId)`: every genre with `recordCount`, largest first, without covers
  - `QFuture<Result<Page<GenreBin>>> genreBins(const QString &libraryId, int limit)`: the `limit` largest, each with ≤ 3 covers; `totalRecordCount` = number of genres
  - `QFuture<Result<QList<Artist>>> topArtists(const QString &libraryId, int limit = 20)`
  - `QFuture<Result<QList<Album>>> forgottenFavourites(const QString &libraryId, int limit = 20)`
  - `QFuture<Result<QList<Album>>> randomAlbums(const QString &libraryId, int limit = 20)`
  - `static QList<Station> stations(const QList<Album> &coverPool, const Artist &topArtist)`
  - `QFuture<Result<QList<Track>>> resolveStation(const QString &libraryId, const Station &station)`
  - `void markStale(Freshness freshness)`

Cache keys and TTLs:

| Cache | Type | TTL | Keys |
|---|---|---|---|
| `m_continueCache` | `ContinueListening` | 5 min | `home:continue:<lib>` |
| `m_albumListCache` | `QList<Album>` | 5 min | `home:recent:<lib>:<limit>`, `fav:forgotten:<lib>:<limit>` |
| `m_newCache` | `NewAlbums` | 5 min | `lib:new:<lib>:<limit>` |
| `m_artistListCache` | `QList<Artist>` | 5 min | `home:top:<lib>:<limit>` |
| `m_genreCache` | `QList<GenreBin>` | 5 min | `lib:genres:<lib>` |
| `m_coverCache` | `QList<ImageRef>` | session (−1) | `covers:<genreId>` |

`markStale`:
- `Listening` → the `home:` prefix on the continue, album-list and artist-list caches.
- `Favourites` → the `fav:` prefix on the album-list cache.
- `Everything` → `markStale()` on every cache except covers.

`clear()` also clears all six caches.

Every "played" query sends `Filters=IsPlayed`, so never-played tracks do not sort into history.

- [x] **Step 1: Write the failing tests**

Add these slots to `MusicRepositoryTest`:

```cpp
    void continueListeningResumesTheNextTrack();
    void continueListeningIsEmptyWithoutHistory();
    void recentAlbumsDedupeInPlayOrder();
    void newAlbumsCountThisWeek();
    void genreBinsSampleCoversOnce();
    void topArtistsRankByPlays();
    void stationsDescribeFiveTilesWithCovers();
    void heavyRotationIsShuffledPlayedTracks();
    void moreLikeDeduplicatesInstantMix();
    void markStaleRefetchesListeningShelves();
```

Add these helpers to the test's anonymous namespace:

```cpp
QJsonObject playedTrack(const QString &id, const QString &albumId, const QString &artistId,
                        const QString &lastPlayed, qint64 positionTicks = 0)
{
    QJsonObject track = trackJson(id, albumId, 1, 1, artistId);
    track.insert("UserData", QJsonObject{{"Played", positionTicks == 0},
                                         {"PlaybackPositionTicks", positionTicks},
                                         {"LastPlayedDate", lastPlayed}});
    return track;
}
```

Add the tests:

```cpp
void MusicRepositoryTest::continueListeningResumesTheNextTrack()
{
    m_mock->addQueryRoute("GET", itemsPath(),
                          Q{{"IncludeItemTypes", "Audio"}, {"SortBy", "DatePlayed"}, {"Limit", "1"}}, 200,
                          page({playedTrack("al1-t3", "al1", "ar1", "2026-09-15T20:00:00Z")}));
    routeAlbum(QStringLiteral("al1"), 4);

    const auto result = waitFor(m_repo->continueListening(kLibrary));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QVERIFY(result.value.isValid());
    QCOMPARE(result.value.album.id, QStringLiteral("al1"));
    QCOMPARE(result.value.resumeIndex, 3); // finished track 3 → resume track 4
    QCOMPARE(result.value.resumeTrack.id, QStringLiteral("al1-t4"));
    QCOMPARE(result.value.progress, 0.75);

    bool sawPlayedFilter = false;
    for (const auto &request : m_mock->requests()) {
        const QUrlQuery q(request.query);
        if (q.queryItemValue("SortBy") == QLatin1String("DatePlayed")) {
            sawPlayedFilter = q.queryItemValue("Filters") == QLatin1String("IsPlayed")
                              && q.queryItemValue("SortOrder") == QLatin1String("Descending");
        }
    }
    QVERIFY(sawPlayedFilter);
}

void MusicRepositoryTest::continueListeningIsEmptyWithoutHistory()
{
    m_mock->addQueryRoute("GET", itemsPath(),
                          Q{{"IncludeItemTypes", "Audio"}, {"SortBy", "DatePlayed"}, {"Limit", "1"}}, 200,
                          page({}));
    const auto result = waitFor(m_repo->continueListening(kLibrary));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QVERIFY(!result.value.isValid());
}

void MusicRepositoryTest::recentAlbumsDedupeInPlayOrder()
{
    m_mock->addQueryRoute("GET", itemsPath(),
                          Q{{"IncludeItemTypes", "Audio"}, {"SortBy", "DatePlayed"}, {"Limit", "200"}}, 200,
                          page({playedTrack("x1", "alB", "ar1", "2026-09-15T20:00:00Z"),
                                playedTrack("x2", "alA", "ar1", "2026-09-15T19:00:00Z"),
                                playedTrack("x3", "alB", "ar1", "2026-09-15T18:00:00Z"),
                                playedTrack("x4", "alC", "ar1", "2026-09-15T17:00:00Z")}));
    // The server answers Ids in its own order; the repository restores play order.
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"Ids", "alB,alA"}}, 200,
                          page({albumJson("alA"), albumJson("alB")}));

    const auto result = waitFor(m_repo->recentAlbums(kLibrary, 2));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.value.size(), 2);
    QCOMPARE(result.value.at(0).id, QStringLiteral("alB"));
    QCOMPARE(result.value.at(1).id, QStringLiteral("alA"));
}

void MusicRepositoryTest::newAlbumsCountThisWeek()
{
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"IncludeItemTypes", "MusicAlbum"}, {"SortBy", "DateCreated"}},
                          200, page({albumJson("n1"), albumJson("n2")}));
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"IncludeItemTypes", "MusicAlbum"}, {"Limit", "0"}}, 200,
                          page({}, 6));
    const auto result = waitFor(m_repo->newAlbums(kLibrary, 20));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.value.albums.size(), 2);
    QCOMPARE(result.value.addedThisWeek, emby::caps::kMinDateCreated ? 6 : -1);
    if (emby::caps::kMinDateCreated) {
        bool sawBound = false;
        for (const auto &request : m_mock->requests()) {
            const QString bound = QUrlQuery(request.query).queryItemValue("MinDateCreated");
            if (!bound.isEmpty()) {
                sawBound = true;
                QCOMPARE(QDateTime::fromString(bound, Qt::ISODate), m_now.addDays(-7));
            }
        }
        QVERIFY(sawBound);
    }
}

void MusicRepositoryTest::genreBinsSampleCoversOnce()
{
    QJsonArray genres{QJsonObject{{"Id", "g1"}, {"Name", "Jazz"}, {"AlbumCount", 3}},
                      QJsonObject{{"Id", "g2"}, {"Name", "Rock"}, {"AlbumCount", 40}},
                      QJsonObject{{"Id", "g3"}, {"Name", "Folk"}, {"AlbumCount", 12}}};
    m_mock->addRoute("GET", "/MusicGenres", 200, page(genres));
    if (!emby::caps::kGenreItemCounts) {
        // Counts come from an album walk instead.
        QJsonArray albums;
        auto tagged = [](const QString &id, const QString &genreId, const QString &name) {
            QJsonObject album = albumJson(id);
            album.insert("GenreItems", QJsonArray{QJsonObject{{"Id", genreId}, {"Name", name}}});
            return album;
        };
        for (int i = 0; i < 40; ++i)
            albums.append(tagged("r" + QString::number(i), "g2", "Rock"));
        for (int i = 0; i < 12; ++i)
            albums.append(tagged("f" + QString::number(i), "g3", "Folk"));
        for (int i = 0; i < 3; ++i)
            albums.append(tagged("j" + QString::number(i), "g1", "Jazz"));
        m_mock->addQueryRoute("GET", itemsPath(),
                              Q{{"IncludeItemTypes", "MusicAlbum"}, {"Fields", "Genres"}}, 200, page(albums));
    }
    for (const char *id : {"g1", "g2", "g3"}) {
        m_mock->addQueryRoute("GET", itemsPath(), Q{{"GenreIds", id}, {"SortBy", "Random"}}, 200,
                              page({albumJson(QString("c-") + id)}));
    }

    const auto first = waitFor(m_repo->genreBins(kLibrary, 2));
    QVERIFY2(first.ok(), qPrintable(first.error));
    QCOMPARE(first.value.totalRecordCount, 3);
    QCOMPARE(first.value.items.size(), 2);
    QCOMPARE(first.value.items.at(0).name, QStringLiteral("Rock"));
    QCOMPARE(first.value.items.at(0).recordCount, 40);
    QCOMPARE(first.value.items.at(1).name, QStringLiteral("Folk"));
    QCOMPARE(first.value.items.at(0).covers.size(), 1);

    m_repo->markStale(Freshness::Everything);
    const int before = m_mock->requestCount();
    QVERIFY(waitFor(m_repo->genreBins(kLibrary, 2)).ok());
    for (qsizetype i = before; i < m_mock->requests().size(); ++i)
        QVERIFY(!QUrlQuery(m_mock->requests().at(i).query).hasQueryItem("GenreIds")); // covers cached
}

void MusicRepositoryTest::topArtistsRankByPlays()
{
    m_mock->addQueryRoute("GET", itemsPath(),
                          Q{{"IncludeItemTypes", "Audio"}, {"SortBy", "DatePlayed"}, {"Limit", "500"}}, 200,
                          page({playedTrack("a", "al", "arX", "2026-09-15T20:00:00Z"),
                                playedTrack("b", "al", "arY", "2026-09-15T19:00:00Z"),
                                playedTrack("c", "al", "arY", "2026-09-15T18:00:00Z")}));
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"Ids", "arY,arX"}}, 200,
                          page({QJsonObject{{"Id", "arX"}, {"Name", "X"}}, QJsonObject{{"Id", "arY"}, {"Name", "Y"}}}));
    const auto result = waitFor(m_repo->topArtists(kLibrary, 5));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.value.size(), 2);
    QCOMPARE(result.value.at(0).id, QStringLiteral("arY"));
}

void MusicRepositoryTest::stationsDescribeFiveTilesWithCovers()
{
    QList<Album> pool;
    for (int i = 0; i < 6; ++i)
        pool.append(emby::parseAlbum(albumJson("p" + QString::number(i))));
    Artist top;
    top.id = QStringLiteral("ar1");
    top.name = QStringLiteral("Nina Simone");

    const QList<Station> withTop = MusicRepository::stations(pool, top);
    QCOMPARE(withTop.size(), 5);
    QCOMPARE(withTop.at(3).kind, StationKind::MoreLike);
    QCOMPARE(withTop.at(3).label, QStringLiteral("More like Nina Simone"));
    QCOMPARE(withTop.at(3).seedId, QStringLiteral("ar1"));
    for (const Station &station : withTop)
        QCOMPARE(station.covers.size(), 4);

    const QList<Station> without = MusicRepository::stations({}, Artist{});
    QCOMPARE(without.size(), 4);
    QVERIFY(without.first().covers.isEmpty());
}

void MusicRepositoryTest::heavyRotationIsShuffledPlayedTracks()
{
    QJsonArray tracks;
    for (int i = 0; i < 20; ++i)
        tracks.append(trackJson("h" + QString::number(i), "al", 1, i + 1));
    m_mock->addQueryRoute("GET", itemsPath(),
                          Q{{"IncludeItemTypes", "Audio"}, {"SortBy", "PlayCount"}, {"Filters", "IsPlayed"}}, 200,
                          page(tracks));
    Station station;
    station.kind = StationKind::HeavyRotation;
    const auto result = waitFor(m_repo->resolveStation(kLibrary, station));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.value.size(), 20);
    QStringList ids;
    for (const Track &track : result.value)
        ids.append(track.id);
    QStringList sorted = ids;
    std::sort(sorted.begin(), sorted.end());
    QVERIFY(ids != sorted || ids.first() != QStringLiteral("h0")); // order changed
    QCOMPARE(QSet<QString>(ids.cbegin(), ids.cend()).size(), 20);
}

void MusicRepositoryTest::moreLikeDeduplicatesInstantMix()
{
    m_mock->addRoute("GET", "/Items/ar1/InstantMix", 200,
                     page({trackJson("m1", "al", 1, 1), trackJson("m2", "al", 1, 2), trackJson("m1", "al", 1, 1)}));
    Station station;
    station.kind = StationKind::MoreLike;
    station.seedId = QStringLiteral("ar1");
    const auto result = waitFor(m_repo->resolveStation(kLibrary, station));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.value.size(), 2);
    QCOMPARE(QUrlQuery(m_mock->lastRequestFor("GET", "/Items/ar1/InstantMix").query).queryItemValue("UserId"),
             kUserId);
}

void MusicRepositoryTest::markStaleRefetchesListeningShelves()
{
    m_mock->addQueryRoute("GET", itemsPath(),
                          Q{{"IncludeItemTypes", "Audio"}, {"SortBy", "DatePlayed"}, {"Limit", "200"}}, 200,
                          page({playedTrack("x1", "alA", "ar1", "2026-09-15T20:00:00Z")}));
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"Ids", "alA"}}, 200, page({albumJson("alA")}));
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"Filters", "IsFavorite"}, {"IncludeItemTypes", "MusicAlbum"}},
                          200, page({albumJson("fav")}));

    QVERIFY(waitFor(m_repo->recentAlbums(kLibrary)).ok());
    QVERIFY(waitFor(m_repo->forgottenFavourites(kLibrary)).ok());
    int count = m_mock->requestCount();

    m_repo->markStale(Freshness::Listening);
    QVERIFY(waitFor(m_repo->forgottenFavourites(kLibrary)).ok());
    QCOMPARE(m_mock->requestCount(), count); // favourites untouched
    QVERIFY(waitFor(m_repo->recentAlbums(kLibrary)).ok());
    QVERIFY(m_mock->requestCount() > count);

    count = m_mock->requestCount();
    m_repo->markStale(Freshness::Favourites);
    QVERIFY(waitFor(m_repo->forgottenFavourites(kLibrary)).ok());
    QVERIFY(m_mock->requestCount() > count);
}
```

`playedTrack` gives every track `IndexNumber` 1. `continueListening` matches the resume position by track **id** in the album track list, so the mock order from `routeAlbum` (t1..t4) is what counts.

- [x] **Step 2: Run the tests to verify they fail**

Run: `cmake --build --preset dev --target tst_music_repository`
Expected: a compile error (`continueListening` and the other shelf methods are not members).

- [x] **Step 3: Extend the header**

Add to `MusicRepository.h`, above the class:

```cpp
struct NewAlbums
{
    QList<Album> albums;
    int addedThisWeek = -1; // -1: the server cannot say
};
```

Add these public members:

```cpp
    QFuture<Result<ContinueListening>> continueListening(const QString &libraryId);
    QFuture<Result<QList<Album>>> recentAlbums(const QString &libraryId, int limit = 20);
    QFuture<Result<NewAlbums>> newAlbums(const QString &libraryId, int limit = 20);
    QFuture<Result<QList<GenreBin>>> allGenres(const QString &libraryId);
    QFuture<Result<Page<GenreBin>>> genreBins(const QString &libraryId, int limit);
    QFuture<Result<QList<Artist>>> topArtists(const QString &libraryId, int limit = 20);
    QFuture<Result<QList<Album>>> forgottenFavourites(const QString &libraryId, int limit = 20);
    QFuture<Result<QList<Album>>> randomAlbums(const QString &libraryId, int limit = 20);
    static QList<Station> stations(const QList<Album> &coverPool, const Artist &topArtist);
    QFuture<Result<QList<Track>>> resolveStation(const QString &libraryId, const Station &station);
    void markStale(Freshness freshness);
```

Add these private members:

```cpp
    QFuture<Result<QList<Track>>> playedHistory(const QString &libraryId, int limit);
    QFuture<Result<QList<GenreBin>>> genreCountsFromAlbums(const QString &libraryId,
                                                           QList<GenreBin> genres);
    QFuture<Result<QList<ImageRef>>> genreCovers(const QString &libraryId, const QString &genreId);

    TtlCache<ContinueListening> m_continueCache{std::chrono::minutes(5)};
    TtlCache<QList<Album>> m_albumListCache{std::chrono::minutes(5)};
    TtlCache<NewAlbums> m_newCache{std::chrono::minutes(5)};
    TtlCache<QList<Artist>> m_artistListCache{std::chrono::minutes(5)};
    TtlCache<QList<GenreBin>> m_genreCache{std::chrono::minutes(5)};
    TtlCache<QList<ImageRef>> m_coverCache{std::chrono::milliseconds(-1)};
```

- [x] **Step 4: Implement**

Move `Pending`, `ready`, `itemsOf`, `kAudio` and `kAlbum` into the anonymous namespace at the top of `MusicRepository.cpp`, if they are not already there. Then add the following.

Extend `clear()`:

```cpp
void MusicRepository::clear()
{
    ++m_epoch;
    m_trackCache.clear();
    m_sleeveCache.clear();
    m_continueCache.clear();
    m_albumListCache.clear();
    m_newCache.clear();
    m_artistListCache.clear();
    m_genreCache.clear();
    m_coverCache.clear();
}
```

Add a helper in the anonymous namespace that reorders a result by requested ids:

```cpp
template<class T>
QList<T> orderedByIds(const QList<T> &items, const QStringList &ids)
{
    QHash<QString, T> byId;
    for (const T &item : items)
        byId.insert(item.id, item);
    QList<T> ordered;
    for (const QString &id : ids) {
        if (const auto it = byId.constFind(id); it != byId.cend())
            ordered.append(*it);
    }
    return ordered;
}

ItemsQuery libraryQuery(const QString &libraryId, const QString &type)
{
    ItemsQuery query;
    query.parentId = libraryId;
    query.includeItemTypes = {type};
    query.recursive = true;
    return query;
}
```

Then the methods:

```cpp
void MusicRepository::markStale(Freshness freshness)
{
    switch (freshness) {
    case Freshness::Listening:
        m_continueCache.markStale(QStringLiteral("home:"));
        m_albumListCache.markStale(QStringLiteral("home:"));
        m_artistListCache.markStale(QStringLiteral("home:"));
        break;
    case Freshness::Favourites:
        m_albumListCache.markStale(QStringLiteral("fav:"));
        break;
    case Freshness::Everything:
        m_trackCache.markStale();
        m_sleeveCache.markStale();
        m_continueCache.markStale();
        m_albumListCache.markStale();
        m_newCache.markStale();
        m_artistListCache.markStale();
        m_genreCache.markStale();
        break;
    }
}

QFuture<Result<QList<Track>>> MusicRepository::playedHistory(const QString &libraryId, int limit)
{
    ItemsQuery query = libraryQuery(libraryId, kAudio);
    query.sortBy = QStringLiteral("DatePlayed");
    query.sortDescending = true;
    query.filters = {QStringLiteral("IsPlayed")};
    query.fields = {QStringLiteral("DateCreated")};
    query.limit = limit;
    return fetchTracks(query);
}

QFuture<Result<ContinueListening>> MusicRepository::continueListening(const QString &libraryId)
{
    const QString key = QStringLiteral("home:continue:") + libraryId;
    if (auto cached = m_continueCache.get(key, now()))
        return ready(Result<ContinueListening>::success(*cached));

    Pending<ContinueListening> pending;
    const quint64 epoch = m_epoch;
    playedHistory(libraryId, 1).then(this, [this, pending, key, epoch](Result<QList<Track>> history) {
        if (!history.ok())
            return pending.resolve(Result<ContinueListening>::failure(history.error));
        if (history.value.isEmpty() || history.value.first().albumId.isEmpty()
            || !history.value.first().lastPlayed.isValid()) {
            return pending.resolve(Result<ContinueListening>::success({}));
        }
        const Track last = history.value.first();

        struct State
        {
            Result<Album> album;
            Result<QList<Track>> tracks;
        };
        auto state = std::make_shared<State>();
        auto fan = Fanout::create(this, [this, state, pending, last, key, epoch] {
            if (!state->album.ok())
                return pending.resolve(Result<ContinueListening>::failure(state->album.error));
            if (!state->tracks.ok())
                return pending.resolve(Result<ContinueListening>::failure(state->tracks.error));
            const QList<Track> &tracks = state->tracks.value;
            if (tracks.isEmpty())
                return pending.resolve(Result<ContinueListening>::success({}));

            int index = 0;
            for (int i = 0; i < tracks.size(); ++i) {
                if (tracks.at(i).id == last.id)
                    index = i;
            }
            const bool partial = last.positionMs > 0 && !last.played;
            int resume = partial ? index : index + 1;
            if (resume >= tracks.size())
                resume = 0;

            ContinueListening result;
            result.album = state->album.value;
            emby::refineAlbumFromTracks(result.album, tracks);
            result.resumeIndex = resume;
            result.resumeTrack = tracks.at(resume);
            result.progress = double(resume) / double(tracks.size());
            if (epochIs(epoch))
                m_continueCache.put(key, result, now());
            pending.resolve(Result<ContinueListening>::success(result));
        });
        fan->add<Album>(fetchItem(last.albumId).then(this, [](Result<QJsonDocument> r) {
            if (!r.ok())
                return Result<Album>::failure(r.error);
            return Result<Album>::success(emby::parseAlbum(r.value.object()));
        }), [state](Result<Album> r) { state->album = std::move(r); });
        fan->add<QList<Track>>(albumTracks(last.albumId),
                               [state](Result<QList<Track>> r) { state->tracks = std::move(r); });
        fan->seal();
    });
    return pending.future();
}

QFuture<Result<QList<Album>>> MusicRepository::recentAlbums(const QString &libraryId, int limit)
{
    const QString key = QStringLiteral("home:recent:%1:%2").arg(libraryId).arg(limit);
    if (auto cached = m_albumListCache.get(key, now()))
        return ready(Result<QList<Album>>::success(*cached));

    Pending<QList<Album>> pending;
    const quint64 epoch = m_epoch;
    playedHistory(libraryId, 200).then(this, [this, pending, key, epoch, libraryId, limit](Result<QList<Track>> history) {
        if (!history.ok())
            return pending.resolve(Result<QList<Album>>::failure(history.error));
        QStringList ids;
        for (const Track &track : std::as_const(history.value)) {
            if (!track.albumId.isEmpty() && !ids.contains(track.albumId) && ids.size() < limit)
                ids.append(track.albumId);
        }
        if (ids.isEmpty())
            return pending.resolve(Result<QList<Album>>::success({}));
        ItemsQuery query = libraryQuery(libraryId, kAlbum);
        query.ids = ids;
        query.fields = albumFields();
        query.limit = static_cast<int>(ids.size());
        fetchAlbums(query).then(this, [this, pending, key, epoch, ids](Result<QList<Album>> albums) {
            if (!albums.ok())
                return pending.resolve(albums);
            const QList<Album> ordered = orderedByIds(albums.value, ids);
            if (epochIs(epoch))
                m_albumListCache.put(key, ordered, now());
            pending.resolve(Result<QList<Album>>::success(ordered));
        });
    });
    return pending.future();
}

QFuture<Result<NewAlbums>> MusicRepository::newAlbums(const QString &libraryId, int limit)
{
    const QString key = QStringLiteral("lib:new:%1:%2").arg(libraryId).arg(limit);
    if (auto cached = m_newCache.get(key, now()))
        return ready(Result<NewAlbums>::success(*cached));

    auto state = std::make_shared<std::pair<Result<QList<Album>>, int>>(Result<QList<Album>>{}, -1);
    Pending<NewAlbums> pending;
    const quint64 epoch = m_epoch;
    auto fan = Fanout::create(this, [this, state, pending, key, epoch] {
        if (!state->first.ok())
            return pending.resolve(Result<NewAlbums>::failure(state->first.error));
        NewAlbums result{state->first.value, state->second};
        if (epochIs(epoch))
            m_newCache.put(key, result, now());
        pending.resolve(Result<NewAlbums>::success(result));
    });

    ItemsQuery albums = libraryQuery(libraryId, kAlbum);
    albums.sortBy = QStringLiteral("DateCreated");
    albums.sortDescending = true;
    albums.fields = albumFields();
    albums.limit = limit;
    fan->add<QList<Album>>(fetchAlbums(albums), [state](Result<QList<Album>> r) { state->first = std::move(r); });

    if (emby::caps::kMinDateCreated) {
        ItemsQuery count = libraryQuery(libraryId, kAlbum);
        count.minDateCreated = now().addDays(-7).toUTC().toString(Qt::ISODate);
        count.limit = 0;
        fan->add<QJsonDocument>(fetchItems(count), [state](Result<QJsonDocument> r) {
            if (r.ok())
                state->second = r.value.object().value(QStringLiteral("TotalRecordCount")).toInt(-1);
            else
                qCWarning(logApp) << "music: added-this-week count failed" << r.error;
        });
    }
    fan->seal();
    return pending.future();
}

QFuture<Result<QList<GenreBin>>> MusicRepository::allGenres(const QString &libraryId)
{
    const QString key = QStringLiteral("lib:genres:") + libraryId;
    if (auto cached = m_genreCache.get(key, now()))
        return ready(Result<QList<GenreBin>>::success(*cached));

    Pending<QList<GenreBin>> pending;
    const quint64 epoch = m_epoch;
    auto collected = std::make_shared<QList<GenreBin>>();
    auto step = std::make_shared<std::function<void(int)>>();
    *step = [this, pending, collected, step, libraryId, key, epoch](int startIndex) {
        const int pageSize = 200;
        QUrlQuery params;
        params.addQueryItem(QStringLiteral("UserId"), QStringLiteral("{uid}"));
        params.addQueryItem(QStringLiteral("ParentId"), libraryId);
        params.addQueryItem(QStringLiteral("StartIndex"), QString::number(startIndex));
        params.addQueryItem(QStringLiteral("Limit"), QString::number(pageSize));
        params.addQueryItem(QStringLiteral("SortBy"), QStringLiteral("SortName"));
        if (emby::caps::kGenreItemCounts)
            params.addQueryItem(QStringLiteral("Fields"), QStringLiteral("ItemCounts"));
        m_client->getJson(QStringLiteral("/MusicGenres"), params)
            .then(this, [this, pending, collected, step, startIndex, libraryId, key, epoch](Result<QJsonDocument> r) {
                if (!r.ok()) {
                    *step = nullptr; // break the self-reference
                    return pending.resolve(Result<QList<GenreBin>>::failure(r.error));
                }
                const QJsonArray items = itemsOf(r.value);
                for (const QJsonValue &value : items)
                    collected->append(emby::parseGenreBin(value.toObject()));
                // Page on the returned array's own size (ARCHITECTURE.md §2).
                if (items.size() == pageSize && startIndex / pageSize < 19)
                    return (*step)(startIndex + pageSize);
                *step = nullptr;

                auto finish = [this, pending, key, epoch](QList<GenreBin> genres) {
                    std::stable_sort(genres.begin(), genres.end(), [](const GenreBin &a, const GenreBin &b) {
                        if (a.recordCount != b.recordCount)
                            return a.recordCount > b.recordCount;
                        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
                    });
                    if (epochIs(epoch))
                        m_genreCache.put(key, genres, now());
                    pending.resolve(Result<QList<GenreBin>>::success(genres));
                };
                if (emby::caps::kGenreItemCounts)
                    return finish(*collected);
                genreCountsFromAlbums(libraryId, *collected).then(this, [pending, finish](Result<QList<GenreBin>> counted) {
                    if (!counted.ok())
                        return pending.resolve(counted);
                    finish(counted.value);
                });
            });
    };
    (*step)(0);
    return pending.future();
}

QFuture<Result<QList<GenreBin>>> MusicRepository::genreCountsFromAlbums(const QString &libraryId,
                                                                        QList<GenreBin> genres)
{
    Pending<QList<GenreBin>> pending;
    auto counts = std::make_shared<QHash<QString, int>>(); // key: id, or "name:" + lower-case name
    auto shared = std::make_shared<QList<GenreBin>>(std::move(genres));
    auto step = std::make_shared<std::function<void(int)>>();
    *step = [this, pending, counts, shared, step, libraryId](int startIndex) {
        const int pageSize = 1000;
        ItemsQuery query = libraryQuery(libraryId, kAlbum);
        query.fields = {QStringLiteral("Genres")};
        query.startIndex = startIndex;
        query.limit = pageSize;
        fetchItems(query).then(this, [pending, counts, shared, step, startIndex](Result<QJsonDocument> r) {
            if (!r.ok()) {
                *step = nullptr;
                return pending.resolve(Result<QList<GenreBin>>::failure(r.error));
            }
            const QJsonArray items = itemsOf(r.value);
            for (const QJsonValue &value : items) {
                for (const GenreRef &genre : emby::parseAlbum(value.toObject()).genres) {
                    ++(*counts)[genre.id.isEmpty() ? QStringLiteral("name:") + genre.name.toLower() : genre.id];
                }
            }
            if (items.size() == pageSize && startIndex / pageSize < 19)
                return (*step)(startIndex + pageSize);
            *step = nullptr;
            for (GenreBin &genre : *shared)
                genre.recordCount = counts->value(genre.id) + counts->value(QStringLiteral("name:") + genre.name.toLower());
            pending.resolve(Result<QList<GenreBin>>::success(*shared));
        });
    };
    (*step)(0);
    return pending.future();
}

QFuture<Result<QList<ImageRef>>> MusicRepository::genreCovers(const QString &libraryId, const QString &genreId)
{
    const QString key = QStringLiteral("covers:") + genreId;
    if (auto cached = m_coverCache.get(key, now()))
        return ready(Result<QList<ImageRef>>::success(*cached));
    ItemsQuery query = libraryQuery(libraryId, kAlbum);
    query.genreIds = {genreId};
    query.sortBy = QStringLiteral("Random");
    query.limit = 3;
    const quint64 epoch = m_epoch;
    return fetchAlbums(query).then(this, [this, key, epoch](Result<QList<Album>> r) {
        if (!r.ok())
            return Result<QList<ImageRef>>::failure(r.error);
        QList<ImageRef> covers;
        for (const Album &album : std::as_const(r.value)) {
            if (album.coverRef.isValid())
                covers.append(album.coverRef);
        }
        if (epochIs(epoch))
            m_coverCache.put(key, covers, now());
        return Result<QList<ImageRef>>::success(covers);
    });
}

QFuture<Result<Page<GenreBin>>> MusicRepository::genreBins(const QString &libraryId, int limit)
{
    Pending<Page<GenreBin>> pending;
    allGenres(libraryId).then(this, [this, pending, libraryId, limit](Result<QList<GenreBin>> all) {
        if (!all.ok())
            return pending.resolve(Result<Page<GenreBin>>::failure(all.error));
        auto page = std::make_shared<Page<GenreBin>>();
        page->items = all.value.mid(0, limit);
        page->totalRecordCount = static_cast<int>(all.value.size());
        auto fan = Fanout::create(this, [pending, page] {
            pending.resolve(Result<Page<GenreBin>>::success(*page));
        });
        for (int i = 0; i < page->items.size(); ++i) {
            fan->add<QList<ImageRef>>(genreCovers(libraryId, page->items.at(i).id),
                                      [page, i](Result<QList<ImageRef>> r) {
                                          if (r.ok())
                                              page->items[i].covers = r.value;
                                          else
                                              qCWarning(logApp) << "music: genre covers failed" << r.error;
                                      });
        }
        fan->seal();
    });
    return pending.future();
}

QFuture<Result<QList<Artist>>> MusicRepository::topArtists(const QString &libraryId, int limit)
{
    const QString key = QStringLiteral("home:top:%1:%2").arg(libraryId).arg(limit);
    if (auto cached = m_artistListCache.get(key, now()))
        return ready(Result<QList<Artist>>::success(*cached));

    Pending<QList<Artist>> pending;
    const quint64 epoch = m_epoch;
    playedHistory(libraryId, 500).then(this, [this, pending, key, epoch, limit](Result<QList<Track>> history) {
        if (!history.ok())
            return pending.resolve(Result<QList<Artist>>::failure(history.error));
        QStringList order;
        QHash<QString, int> plays;
        for (const Track &track : std::as_const(history.value)) {
            if (track.artists.isEmpty() || track.artists.first().id.isEmpty())
                continue;
            const QString id = track.artists.first().id;
            if (!plays.contains(id))
                order.append(id);
            ++plays[id];
        }
        std::stable_sort(order.begin(), order.end(),
                         [&](const QString &a, const QString &b) { return plays.value(a) > plays.value(b); });
        const QStringList ids = order.mid(0, limit);
        if (ids.isEmpty())
            return pending.resolve(Result<QList<Artist>>::success({}));
        ItemsQuery query;
        query.ids = ids;
        query.limit = static_cast<int>(ids.size());
        fetchItems(query).then(this, [this, pending, key, epoch, ids](Result<QJsonDocument> r) {
            if (!r.ok())
                return pending.resolve(Result<QList<Artist>>::failure(r.error));
            const QList<Artist> ordered = orderedByIds(emby::parseArtists(itemsOf(r.value)), ids);
            if (epochIs(epoch))
                m_artistListCache.put(key, ordered, now());
            pending.resolve(Result<QList<Artist>>::success(ordered));
        });
    });
    return pending.future();
}

QFuture<Result<QList<Album>>> MusicRepository::forgottenFavourites(const QString &libraryId, int limit)
{
    const QString key = QStringLiteral("fav:forgotten:%1:%2").arg(libraryId).arg(limit);
    if (auto cached = m_albumListCache.get(key, now()))
        return ready(Result<QList<Album>>::success(*cached));
    ItemsQuery query = libraryQuery(libraryId, kAlbum);
    query.filters = {QStringLiteral("IsFavorite")};
    query.sortBy = QStringLiteral("DatePlayed");
    query.fields = albumFields();
    query.limit = limit;
    const quint64 epoch = m_epoch;
    return fetchAlbums(query).then(this, [this, key, epoch](Result<QList<Album>> r) {
        if (r.ok() && epochIs(epoch))
            m_albumListCache.put(key, r.value, now());
        return r;
    });
}

QFuture<Result<QList<Album>>> MusicRepository::randomAlbums(const QString &libraryId, int limit)
{
    ItemsQuery query = libraryQuery(libraryId, kAlbum);
    query.sortBy = QStringLiteral("Random");
    query.fields = albumFields();
    query.limit = limit;
    return fetchAlbums(query);
}

QList<Station> MusicRepository::stations(const QList<Album> &coverPool, const Artist &topArtist)
{
    QList<ImageRef> covers;
    for (const Album &album : coverPool) {
        if (album.coverRef.isValid())
            covers.append(album.coverRef);
    }
    QList<Station> list;
    auto add = [&](StationKind kind, const QString &label, const QString &seed = QString()) {
        Station station{kind, label, seed, {}};
        if (!covers.isEmpty()) {
            const qsizetype offset = list.size() * 4;
            for (qsizetype k = 0; k < 4; ++k)
                station.covers.append(covers.at((offset + k) % covers.size()));
        }
        list.append(station);
    };
    add(StationKind::HeavyRotation, QStringLiteral("Heavy rotation"));
    add(StationKind::Favourites, QStringLiteral("Favourites"));
    add(StationKind::DeepCuts, QStringLiteral("Deep cuts"));
    if (!topArtist.id.isEmpty())
        add(StationKind::MoreLike, QStringLiteral("More like %1").arg(topArtist.name), topArtist.id);
    add(StationKind::ShuffleAll, QStringLiteral("Shuffle all"));
    return list;
}

QFuture<Result<QList<Track>>> MusicRepository::resolveStation(const QString &libraryId, const Station &station)
{
    if (station.kind == StationKind::MoreLike) {
        if (station.seedId.isEmpty())
            return ready(Result<QList<Track>>::failure(QStringLiteral("no seed for station")));
        QUrlQuery params;
        params.addQueryItem(QStringLiteral("UserId"), QStringLiteral("{uid}"));
        params.addQueryItem(QStringLiteral("Limit"), QStringLiteral("200"));
        params.addQueryItem(QStringLiteral("Fields"), trackFields().join(QLatin1Char(',')));
        return m_client->getJson(QStringLiteral("/Items/%1/InstantMix").arg(station.seedId), params)
            .then(this, [](Result<QJsonDocument> r) {
                if (!r.ok())
                    return Result<QList<Track>>::failure(r.error);
                QList<Track> unique;
                QSet<QString> seen;
                for (const Track &track : emby::parseTracks(itemsOf(r.value))) {
                    if (!track.id.isEmpty() && !seen.contains(track.id)) {
                        seen.insert(track.id);
                        unique.append(track);
                    }
                }
                return Result<QList<Track>>::success(unique);
            });
    }

    ItemsQuery query = libraryQuery(libraryId, kAudio);
    query.fields = trackFields();
    query.limit = 200;
    query.sortBy = QStringLiteral("Random");
    switch (station.kind) {
    case StationKind::HeavyRotation:
        query.sortBy = QStringLiteral("PlayCount");
        query.sortDescending = true;
        query.filters = {QStringLiteral("IsPlayed")};
        break;
    case StationKind::Favourites:
        query.filters = {QStringLiteral("IsFavorite")};
        break;
    case StationKind::DeepCuts:
        query.filters = {QStringLiteral("IsUnplayed")};
        break;
    case StationKind::ShuffleAll:
    case StationKind::MoreLike:
        break;
    }
    const bool shuffle = station.kind == StationKind::HeavyRotation;
    return fetchTracks(query).then(this, [this, shuffle](Result<QList<Track>> r) {
        if (r.ok() && shuffle)
            std::shuffle(r.value.begin(), r.value.end(), m_rng);
        return r;
    });
}
```

Add `#include <QHash>` and `#include <algorithm>` to the `.cpp`.

In `heavyRotationIsShuffledPlayedTracks`, seed 7 produces a fixed permutation. The assertion accepts any order except the identity, which a 20-element shuffle essentially never produces.

- [x] **Step 5: Run the tests to verify they pass**

Run: `cmake --build --preset dev --target tst_music_repository && ctest --preset dev -R tst_music_repository --output-on-failure`
Expected: PASS (17 tests).

- [x] **Step 6: Commit**

```bash
git add src/app/music/MusicRepository.* tests/integration/tst_music_repository.cpp
git commit -m "feat(music): compose Home shelves, genre bins and stations

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 12: `MusicRepository`, part 3: browse, samples, user-data invalidation

**Files:**
- Modify: `src/app/music/MusicRepository.h`, `src/app/music/MusicRepository.cpp`
- Test: `tests/integration/tst_music_repository.cpp`

**Interfaces:**
- Consumes: `MusicQueryTranslator::toItemsQuery` (Task 9), `EmbyClient::artistParams` (Task 1), `caps::kHiResQueryKey`/`kHiResQueryValue` (Task 2), Tasks 10–11.
- Produces (public on `MusicRepository`):
  - `QFuture<Result<Page<Album>>> browseAlbums(const MusicQuery &query, int startIndex, int limit)`
  - `QFuture<Result<Page<Artist>>> browseArtists(const MusicQuery &query, int startIndex, int limit)`
  - `QFuture<Result<Page<Track>>> browseTracks(const MusicQuery &query, int startIndex, int limit)`
  - `QFuture<Result<Page<Playlist>>> browsePlaylists(const MusicQuery &query, int startIndex, int limit)`
  - `QFuture<Result<QList<Track>>> sampleTracks(const MusicQuery &query, int limit = 200)`: random tracks across the filtered scope, for ⇄ Shuffle and ▶ Play on a filtered view
  - `void noteUserDataChanged(const QString &itemId)`

Rules:
- `totalRecordCount` of a page is `max(TotalRecordCount, startIndex + items.size())`, so a server that under-reports never hides a page.
- Artists:
  - `/Artists/AlbumArtists` for `ArtistMode::AlbumArtists`, otherwise `/Artists`.
  - Query string: `EmbyClient::artistParams("{uid}", toItemsQuery(query…))` plus `Fields=ItemCounts,DateCreated`.
- Tracks: when `query.format == FormatFilter::HiRes && caps::kHiResFilter`, append `kHiResQueryKey=kHiResQueryValue` to the query string.
- `sampleTracks`:
  - Translates the query as `Section::Songs` with `sortKey = "random"`, `startIndex` 0 and the given limit. The letter is dropped, because a sample spans the whole filtered scope.
  - `ParentId` stays the library.
- `noteUserDataChanged(id)` removes every cached track list, sleeve and continue-listening entry that contains the id (as an album, track or more-by album), then calls `markStale(Freshness::Favourites)`.

- [x] **Step 1: Write the failing tests**

Add these slots:

```cpp
    void browseAlbumsPagesWithTheTranslatedQuery();
    void browseArtistsPicksTheEndpointByMode();
    void browseTracksAddsHiResOnlyWhenMeasured();
    void sampleTracksIsRandomAcrossTheFilteredScope();
    void userDataChangeDropsCachesHoldingTheItem();
```

```cpp
void MusicRepositoryTest::browseAlbumsPagesWithTheTranslatedQuery()
{
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"IncludeItemTypes", "MusicAlbum"}, {"StartIndex", "50"}}, 200,
                          page({albumJson("b1"), albumJson("b2")}, 10)); // under-reported total
    MusicQuery query;
    query.libraryId = kLibrary;
    query.sortKey = QStringLiteral("year");
    query.descending = true;
    query.favouritesOnly = true;
    const auto result = waitFor(m_repo->browseAlbums(query, 50, 50));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.value.items.size(), 2);
    QCOMPARE(result.value.startIndex, 50);
    QCOMPARE(result.value.totalRecordCount, 52);
    const QUrlQuery sent(m_mock->lastRequestFor("GET", itemsPath()).query);
    QCOMPARE(sent.queryItemValue("SortBy"), QStringLiteral("ProductionYear,PremiereDate,SortName"));
    QCOMPARE(sent.queryItemValue("SortOrder"), QStringLiteral("Descending"));
    QCOMPARE(sent.queryItemValue("Filters"), QStringLiteral("IsFavorite"));
    QCOMPARE(sent.queryItemValue("ParentId"), kLibrary);
}

void MusicRepositoryTest::browseArtistsPicksTheEndpointByMode()
{
    const QByteArray body = page({QJsonObject{{"Id", "ar1"}, {"Name", "A"}, {"AlbumCount", 4}}}, 1);
    m_mock->addRoute("GET", "/Artists/AlbumArtists", 200, body);
    m_mock->addRoute("GET", "/Artists", 200, body);
    MusicQuery query;
    query.libraryId = kLibrary;
    query.section = Section::Artists;
    query.letter = QStringLiteral("A");

    const auto filed = waitFor(m_repo->browseArtists(query, 0, 100));
    QVERIFY2(filed.ok(), qPrintable(filed.error));
    QCOMPARE(filed.value.items.first().albumCount, 4);
    const QUrlQuery sent(m_mock->lastRequestFor("GET", "/Artists/AlbumArtists").query);
    QCOMPARE(sent.queryItemValue("UserId"), kUserId);
    QCOMPARE(sent.queryItemValue("NameStartsWithOrGreater"), QStringLiteral("A"));
    QCOMPARE(sent.queryItemValue("NameLessThan"), QStringLiteral("B"));
    QVERIFY(sent.queryItemValue("Fields").contains("ItemCounts"));

    query.artistMode = ArtistMode::Everyone;
    QVERIFY(waitFor(m_repo->browseArtists(query, 0, 100)).ok());
    QCOMPARE(m_mock->lastRequestFor("GET", "/Artists").path, QStringLiteral("/Artists"));
}

void MusicRepositoryTest::browseTracksAddsHiResOnlyWhenMeasured()
{
    m_mock->addRoute("GET", itemsPath(), 200, page({trackJson("t", "al", 1, 1)}));
    MusicQuery query;
    query.libraryId = kLibrary;
    query.section = Section::Songs;
    query.format = FormatFilter::HiRes;
    QVERIFY(waitFor(m_repo->browseTracks(query, 0, 100)).ok());
    const QUrlQuery sent(m_mock->lastRequestFor("GET", itemsPath()).query);
    const QString key = QString::fromLatin1(emby::caps::kHiResQueryKey);
    if (emby::caps::kHiResFilter && emby::caps::kAudioCodecsFiltersAudio)
        QCOMPARE(sent.queryItemValue(key), QString::fromLatin1(emby::caps::kHiResQueryValue));
    else
        QVERIFY(key.isEmpty() || !sent.hasQueryItem(key));
}

void MusicRepositoryTest::sampleTracksIsRandomAcrossTheFilteredScope()
{
    m_mock->addRoute("GET", itemsPath(), 200, page({trackJson("s1", "al", 1, 1)}));
    MusicQuery query;
    query.libraryId = kLibrary;
    query.section = Section::Albums;
    query.sortKey = QStringLiteral("name");
    query.letter = QStringLiteral("Q");
    query.genreIds = {QStringLiteral("g1")};
    const auto result = waitFor(m_repo->sampleTracks(query, 150));
    QVERIFY2(result.ok(), qPrintable(result.error));
    const QUrlQuery sent(m_mock->lastRequestFor("GET", itemsPath()).query);
    QCOMPARE(sent.queryItemValue("IncludeItemTypes"), QStringLiteral("Audio"));
    QCOMPARE(sent.queryItemValue("SortBy"), QStringLiteral("Random"));
    QCOMPARE(sent.queryItemValue("GenreIds"), QStringLiteral("g1"));
    QCOMPARE(sent.queryItemValue("Limit"), QStringLiteral("150"));
    QVERIFY(!sent.hasQueryItem("NameLessThan"));
}

void MusicRepositoryTest::userDataChangeDropsCachesHoldingTheItem()
{
    routeAlbum(QStringLiteral("al1"), 3);
    routeAlbum(QStringLiteral("al2"), 3);
    QVERIFY(waitFor(m_repo->albumSleeve(QStringLiteral("al1"))).ok());
    QVERIFY(waitFor(m_repo->albumSleeve(QStringLiteral("al2"))).ok());
    const int cached = m_mock->requestCount();

    m_repo->noteUserDataChanged(QStringLiteral("al1-t2"));
    QVERIFY(waitFor(m_repo->albumSleeve(QStringLiteral("al2"))).ok());
    QCOMPARE(m_mock->requestCount(), cached); // untouched album stays cached
    QVERIFY(waitFor(m_repo->albumSleeve(QStringLiteral("al1"))).ok());
    QVERIFY(m_mock->requestCount() > cached);
}
```

`routeAlbum` does not route "more by" for `ar1`, so those requests 404 and come back as empty secondaries, which is fine for these tests.

- [x] **Step 2: Run the tests to verify they fail**

Run: `cmake --build --preset dev --target tst_music_repository`
Expected: a compile error (`browseAlbums` is not a member).

- [x] **Step 3: Extend the header**

Add `#include "server/dto/music/MusicQuery.h"` if it is not already included. Add these public members:

```cpp
    QFuture<Result<Page<Album>>> browseAlbums(const MusicQuery &query, int startIndex, int limit);
    QFuture<Result<Page<Artist>>> browseArtists(const MusicQuery &query, int startIndex, int limit);
    QFuture<Result<Page<Track>>> browseTracks(const MusicQuery &query, int startIndex, int limit);
    QFuture<Result<Page<Playlist>>> browsePlaylists(const MusicQuery &query, int startIndex, int limit);
    QFuture<Result<QList<Track>>> sampleTracks(const MusicQuery &query, int limit = 200);
    void noteUserDataChanged(const QString &itemId);
```

- [x] **Step 4: Implement**

Add `#include "app/music/MusicQueryTranslator.h"`. Put this helper in the anonymous namespace:

```cpp
template<class T, class Parse>
Result<Page<T>> toPage(const Result<QJsonDocument> &result, int startIndex, Parse parse)
{
    if (!result.ok())
        return Result<Page<T>>::failure(result.error);
    Page<T> page;
    page.items = parse(itemsOf(result.value));
    page.startIndex = startIndex;
    page.totalRecordCount = qMax(result.value.object().value(QStringLiteral("TotalRecordCount")).toInt(),
                                 startIndex + static_cast<int>(page.items.size()));
    return Result<Page<T>>::success(page);
}
```

Then the methods:

```cpp
QFuture<Result<Page<Album>>> MusicRepository::browseAlbums(const MusicQuery &query, int startIndex, int limit)
{
    MusicQuery scoped = query;
    scoped.section = Section::Albums;
    const ItemsQuery items = MusicQueryTranslator::toItemsQuery(scoped, startIndex, limit);
    return fetchItems(items).then(this, [startIndex](Result<QJsonDocument> r) {
        return toPage<Album>(r, startIndex, &emby::parseAlbums);
    });
}

QFuture<Result<Page<Artist>>> MusicRepository::browseArtists(const MusicQuery &query, int startIndex, int limit)
{
    MusicQuery scoped = query;
    scoped.section = Section::Artists;
    const ItemsQuery items = MusicQueryTranslator::toItemsQuery(scoped, startIndex, limit);
    QUrlQuery params = emby::EmbyClient::artistParams(QStringLiteral("{uid}"), items);
    params.addQueryItem(QStringLiteral("Fields"), QStringLiteral("ItemCounts,DateCreated"));
    const QString path = query.artistMode == ArtistMode::AlbumArtists ? QStringLiteral("/Artists/AlbumArtists")
                                                                      : QStringLiteral("/Artists");
    return m_client->getJson(path, params).then(this, [startIndex](Result<QJsonDocument> r) {
        return toPage<Artist>(r, startIndex, &emby::parseArtists);
    });
}

QFuture<Result<Page<Track>>> MusicRepository::browseTracks(const MusicQuery &query, int startIndex, int limit)
{
    MusicQuery scoped = query;
    scoped.section = Section::Songs;
    QUrlQuery params = emby::EmbyClient::itemsParams(MusicQueryTranslator::toItemsQuery(scoped, startIndex, limit));
    if (query.format == FormatFilter::HiRes && emby::caps::kHiResFilter
        && MusicQueryTranslator::formatFilterable(Section::Songs)) {
        params.addQueryItem(QString::fromLatin1(emby::caps::kHiResQueryKey),
                            QString::fromLatin1(emby::caps::kHiResQueryValue));
    }
    return m_client->getJson(QStringLiteral("/Users/{uid}/Items"), params)
        .then(this, [startIndex](Result<QJsonDocument> r) {
            return toPage<Track>(r, startIndex, &emby::parseTracks);
        });
}

QFuture<Result<Page<Playlist>>> MusicRepository::browsePlaylists(const MusicQuery &query, int startIndex, int limit)
{
    MusicQuery scoped = query;
    scoped.section = Section::Playlists;
    const ItemsQuery items = MusicQueryTranslator::toItemsQuery(scoped, startIndex, limit);
    return fetchItems(items).then(this, [startIndex](Result<QJsonDocument> r) {
        return toPage<Playlist>(r, startIndex, &emby::parsePlaylists);
    });
}

QFuture<Result<QList<Track>>> MusicRepository::sampleTracks(const MusicQuery &query, int limit)
{
    MusicQuery scoped = query;
    scoped.section = Section::Songs;
    scoped.sortKey = QStringLiteral("random");
    scoped.letter.clear();
    return browseTracks(scoped, 0, limit).then(this, [](Result<Page<Track>> r) {
        if (!r.ok())
            return Result<QList<Track>>::failure(r.error);
        return Result<QList<Track>>::success(r.value.items);
    });
}

void MusicRepository::noteUserDataChanged(const QString &itemId)
{
    if (itemId.isEmpty())
        return;
    auto hasTrack = [&](const QList<Track> &tracks) {
        return std::any_of(tracks.cbegin(), tracks.cend(), [&](const Track &t) { return t.id == itemId; });
    };
    m_trackCache.removeIf([&](const QString &, const QList<Track> &tracks) { return hasTrack(tracks); });
    m_sleeveCache.removeIf([&](const QString &, const AlbumSleeve &sleeve) {
        if (sleeve.album.id == itemId)
            return true;
        for (const Disc &disc : sleeve.discs) {
            if (hasTrack(disc.tracks))
                return true;
        }
        return std::any_of(sleeve.moreByArtist.cbegin(), sleeve.moreByArtist.cend(),
                           [&](const Album &album) { return album.id == itemId; });
    });
    m_continueCache.removeIf([&](const QString &, const ContinueListening &entry) {
        return entry.album.id == itemId || entry.resumeTrack.id == itemId;
    });
    markStale(Freshness::Favourites);
}
```

`toPage<Album>(r, startIndex, &emby::parseAlbums)` passes a function pointer; `parseAlbums` has one overload, so the address is unambiguous.

`ItemsQuery.limit` of 0 is sent as `Limit=0`, and `startIndex` is echoed as given.

- [x] **Step 5: Run the tests to verify they pass**

Run: `cmake --build --preset dev --target tst_music_repository && ctest --preset dev -R tst_music_repository --output-on-failure`
Expected: PASS (22 tests).

- [x] **Step 6: Commit**

```bash
git add src/app/music/MusicRepository.* tests/integration/tst_music_repository.cpp
git commit -m "feat(music): page music browse queries and invalidate on user data

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 13: Music models, part 1: base, album/artist/playlist grids, `mapForItem`

**Files:**
- Create: `src/app/music/UserDataPatch.h`
- Create: `src/app/music/models/MusicModelBase.h/.cpp`, `MusicListModel.h`, `AlbumGridModel.h/.cpp`, `ArtistGridModel.h/.cpp`, `PlaylistGridModel.h/.cpp`
- Modify: `src/app/models/MediaItemModel.h/.cpp` (static `mapForItem`)
- Modify: `src/CMakeLists.txt` (`strmqt_app`)
- Test: `tests/unit/tst_music_models.cpp`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: DTOs and `toMediaItem` (Task 3); `MusicFormat` (Task 9); `MediaItemModel::dataForItem`/`mediaRoleNames`.
- Produces:
  - `struct UserDataPatch { std::optional<bool> favourite; std::optional<bool> played; std::optional<int> playCount; std::optional<qint64> positionMs; }` (namespace `strmqt::music`)
  - `static QVariantMap MediaItemModel::mapForItem(const MediaItem &item)`
  - `class MusicModelBase : QAbstractListModel`:
    - properties `count`, `totalRecordCount`, `canLoadMore`
    - `Q_INVOKABLE virtual QVariantMap get(int row) const = 0`
    - `Q_INVOKABLE int indexOfNavigationIdentity(const QString &)`, `Q_INVOKABLE QString idAt(int row) const`
    - `virtual void applyUserData(const QString &itemId, const UserDataPatch &patch) = 0`
    - signals `countChanged()`, `totalRecordCountChanged()`
  - `template<class T> class MusicListModel : MusicModelBase`: `setItems(QList<T>, int total = -1)`, `appendItems(const QList<T>&, int total = -1)`, `clear()`, `items()`, `at(int)`
  - `AlbumGridModel`, `ArtistGridModel`, `PlaylistGridModel` (namespace `strmqt::music`), each `: MusicListModel<Album|Artist|Playlist>` with `Q_OBJECT`

Roles (QML names). Every model also answers `get(row)` with its media-role map (`mapForItem`) merged with these:

| Model | Roles |
|---|---|
| Album | `itemId, title, name, artist, artistId, subtitle ("Artist · 1973"), year, releaseType ("Album"/"EP"/…), releaseBadge ("" for Album), coverUrl, posterUrl, formatBadge, trackCount, durationText, favourite, favorite, playCount, type ("MusicAlbum")` |
| Artist | `itemId, name, title, subtitle ("12 records"), recordCount, coverUrl, posterUrl, backdropUrl, favourite, favorite, type ("MusicArtist")` |
| Playlist | `itemId, name, title, subtitle ("31 tracks · 2 h"), trackCount, coverUrl, posterUrl, type ("Playlist")` |

`posterUrl` and `favorite` duplicate `coverUrl` and `favourite`, so `StrmRail`, `StrmGrid` and `ItemMenu` keep working unchanged.

- [x] **Step 1: Write the failing tests**

`tests/unit/tst_music_models.cpp`:

```cpp
#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QtTest>

#include "app/models/MediaItemModel.h"
#include "app/music/models/AlbumGridModel.h"
#include "app/music/models/ArtistGridModel.h"
#include "app/music/models/PlaylistGridModel.h"

using namespace strmqt;
using namespace strmqt::music;

namespace {

Album album(const QString &id, int year = 1973, ReleaseType type = ReleaseType::Album)
{
    Album a;
    a.id = id;
    a.title = QStringLiteral("Title ") + id;
    a.albumArtists = {{QStringLiteral("ar1"), QStringLiteral("Pink Floyd")}};
    a.year = year;
    a.trackCount = 9;
    a.runtimeMs = 43 * 60'000;
    a.releaseType = type;
    a.coverRef = {id, QStringLiteral("Primary"), QStringLiteral("tag")};
    return a;
}

QVariant role(const QAbstractItemModel &model, int row, const QByteArray &name)
{
    const auto names = model.roleNames();
    return model.data(model.index(row, 0), names.key(name));
}

} // namespace

class MusicModelsTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { setEmbyImageSourceNamespace(QStringLiteral("t")); }
    void albumRolesAndGet();
    void pagingAndIdentity();
    void albumUserDataPatchesInPlace();
    void artistAndPlaylistRoles();
    void mapForItemMatchesMediaModelGet();
};

void MusicModelsTest::albumRolesAndGet()
{
    AlbumGridModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    model.setItems({album("a1"), album("a2", 0, ReleaseType::EP)});
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(role(model, 0, "title").toString(), QStringLiteral("Title a1"));
    QCOMPARE(role(model, 0, "artist").toString(), QStringLiteral("Pink Floyd"));
    QCOMPARE(role(model, 0, "subtitle").toString(), QStringLiteral("Pink Floyd · 1973"));
    QCOMPARE(role(model, 1, "subtitle").toString(), QStringLiteral("Pink Floyd")); // no year
    QCOMPARE(role(model, 0, "releaseBadge").toString(), QString());
    QCOMPARE(role(model, 1, "releaseBadge").toString(), QStringLiteral("EP"));
    QCOMPARE(role(model, 0, "coverUrl").toString(), QStringLiteral("image://emby/t/a1/Primary/tag"));
    QCOMPARE(role(model, 0, "posterUrl"), role(model, 0, "coverUrl"));
    QCOMPARE(role(model, 0, "durationText").toString(), QStringLiteral("43 min"));
    QCOMPARE(role(model, 0, "type").toString(), QStringLiteral("MusicAlbum"));

    const QVariantMap map = model.get(0);
    QCOMPARE(map.value("itemId").toString(), QStringLiteral("a1"));
    QCOMPARE(map.value("type").toString(), QStringLiteral("MusicAlbum"));
    QCOMPARE(map.value("albumArtist").toString(), QStringLiteral("Pink Floyd")); // media role
    QCOMPARE(map.value("releaseType").toString(), QStringLiteral("Album"));     // music role
    QVERIFY(model.get(7).isEmpty());
}

void MusicModelsTest::pagingAndIdentity()
{
    AlbumGridModel model;
    QSignalSpy count(&model, &MusicModelBase::countChanged);
    QSignalSpy total(&model, &MusicModelBase::totalRecordCountChanged);
    model.setItems({album("a1"), album("a2")}, 5);
    QVERIFY(model.canLoadMore());
    QCOMPARE(model.totalRecordCount(), 5);
    model.appendItems({album("a3"), album("a1")}, 4); // under-reported: floored at 4 rows
    QCOMPARE(model.rowCount(), 4);
    QCOMPARE(model.totalRecordCount(), 4);
    QVERIFY(!model.canLoadMore());
    QCOMPARE(model.indexOfNavigationIdentity(QStringLiteral("i:a3")), 2);
    QCOMPARE(model.indexOfNavigationIdentity(QStringLiteral("i:a1")), 0); // first occurrence
    QCOMPARE(model.indexOfNavigationIdentity(QStringLiteral("i:zz")), -1);
    QCOMPARE(model.idAt(1), QStringLiteral("a2"));
    QVERIFY(count.count() >= 2);
    QVERIFY(total.count() >= 2);
    model.clear();
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.totalRecordCount(), 0);
}

void MusicModelsTest::albumUserDataPatchesInPlace()
{
    AlbumGridModel model;
    model.setItems({album("a1"), album("a2"), album("a1")});
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    UserDataPatch patch;
    patch.favourite = true;
    patch.playCount = 3;
    model.applyUserData(QStringLiteral("a1"), patch);
    QCOMPARE(changed.count(), 2); // both rows holding a1
    QVERIFY(role(model, 0, "favourite").toBool());
    QVERIFY(role(model, 2, "favorite").toBool());
    QCOMPARE(role(model, 0, "playCount").toInt(), 3);
    model.applyUserData(QStringLiteral("nope"), patch);
    QCOMPARE(changed.count(), 2);
}

void MusicModelsTest::artistAndPlaylistRoles()
{
    ArtistGridModel artists;
    Artist artist;
    artist.id = QStringLiteral("ar1");
    artist.name = QStringLiteral("Björk");
    artist.albumCount = 1;
    artist.coverRef = {QStringLiteral("ar1"), QStringLiteral("Primary"), QStringLiteral("p")};
    artists.setItems({artist});
    QCOMPARE(role(artists, 0, "subtitle").toString(), QStringLiteral("1 record"));
    QCOMPARE(role(artists, 0, "recordCount").toInt(), 1);
    QCOMPARE(artists.get(0).value("type").toString(), QStringLiteral("MusicArtist"));

    PlaylistGridModel playlists;
    Playlist playlist;
    playlist.id = QStringLiteral("pl1");
    playlist.name = QStringLiteral("Road trip");
    playlist.trackCount = 31;
    playlist.runtimeMs = 120 * 60'000;
    playlists.setItems({playlist});
    QCOMPARE(role(playlists, 0, "subtitle").toString(), QStringLiteral("31 tracks · 2 h"));
    QCOMPARE(playlists.get(0).value("type").toString(), QStringLiteral("Playlist"));
    QCOMPARE(playlists.get(0).value("childCount").toInt(), 31);
}

void MusicModelsTest::mapForItemMatchesMediaModelGet()
{
    MediaItem item;
    item.id = QStringLiteral("x");
    item.name = QStringLiteral("X");
    item.type = QStringLiteral("Audio");
    item.runtimeTicks = 1000 * kTicksPerMs;
    MediaItemModel model;
    model.setItems({item});
    QCOMPARE(MediaItemModel::mapForItem(item), model.get(0));
}

QTEST_GUILESS_MAIN(MusicModelsTest)
#include "tst_music_models.moc"
```

Register:

```cmake
strmqt_add_test(tst_music_models unit/tst_music_models.cpp)
```

- [x] **Step 2: Run the tests to verify they fail**

Run: `cmake --preset dev && cmake --build --preset dev --target tst_music_models`
Expected: a compile error (headers not found).

- [x] **Step 3: Add `MediaItemModel::mapForItem`**

In `MediaItemModel.h`, next to `dataForItem`:

```cpp
    // The map get() returns, for an item that is not in a model (music models
    // build it from their DTOs so every consumer sees the same keys).
    static QVariantMap mapForItem(const MediaItem &item);
```

In `MediaItemModel.cpp`, replace the body of `get()` and add `mapForItem`:

```cpp
QVariantMap MediaItemModel::mapForItem(const MediaItem &item)
{
    QVariantMap map;
    const auto &roles = mediaRoleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        map.insert(QString::fromLatin1(it.value()), dataForItem(item, it.key()));
    return map;
}

QVariantMap MediaItemModel::get(int row) const
{
    if (row < 0 || row >= m_items.size())
        return {};
    return mapForItem(m_items.at(row));
}
```

`MediaItemModel::data()` is already a straight `dataForItem(m_items[row], role)` (`MediaItemModel.cpp:141`), so the two are equivalent. `mapForItemMatchesMediaModelGet` keeps them that way.

- [x] **Step 4: Write `UserDataPatch.h`, the base and the list template**

`src/app/music/UserDataPatch.h`:

```cpp
#pragma once

#include <QtGlobal>

#include <optional>

namespace strmqt::music {

// A partial user-data change. Unset fields are left alone: a local favourite
// toggle says nothing about play counts.
struct UserDataPatch
{
    std::optional<bool> favourite;
    std::optional<bool> played;
    std::optional<int> playCount;
    std::optional<qint64> positionMs;
};

} // namespace strmqt::music
```

`src/app/music/models/MusicModelBase.h`:

```cpp
#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QVariantMap>

#include "app/music/UserDataPatch.h"

namespace strmqt::music {

// Contract shared by every music model (Crate spec §3.4): count, paging,
// get(row) with MediaItemModel's keys, and the navigation identity lookup
// NavigationFocusRestorer uses ("i:<id>" or "p:<playlistItemId>").
class MusicModelBase : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int totalRecordCount READ totalRecordCount NOTIFY totalRecordCountChanged)
    Q_PROPERTY(bool canLoadMore READ canLoadMore NOTIFY totalRecordCountChanged)

public:
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : size();
    }
    int count() const { return size(); }
    int totalRecordCount() const { return m_total; }
    bool canLoadMore() const { return size() < m_total; }

    Q_INVOKABLE virtual QVariantMap get(int row) const = 0;
    Q_INVOKABLE int indexOfNavigationIdentity(const QString &identity) const;
    Q_INVOKABLE QString idAt(int row) const;

    virtual void applyUserData(const QString &itemId, const UserDataPatch &patch) = 0;

signals:
    void countChanged();
    void totalRecordCountChanged();

protected:
    virtual int size() const = 0;
    virtual QString idOf(int row) const = 0;
    virtual QString playlistItemIdOf(int row) const
    {
        Q_UNUSED(row);
        return {};
    }

    void rebuildIndex();
    void extendIndex(int fromRow);
    void setTotal(int total);
    QList<int> rowsFor(const QString &itemId) const { return m_rowsById.value(itemId); }

private:
    void indexRow(int row);

    QHash<QString, int> m_rowByIdentity;
    QHash<QString, QList<int>> m_rowsById;
    int m_total = 0;
};

} // namespace strmqt::music
```

`src/app/music/models/MusicModelBase.cpp`:

```cpp
#include "app/music/models/MusicModelBase.h"

namespace strmqt::music {

int MusicModelBase::indexOfNavigationIdentity(const QString &identity) const
{
    return m_rowByIdentity.value(identity, -1);
}

QString MusicModelBase::idAt(int row) const
{
    return row >= 0 && row < size() ? idOf(row) : QString();
}

void MusicModelBase::rebuildIndex()
{
    m_rowByIdentity.clear();
    m_rowsById.clear();
    extendIndex(0);
}

void MusicModelBase::extendIndex(int fromRow)
{
    for (int row = fromRow; row < size(); ++row)
        indexRow(row);
}

void MusicModelBase::indexRow(int row)
{
    const QString id = idOf(row);
    m_rowsById[id].append(row);
    const QString entry = playlistItemIdOf(row);
    const QString identity = entry.isEmpty() ? QStringLiteral("i:") + id : QStringLiteral("p:") + entry;
    if (!m_rowByIdentity.contains(identity))
        m_rowByIdentity.insert(identity, row);
}

void MusicModelBase::setTotal(int total)
{
    const int floored = qMax(total, size());
    if (floored == m_total)
        return;
    m_total = floored;
    emit totalRecordCountChanged();
}

} // namespace strmqt::music
```

`src/app/music/models/MusicListModel.h`:

```cpp
#pragma once

#include "app/music/models/MusicModelBase.h"

namespace strmqt::music {

// Storage and paging for a model of one DTO type with an `id` member. Not a
// QObject class of its own (templates cannot carry Q_OBJECT); concrete models
// derive from it and add Q_OBJECT, roles and applyUserData.
template<class T>
class MusicListModel : public MusicModelBase
{
public:
    using MusicModelBase::MusicModelBase;

    void setItems(QList<T> items, int total = -1)
    {
        beginResetModel();
        m_items = std::move(items);
        onItemsReset();
        endResetModel();
        rebuildIndex();
        setTotal(total < 0 ? size() : total);
        emit countChanged();
    }

    void appendItems(const QList<T> &items, int total = -1)
    {
        if (!items.isEmpty()) {
            const int from = size();
            beginInsertRows(QModelIndex(), from, from + static_cast<int>(items.size()) - 1);
            m_items.append(items);
            onItemsAppended(from);
            endInsertRows();
            extendIndex(from);
            emit countChanged();
        }
        setTotal(total < 0 ? totalRecordCount() : total);
    }

    void clear() { setItems({}, 0); }
    const QList<T> &items() const { return m_items; }
    const T &at(int row) const { return m_items.at(row); }

protected:
    int size() const override { return static_cast<int>(m_items.size()); }
    QString idOf(int row) const override { return m_items.at(row).id; }
    virtual void onItemsReset() {}
    virtual void onItemsAppended(int fromRow) { Q_UNUSED(fromRow); }
    void notifyRow(int row, const QList<int> &roles) { emit dataChanged(index(row), index(row), roles); }

    QList<T> m_items;
};

} // namespace strmqt::music
```

- [x] **Step 5: Write the three grid models**

`AlbumGridModel.h`:

```cpp
#pragma once

#include "app/music/models/MusicListModel.h"
#include "server/dto/music/MusicTypes.h"

namespace strmqt::music {

class AlbumGridModel : public MusicListModel<Album>
{
    Q_OBJECT

public:
    enum Role
    {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        NameRole,
        ArtistRole,
        ArtistIdRole,
        SubtitleRole,
        YearRole,
        ReleaseTypeRole,
        ReleaseBadgeRole,
        CoverUrlRole,
        PosterUrlRole,
        FormatBadgeRole,
        TrackCountRole,
        DurationTextRole,
        FavouriteRole,
        FavoriteRole,
        PlayCountRole,
        TypeRole,
    };

    explicit AlbumGridModel(QObject *parent = nullptr) : MusicListModel<Album>(parent) {}

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QVariantMap get(int row) const override;
    void applyUserData(const QString &itemId, const UserDataPatch &patch) override;

    static QVariant dataFor(const Album &album, int role);
};

} // namespace strmqt::music
```

`AlbumGridModel.cpp`:

```cpp
#include "app/music/models/AlbumGridModel.h"

#include "app/models/MediaItemModel.h"
#include "app/music/MusicFormat.h"
#include "server/dto/music/MusicMediaItem.h"

namespace strmqt::music {

QHash<int, QByteArray> AlbumGridModel::roleNames() const
{
    static const QHash<int, QByteArray> names{
        {IdRole, "itemId"}, {TitleRole, "title"}, {NameRole, "name"}, {ArtistRole, "artist"},
        {ArtistIdRole, "artistId"}, {SubtitleRole, "subtitle"}, {YearRole, "year"},
        {ReleaseTypeRole, "releaseType"}, {ReleaseBadgeRole, "releaseBadge"}, {CoverUrlRole, "coverUrl"},
        {PosterUrlRole, "posterUrl"}, {FormatBadgeRole, "formatBadge"}, {TrackCountRole, "trackCount"},
        {DurationTextRole, "durationText"}, {FavouriteRole, "favourite"}, {FavoriteRole, "favorite"},
        {PlayCountRole, "playCount"}, {TypeRole, "type"}};
    return names;
}

QVariant AlbumGridModel::dataFor(const Album &album, int role)
{
    switch (role) {
    case IdRole:
        return album.id;
    case TitleRole:
    case NameRole:
        return album.title;
    case ArtistRole:
        return joinNames(album.albumArtists);
    case ArtistIdRole:
        return album.albumArtists.isEmpty() ? QString() : album.albumArtists.first().id;
    case SubtitleRole: {
        const QString artist = joinNames(album.albumArtists);
        if (album.year <= 0)
            return artist;
        return artist.isEmpty() ? QString::number(album.year)
                                : QStringLiteral("%1 · %2").arg(artist).arg(album.year);
    }
    case YearRole:
        return album.year;
    case ReleaseTypeRole:
        return releaseTypeName(album.releaseType);
    case ReleaseBadgeRole:
        return album.releaseType == ReleaseType::Album ? QString() : releaseTypeName(album.releaseType);
    case CoverUrlRole:
    case PosterUrlRole:
        return coverUrl(album.coverRef);
    case FormatBadgeRole:
        return album.formatSummary;
    case TrackCountRole:
        return album.trackCount;
    case DurationTextRole:
        return formatRuntime(album.runtimeMs);
    case FavouriteRole:
    case FavoriteRole:
        return album.favourite;
    case PlayCountRole:
        return album.playCount;
    case TypeRole:
        return QStringLiteral("MusicAlbum");
    default:
        return {};
    }
}

QVariant AlbumGridModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= size())
        return {};
    return dataFor(m_items.at(index.row()), role);
}

QVariantMap AlbumGridModel::get(int row) const
{
    if (row < 0 || row >= size())
        return {};
    const Album &album = m_items.at(row);
    QVariantMap map = MediaItemModel::mapForItem(toMediaItem(album));
    const auto names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        map.insert(QString::fromLatin1(it.value()), dataFor(album, it.key()));
    return map;
}

void AlbumGridModel::applyUserData(const QString &itemId, const UserDataPatch &patch)
{
    for (int row : rowsFor(itemId)) {
        Album &album = m_items[row];
        if (patch.favourite)
            album.favourite = *patch.favourite;
        if (patch.playCount)
            album.playCount = *patch.playCount;
        notifyRow(row, {FavouriteRole, FavoriteRole, PlayCountRole});
    }
}

} // namespace strmqt::music
```

`ArtistGridModel.cpp`:

```cpp
QHash<int, QByteArray> ArtistGridModel::roleNames() const
{
    static const QHash<int, QByteArray> names{
        {IdRole, "itemId"}, {NameRole, "name"}, {TitleRole, "title"}, {SubtitleRole, "subtitle"},
        {RecordCountRole, "recordCount"}, {CoverUrlRole, "coverUrl"}, {PosterUrlRole, "posterUrl"},
        {BackdropUrlRole, "backdropUrl"}, {FavouriteRole, "favourite"}, {FavoriteRole, "favorite"},
        {TypeRole, "type"}};
    return names;
}

QVariant ArtistGridModel::dataFor(const Artist &artist, int role)
{
    switch (role) {
    case IdRole:
        return artist.id;
    case NameRole:
    case TitleRole:
        return artist.name;
    case SubtitleRole:
        return formatRecordCount(artist.albumCount);
    case RecordCountRole:
        return artist.albumCount;
    case CoverUrlRole:
    case PosterUrlRole:
        return coverUrl(artist.coverRef);
    case BackdropUrlRole:
        return coverUrl(artist.backdropRef);
    case FavouriteRole:
    case FavoriteRole:
        return artist.favourite;
    case TypeRole:
        return QStringLiteral("MusicArtist");
    default:
        return {};
    }
}

void ArtistGridModel::applyUserData(const QString &itemId, const UserDataPatch &patch)
{
    for (int row : rowsFor(itemId)) {
        if (patch.favourite)
            m_items[row].favourite = *patch.favourite;
        notifyRow(row, {FavouriteRole, FavoriteRole});
    }
}
```

`ArtistGridModel.h` in full:

```cpp
#pragma once

#include "app/music/models/MusicListModel.h"
#include "server/dto/music/MusicTypes.h"

namespace strmqt::music {

class ArtistGridModel : public MusicListModel<Artist>
{
    Q_OBJECT

public:
    enum Role
    {
        IdRole = Qt::UserRole + 1,
        NameRole,
        TitleRole,
        SubtitleRole,
        RecordCountRole,
        CoverUrlRole,
        PosterUrlRole,
        BackdropUrlRole,
        FavouriteRole,
        FavoriteRole,
        TypeRole,
    };

    explicit ArtistGridModel(QObject *parent = nullptr) : MusicListModel<Artist>(parent) {}

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QVariantMap get(int row) const override;
    void applyUserData(const QString &itemId, const UserDataPatch &patch) override;

    static QVariant dataFor(const Artist &artist, int role);
};

} // namespace strmqt::music
```

And the remaining two functions in `ArtistGridModel.cpp`, which uses the same includes as `AlbumGridModel.cpp`:

```cpp
QVariant ArtistGridModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= size())
        return {};
    return dataFor(m_items.at(index.row()), role);
}

QVariantMap ArtistGridModel::get(int row) const
{
    if (row < 0 || row >= size())
        return {};
    const Artist &artist = m_items.at(row);
    QVariantMap map = MediaItemModel::mapForItem(toMediaItem(artist));
    const auto names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        map.insert(QString::fromLatin1(it.value()), dataFor(artist, it.key()));
    return map;
}
```

`PlaylistGridModel.h` in full:

```cpp
#pragma once

#include "app/music/models/MusicListModel.h"
#include "server/dto/music/MusicTypes.h"

namespace strmqt::music {

class PlaylistGridModel : public MusicListModel<Playlist>
{
    Q_OBJECT

public:
    enum Role
    {
        IdRole = Qt::UserRole + 1,
        NameRole,
        TitleRole,
        SubtitleRole,
        TrackCountRole,
        CoverUrlRole,
        PosterUrlRole,
        TypeRole,
    };

    explicit PlaylistGridModel(QObject *parent = nullptr) : MusicListModel<Playlist>(parent) {}

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QVariantMap get(int row) const override;
    void applyUserData(const QString &itemId, const UserDataPatch &patch) override;

    static QVariant dataFor(const Playlist &playlist, int role);
};

} // namespace strmqt::music
```

`PlaylistGridModel.cpp` uses the same includes as `AlbumGridModel.cpp`:

```cpp
QVariant PlaylistGridModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= size())
        return {};
    return dataFor(m_items.at(index.row()), role);
}

QHash<int, QByteArray> PlaylistGridModel::roleNames() const
{
    static const QHash<int, QByteArray> names{
        {IdRole, "itemId"}, {NameRole, "name"}, {TitleRole, "title"}, {SubtitleRole, "subtitle"},
        {TrackCountRole, "trackCount"}, {CoverUrlRole, "coverUrl"}, {PosterUrlRole, "posterUrl"},
        {TypeRole, "type"}};
    return names;
}

QVariant PlaylistGridModel::dataFor(const Playlist &playlist, int role)
{
    switch (role) {
    case IdRole:
        return playlist.id;
    case NameRole:
    case TitleRole:
        return playlist.name;
    case SubtitleRole: {
        const QString runtime = formatRuntime(playlist.runtimeMs);
        const QString tracks = formatTrackCount(playlist.trackCount);
        return runtime.isEmpty() ? tracks : QStringLiteral("%1 · %2").arg(tracks, runtime);
    }
    case TrackCountRole:
        return playlist.trackCount;
    case CoverUrlRole:
    case PosterUrlRole:
        return coverUrl(playlist.coverRef);
    case TypeRole:
        return QStringLiteral("Playlist");
    default:
        return {};
    }
}

QVariantMap PlaylistGridModel::get(int row) const
{
    if (row < 0 || row >= size())
        return {};
    const Playlist &playlist = m_items.at(row);
    MediaItem item;
    item.id = playlist.id;
    item.name = playlist.name;
    item.type = QStringLiteral("Playlist");
    item.childCount = playlist.trackCount;
    item.runtimeTicks = playlist.runtimeMs * kTicksPerMs;
    if (playlist.coverRef.itemId == playlist.id)
        item.primaryImageTag = playlist.coverRef.tag;
    QVariantMap map = MediaItemModel::mapForItem(item);
    const auto names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        map.insert(QString::fromLatin1(it.value()), dataFor(playlist, it.key()));
    return map;
}

void PlaylistGridModel::applyUserData(const QString &, const UserDataPatch &) {}
```

Add to `strmqt_app`:

```cmake
    app/music/UserDataPatch.h
    app/music/models/MusicModelBase.h app/music/models/MusicModelBase.cpp
    app/music/models/MusicListModel.h
    app/music/models/AlbumGridModel.h app/music/models/AlbumGridModel.cpp
    app/music/models/ArtistGridModel.h app/music/models/ArtistGridModel.cpp
    app/music/models/PlaylistGridModel.h app/music/models/PlaylistGridModel.cpp
```

- [x] **Step 6: Run the tests to verify they pass**

Run: `cmake --build --preset dev --target tst_music_models tst_models && ctest --preset dev -R 'tst_music_models|tst_models' --output-on-failure`
Expected: PASS (the existing `tst_models` covers the `get()` refactor).

- [x] **Step 7: Commit**

```bash
git add src/app/music/UserDataPatch.h src/app/music/models src/app/models/MediaItemModel.h src/app/models/MediaItemModel.cpp src/CMakeLists.txt tests/unit/tst_music_models.cpp tests/CMakeLists.txt
git commit -m "feat(music): add album, artist and playlist grid models

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 14: Music models, part 2: tracks, genre bins, stations

**Files:**
- Create: `src/app/music/models/TrackListModel.h/.cpp`, `GenreBinModel.h/.cpp`, `StationModel.h/.cpp`
- Modify: `src/CMakeLists.txt`
- Test: `tests/unit/tst_music_models.cpp`

**Interfaces:**
- Consumes: Task 13 (`MusicListModel`, `MusicModelBase`, `UserDataPatch`, `mapForItem`), `MusicFormat`, `toMediaItem`.
- Produces:
  - `class TrackListModel : MusicListModel<Track>`:
    - Role set: every `MediaItemModel::Role` (same values and names, served through `MediaItemModel::dataForItem` on a parallel `QList<MediaItem>`), plus these music roles from `Qt::UserRole + 200`: `displayTitle, featuredText, artistText, albumTitle, discNumber, trackNumber, durationText, formatBadge, isHiRes, favourite, coverUrl, differsFromAlbumArtist`
    - `Q_INVOKABLE QVariantList mediaMaps() const`, which returns every row's `get()` in order, for `ItemActions::playAllFromIfCurrent`
    - Playlist entries use `"p:" + playlistItemId` as their navigation identity.
  - `class GenreBinModel : MusicListModel<GenreBin>`, roles `itemId, name, title, recordCount, subtitle, covers (QStringList of URLs), coverUrl, type ("MusicGenre")`
  - `class StationModel : MusicModelBase`:
    - Roles `itemId (kind key), kind, label, name, seedId, covers (QStringList)`
    - `void setStations(QList<Station>)`, `std::optional<Station> stationFor(const QString &key) const`
    - `static QString kindKey(StationKind)`, `static std::optional<StationKind> kindFromKey(const QString &)`
    - Kind keys: `heavyRotation, favourites, deepCuts, moreLike, shuffleAll`

- [x] **Step 1: Write the failing tests**

Add these includes to `tst_music_models.cpp`:

```cpp
#include "app/music/models/GenreBinModel.h"
#include "app/music/models/StationModel.h"
#include "app/music/models/TrackListModel.h"
```

Add these slots:

```cpp
    void trackRolesServeMediaAndMusicNames();
    void trackUserDataPatchesBothViews();
    void playlistEntriesUsePlaylistIdentity();
    void genreBinsAndStations();
```

```cpp
namespace {
Track track(const QString &id, int number, const QString &playlistItemId = QString())
{
    Track t;
    t.id = id;
    t.title = QStringLiteral("Get Lucky (feat. Pharrell Williams)");
    t.displayTitle = QStringLiteral("Get Lucky");
    t.featured = {{QString(), QStringLiteral("Pharrell Williams")}, {QStringLiteral("nr"), QStringLiteral("Nile Rodgers")}};
    t.artists = {{QStringLiteral("dp"), QStringLiteral("Daft Punk")}};
    t.albumArtists = t.artists;
    t.albumId = QStringLiteral("ram");
    t.albumTitle = QStringLiteral("Random Access Memories");
    t.discNumber = 1;
    t.trackNumber = number;
    t.runtimeMs = 369'000;
    t.format.badge = QStringLiteral("FLAC 24/88.2");
    t.format.isHiRes = true;
    t.coverRef = {QStringLiteral("ram"), QStringLiteral("Primary"), QStringLiteral("c")};
    t.playlistItemId = playlistItemId;
    return t;
}
} // namespace

void MusicModelsTest::trackRolesServeMediaAndMusicNames()
{
    TrackListModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    model.setItems({track("t1", 8)});
    QCOMPARE(role(model, 0, "name").toString(), QStringLiteral("Get Lucky (feat. Pharrell Williams)"));
    QCOMPARE(role(model, 0, "displayTitle").toString(), QStringLiteral("Get Lucky"));
    QCOMPARE(role(model, 0, "featuredText").toString(), QStringLiteral("feat. Pharrell Williams & Nile Rodgers"));
    QCOMPARE(role(model, 0, "artistText").toString(), QStringLiteral("Daft Punk"));
    QCOMPARE(role(model, 0, "runtimeMs").toLongLong(), Q_INT64_C(369000)); // media role
    QCOMPARE(role(model, 0, "indexNumber").toInt(), 8);                      // media role
    QCOMPARE(role(model, 0, "trackNumber").toInt(), 8);
    QCOMPARE(role(model, 0, "durationText").toString(), QStringLiteral("6:09"));
    QCOMPARE(role(model, 0, "formatBadge").toString(), QStringLiteral("FLAC 24/88.2"));
    QVERIFY(role(model, 0, "isHiRes").toBool());
    QCOMPARE(role(model, 0, "coverUrl").toString(), QStringLiteral("image://emby/t/ram/Primary/c"));
    QCOMPARE(role(model, 0, "posterUrl").toString(), role(model, 0, "coverUrl").toString());

    const QVariantList maps = model.mediaMaps();
    QCOMPARE(maps.size(), 1);
    QCOMPARE(maps.first().toMap().value("type").toString(), QStringLiteral("Audio"));
    QCOMPARE(maps.first().toMap().value("albumId").toString(), QStringLiteral("ram"));
}

void MusicModelsTest::trackUserDataPatchesBothViews()
{
    TrackListModel model;
    model.setItems({track("t1", 1), track("t2", 2)});
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    UserDataPatch patch;
    patch.favourite = true;
    patch.played = true;
    patch.playCount = 5;
    model.applyUserData(QStringLiteral("t2"), patch);
    QCOMPARE(changed.count(), 1);
    QVERIFY(role(model, 1, "favorite").toBool());   // media role
    QVERIFY(role(model, 1, "favourite").toBool());  // music role
    QVERIFY(role(model, 1, "played").toBool());
    QCOMPARE(role(model, 1, "playCount").toInt(), 5);
    QVERIFY(model.get(1).value("favorite").toBool());
    QVERIFY(model.at(1).favourite);
}

void MusicModelsTest::playlistEntriesUsePlaylistIdentity()
{
    TrackListModel model;
    model.setItems({track("same", 1, "e1"), track("same", 2, "e2")});
    QCOMPARE(model.indexOfNavigationIdentity(QStringLiteral("p:e2")), 1);
    QCOMPARE(model.indexOfNavigationIdentity(QStringLiteral("i:same")), -1);
    QCOMPARE(role(model, 1, "playlistItemId").toString(), QStringLiteral("e2"));
}

void MusicModelsTest::genreBinsAndStations()
{
    GenreBinModel genres;
    GenreBin bin;
    bin.id = QStringLiteral("g1");
    bin.name = QStringLiteral("Jazz");
    bin.recordCount = 42;
    bin.covers = {{QStringLiteral("a"), QStringLiteral("Primary"), QStringLiteral("1")},
                  {QStringLiteral("b"), QStringLiteral("Primary"), QStringLiteral("2")}};
    genres.setItems({bin}, 120);
    QCOMPARE(role(genres, 0, "subtitle").toString(), QStringLiteral("42 records"));
    QCOMPARE(role(genres, 0, "covers").toStringList().size(), 2);
    QCOMPARE(role(genres, 0, "coverUrl").toString(), QStringLiteral("image://emby/t/a/Primary/1"));
    QCOMPARE(genres.totalRecordCount(), 120);
    QCOMPARE(genres.get(0).value("type").toString(), QStringLiteral("MusicGenre"));

    StationModel stations;
    Station more{StationKind::MoreLike, QStringLiteral("More like Björk"), QStringLiteral("ar1"), {}};
    stations.setStations({Station{StationKind::HeavyRotation, QStringLiteral("Heavy rotation"), {}, {}}, more});
    QCOMPARE(stations.rowCount(), 2);
    QCOMPARE(role(stations, 1, "itemId").toString(), QStringLiteral("moreLike"));
    QCOMPARE(role(stations, 1, "label").toString(), QStringLiteral("More like Björk"));
    QCOMPARE(stations.indexOfNavigationIdentity(QStringLiteral("i:moreLike")), 1);
    QCOMPARE(stations.stationFor(QStringLiteral("moreLike"))->seedId, QStringLiteral("ar1"));
    QVERIFY(!stations.stationFor(QStringLiteral("bogus")).has_value());
    QVERIFY(StationModel::kindFromKey(QStringLiteral("deepCuts")) == StationKind::DeepCuts);
    QCOMPARE(stations.get(0).value("label").toString(), QStringLiteral("Heavy rotation"));
}
```

- [x] **Step 2: Run the tests to verify they fail**

Run: `cmake --build --preset dev --target tst_music_models`
Expected: a compile error (headers not found).

- [x] **Step 3: Write `TrackListModel`**

`TrackListModel.h`:

```cpp
#pragma once

#include "app/music/models/MusicListModel.h"
#include "server/dto/MediaItem.h"
#include "server/dto/music/MusicTypes.h"

namespace strmqt::music {

// Tracks for TrackTable-based surfaces. Serves MediaItemModel's roles unchanged
// (TrackTable, ItemMenu and the queue read them) plus the music display roles.
class TrackListModel : public MusicListModel<Track>
{
    Q_OBJECT

public:
    enum MusicRole
    {
        DisplayTitleRole = Qt::UserRole + 200,
        FeaturedTextRole,
        ArtistTextRole,
        AlbumTitleRole,
        DiscNumberRole,
        TrackNumberRole,
        DurationTextRole,
        FormatBadgeRole,
        IsHiResRole,
        FavouriteRole,
        CoverUrlRole,
        DiffersFromAlbumArtistRole,
    };

    explicit TrackListModel(QObject *parent = nullptr) : MusicListModel<Track>(parent) {}

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QVariantMap get(int row) const override;
    void applyUserData(const QString &itemId, const UserDataPatch &patch) override;
    Q_INVOKABLE QVariantList mediaMaps() const;

protected:
    QString playlistItemIdOf(int row) const override { return m_items.at(row).playlistItemId; }
    void onItemsReset() override;
    void onItemsAppended(int fromRow) override;

private:
    static const QHash<int, QByteArray> &musicRoleNames();
    QVariant musicData(const Track &track, int role) const;

    QList<MediaItem> m_media;
};

} // namespace strmqt::music
```

`TrackListModel.cpp`:

```cpp
#include "app/music/models/TrackListModel.h"

#include "app/models/MediaItemModel.h"
#include "app/music/MusicFormat.h"
#include "server/dto/music/MusicMediaItem.h"

namespace strmqt::music {

const QHash<int, QByteArray> &TrackListModel::musicRoleNames()
{
    static const QHash<int, QByteArray> names{
        {DisplayTitleRole, "displayTitle"}, {FeaturedTextRole, "featuredText"},
        {ArtistTextRole, "artistText"}, {AlbumTitleRole, "albumTitle"}, {DiscNumberRole, "discNumber"},
        {TrackNumberRole, "trackNumber"}, {DurationTextRole, "durationText"},
        {FormatBadgeRole, "formatBadge"}, {IsHiResRole, "isHiRes"}, {FavouriteRole, "favourite"},
        {CoverUrlRole, "coverUrl"}, {DiffersFromAlbumArtistRole, "differsFromAlbumArtist"}};
    return names;
}

QHash<int, QByteArray> TrackListModel::roleNames() const
{
    QHash<int, QByteArray> names = MediaItemModel::mediaRoleNames();
    names.insert(musicRoleNames());
    return names;
}

void TrackListModel::onItemsReset()
{
    m_media.clear();
    m_media.reserve(m_items.size());
    for (const Track &track : std::as_const(m_items))
        m_media.append(toMediaItem(track));
}

void TrackListModel::onItemsAppended(int fromRow)
{
    for (int row = fromRow; row < m_items.size(); ++row)
        m_media.append(toMediaItem(m_items.at(row)));
}

QVariant TrackListModel::musicData(const Track &track, int role) const
{
    switch (role) {
    case DisplayTitleRole:
        return track.displayTitle.isEmpty() ? track.title : track.displayTitle;
    case FeaturedTextRole:
        return track.featured.isEmpty() ? QString() : QStringLiteral("feat. ") + joinNames(track.featured);
    case ArtistTextRole:
        return joinNames(track.artists);
    case AlbumTitleRole:
        return track.albumTitle;
    case DiscNumberRole:
        return track.discNumber;
    case TrackNumberRole:
        return track.trackNumber;
    case DurationTextRole:
        return formatDuration(track.runtimeMs);
    case FormatBadgeRole:
        return track.format.badge;
    case IsHiResRole:
        return track.format.isHiRes;
    case FavouriteRole:
        return track.favourite;
    case CoverUrlRole:
        return coverUrl(track.coverRef);
    case DiffersFromAlbumArtistRole:
        return track.differsFromAlbumArtist;
    default:
        return {};
    }
}

QVariant TrackListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= size())
        return {};
    if (role >= DisplayTitleRole)
        return musicData(m_items.at(index.row()), role);
    return MediaItemModel::dataForItem(m_media.at(index.row()), role);
}

QVariantMap TrackListModel::get(int row) const
{
    if (row < 0 || row >= size())
        return {};
    QVariantMap map = MediaItemModel::mapForItem(m_media.at(row));
    const auto &names = musicRoleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        map.insert(QString::fromLatin1(it.value()), musicData(m_items.at(row), it.key()));
    return map;
}

QVariantList TrackListModel::mediaMaps() const
{
    QVariantList list;
    list.reserve(size());
    for (int row = 0; row < size(); ++row)
        list.append(get(row));
    return list;
}

void TrackListModel::applyUserData(const QString &itemId, const UserDataPatch &patch)
{
    for (int row : rowsFor(itemId)) {
        Track &track = m_items[row];
        MediaItem &media = m_media[row];
        if (patch.favourite)
            track.favourite = media.favorite = *patch.favourite;
        if (patch.played)
            track.played = media.played = *patch.played;
        if (patch.playCount)
            track.playCount = media.playCount = *patch.playCount;
        if (patch.positionMs) {
            track.positionMs = *patch.positionMs;
            media.playbackPositionTicks = *patch.positionMs * kTicksPerMs;
        }
        notifyRow(row, {MediaItemModel::FavoriteRole, MediaItemModel::PlayedRole,
                        MediaItemModel::PlayCountRole, MediaItemModel::PositionMsRole,
                        MediaItemModel::ProgressRole, MediaItemModel::ResumableRole, FavouriteRole});
    }
}

} // namespace strmqt::music
```

`MediaItemModel`'s media roles occupy `Qt::UserRole + 1` to about `+40`, so `+200` cannot collide. Add a `Q_ASSERT(MediaItemModel::SubtitleRole < DisplayTitleRole)` at the top of `roleNames()`.

- [x] **Step 4: Write `GenreBinModel`**

`GenreBinModel.h`:

```cpp
#pragma once

#include "app/music/models/MusicListModel.h"
#include "server/dto/music/MusicTypes.h"

namespace strmqt::music {

class GenreBinModel : public MusicListModel<GenreBin>
{
    Q_OBJECT

public:
    enum Role
    {
        IdRole = Qt::UserRole + 1,
        NameRole,
        TitleRole,
        RecordCountRole,
        SubtitleRole,
        CoversRole,
        CoverUrlRole,
        TypeRole,
    };

    explicit GenreBinModel(QObject *parent = nullptr) : MusicListModel<GenreBin>(parent) {}

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QVariantMap get(int row) const override;
    void applyUserData(const QString &, const UserDataPatch &) override {}

    static QVariant dataFor(const GenreBin &bin, int role);
};

} // namespace strmqt::music
```

`GenreBinModel.cpp`:

```cpp
#include "app/music/models/GenreBinModel.h"

#include "app/music/MusicFormat.h"

namespace strmqt::music {

QHash<int, QByteArray> GenreBinModel::roleNames() const
{
    static const QHash<int, QByteArray> names{
        {IdRole, "itemId"}, {NameRole, "name"}, {TitleRole, "title"}, {RecordCountRole, "recordCount"},
        {SubtitleRole, "subtitle"}, {CoversRole, "covers"}, {CoverUrlRole, "coverUrl"}, {TypeRole, "type"}};
    return names;
}

QVariant GenreBinModel::dataFor(const GenreBin &bin, int role)
{
    switch (role) {
    case IdRole:
        return bin.id;
    case NameRole:
    case TitleRole:
        return bin.name;
    case RecordCountRole:
        return bin.recordCount;
    case SubtitleRole:
        return formatRecordCount(bin.recordCount);
    case CoversRole: {
        QStringList urls;
        for (const ImageRef &ref : bin.covers)
            urls.append(coverUrl(ref));
        return urls;
    }
    case CoverUrlRole:
        return bin.covers.isEmpty() ? QString() : coverUrl(bin.covers.first());
    case TypeRole:
        return QStringLiteral("MusicGenre");
    default:
        return {};
    }
}

QVariant GenreBinModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= size())
        return {};
    return dataFor(m_items.at(index.row()), role);
}

QVariantMap GenreBinModel::get(int row) const
{
    if (row < 0 || row >= size())
        return {};
    QVariantMap map;
    const auto names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        map.insert(QString::fromLatin1(it.value()), dataFor(m_items.at(row), it.key()));
    return map;
}

} // namespace strmqt::music
```

- [x] **Step 5: Write `StationModel`**

`StationModel.h`:

```cpp
#pragma once

#include <optional>

#include "app/music/models/MusicModelBase.h"
#include "server/dto/music/MusicTypes.h"

namespace strmqt::music {

class StationModel : public MusicModelBase
{
    Q_OBJECT

public:
    enum Role
    {
        IdRole = Qt::UserRole + 1,
        KindRole,
        LabelRole,
        NameRole,
        SeedIdRole,
        CoversRole,
    };

    explicit StationModel(QObject *parent = nullptr) : MusicModelBase(parent) {}

    void setStations(QList<Station> stations);
    const QList<Station> &stations() const { return m_stations; }
    std::optional<Station> stationFor(const QString &key) const;

    static QString kindKey(StationKind kind);
    static std::optional<StationKind> kindFromKey(const QString &key);

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QVariantMap get(int row) const override;
    void applyUserData(const QString &, const UserDataPatch &) override {}

protected:
    int size() const override { return static_cast<int>(m_stations.size()); }
    QString idOf(int row) const override { return kindKey(m_stations.at(row).kind); }

private:
    QList<Station> m_stations;
};

} // namespace strmqt::music
```

`StationModel.cpp`:

```cpp
#include "app/music/models/StationModel.h"

#include "app/music/MusicFormat.h"

namespace strmqt::music {

QString StationModel::kindKey(StationKind kind)
{
    switch (kind) {
    case StationKind::HeavyRotation: return QStringLiteral("heavyRotation");
    case StationKind::Favourites: return QStringLiteral("favourites");
    case StationKind::DeepCuts: return QStringLiteral("deepCuts");
    case StationKind::MoreLike: return QStringLiteral("moreLike");
    case StationKind::ShuffleAll: break;
    }
    return QStringLiteral("shuffleAll");
}

std::optional<StationKind> StationModel::kindFromKey(const QString &key)
{
    for (StationKind kind : {StationKind::HeavyRotation, StationKind::Favourites, StationKind::DeepCuts,
                             StationKind::MoreLike, StationKind::ShuffleAll}) {
        if (kindKey(kind) == key)
            return kind;
    }
    return std::nullopt;
}

void StationModel::setStations(QList<Station> stations)
{
    beginResetModel();
    m_stations = std::move(stations);
    endResetModel();
    rebuildIndex();
    setTotal(size());
    emit countChanged();
}

std::optional<Station> StationModel::stationFor(const QString &key) const
{
    for (const Station &station : m_stations) {
        if (kindKey(station.kind) == key)
            return station;
    }
    return std::nullopt;
}

QHash<int, QByteArray> StationModel::roleNames() const
{
    static const QHash<int, QByteArray> names{{IdRole, "itemId"}, {KindRole, "kind"}, {LabelRole, "label"},
                                              {NameRole, "name"}, {SeedIdRole, "seedId"}, {CoversRole, "covers"}};
    return names;
}

QVariant StationModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= size())
        return {};
    const Station &station = m_stations.at(index.row());
    switch (role) {
    case IdRole:
    case KindRole:
        return kindKey(station.kind);
    case LabelRole:
    case NameRole:
        return station.label;
    case SeedIdRole:
        return station.seedId;
    case CoversRole: {
        QStringList urls;
        for (const ImageRef &ref : station.covers)
            urls.append(coverUrl(ref));
        return urls;
    }
    default:
        return {};
    }
}

QVariantMap StationModel::get(int row) const
{
    if (row < 0 || row >= size())
        return {};
    QVariantMap map;
    const auto names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        map.insert(QString::fromLatin1(it.value()), data(index(row), it.key()));
    return map;
}

} // namespace strmqt::music
```

Add to `strmqt_app`:

```cmake
    app/music/models/TrackListModel.h app/music/models/TrackListModel.cpp
    app/music/models/GenreBinModel.h app/music/models/GenreBinModel.cpp
    app/music/models/StationModel.h app/music/models/StationModel.cpp
```

- [x] **Step 6: Run the tests to verify they pass**

Run: `cmake --build --preset dev --target tst_music_models && ctest --preset dev -R tst_music_models --output-on-failure`
Expected: PASS (9 tests).

- [x] **Step 7: Commit**

```bash
git add src/app/music/models src/CMakeLists.txt tests/unit/tst_music_models.cpp
git commit -m "feat(music): add track, genre bin and station models

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 15: `MusicUserDataRelay`

**Files:**
- Create: `src/app/music/MusicUserDataRelay.h`, `src/app/music/MusicUserDataRelay.cpp`
- Modify: `src/CMakeLists.txt` (`strmqt_app`)
- Test: `tests/unit/tst_music_user_data_relay.cpp`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `MusicRepository::noteUserDataChanged` (Task 12); `MusicModelBase::applyUserData`, `UserDataPatch` (Task 13); the `ItemActions` signals `favoriteChanged(QString,bool)` and `playedChanged(QString,bool)`; the `LiveUpdateService` signal `userDataPatched(QVariantList)`, whose entries carry `itemId, played, favorite, positionTicks, playCount`.
- Produces (namespace `strmqt::music`), `class MusicUserDataRelay : QObject`:
  - `MusicUserDataRelay(MusicRepository *repository, QObject *parent = nullptr)`
  - `void addModel(MusicModelBase *model)`: held by `QPointer`, so a destroyed page model drops out and needs no unregistering
  - `void bind(ItemActions *actions, LiveUpdateService *live)`: either may be null
  - `void apply(const QString &itemId, const UserDataPatch &patch)`
  - public slots `onFavouriteChanged(QString,bool)`, `onPlayedChanged(QString,bool)`, `onUserDataPatched(QVariantList)`

This is the spec's user-data sink (§3.4). One place turns every user-data change into model patches plus cache invalidation, so no page refetches after a heart tap.

- [ ] **Step 1: Write the failing test**

`tests/unit/tst_music_user_data_relay.cpp`:

```cpp
#include <QtTest>

#include "app/music/MusicRepository.h"
#include "app/music/MusicUserDataRelay.h"
#include "app/music/models/AlbumGridModel.h"
#include "app/music/models/TrackListModel.h"
#include "server/emby/EmbyClient.h"

using namespace strmqt;
using namespace strmqt::music;

namespace {
QVariant role(const QAbstractItemModel &model, int row, const QByteArray &name)
{
    return model.data(model.index(row, 0), model.roleNames().key(name));
}

Track track(const QString &id)
{
    Track t;
    t.id = id;
    t.title = id;
    return t;
}
} // namespace

class MusicUserDataRelayTest : public QObject
{
    Q_OBJECT

private slots:
    void favouritePatchesEveryRegisteredModel();
    void liveEntriesCarryEveryField();
    void destroyedModelsDropOut();
};

void MusicUserDataRelayTest::favouritePatchesEveryRegisteredModel()
{
    emby::EmbyClient client;
    MusicRepository repository(&client);
    MusicUserDataRelay relay(&repository);
    TrackListModel tracks;
    AlbumGridModel albums;
    tracks.setItems({track("t1"), track("t2")});
    Album album;
    album.id = QStringLiteral("t1"); // same id in both models: both must patch
    albums.setItems({album});
    relay.addModel(&tracks);
    relay.addModel(&albums);

    relay.onFavouriteChanged(QStringLiteral("t1"), true);
    QVERIFY(role(tracks, 0, "favourite").toBool());
    QVERIFY(!role(tracks, 1, "favourite").toBool());
    QVERIFY(role(albums, 0, "favourite").toBool());

    relay.onPlayedChanged(QStringLiteral("t2"), true);
    QVERIFY(role(tracks, 1, "played").toBool());
}

void MusicUserDataRelayTest::liveEntriesCarryEveryField()
{
    emby::EmbyClient client;
    MusicRepository repository(&client);
    MusicUserDataRelay relay(&repository);
    TrackListModel tracks;
    tracks.setItems({track("t1")});
    relay.addModel(&tracks);

    relay.onUserDataPatched({QVariantMap{{"itemId", "t1"}, {"played", true}, {"favorite", true},
                                         {"positionTicks", Q_INT64_C(120000000)}, {"playCount", 3}},
                             QVariantMap{{"played", true}}}); // no id: ignored
    QVERIFY(role(tracks, 0, "favourite").toBool());
    QVERIFY(role(tracks, 0, "played").toBool());
    QCOMPARE(role(tracks, 0, "playCount").toInt(), 3);
    QCOMPARE(role(tracks, 0, "positionMs").toLongLong(), Q_INT64_C(12000));
}

void MusicUserDataRelayTest::destroyedModelsDropOut()
{
    emby::EmbyClient client;
    MusicRepository repository(&client);
    MusicUserDataRelay relay(&repository);
    auto *tracks = new TrackListModel;
    tracks->setItems({track("t1")});
    relay.addModel(tracks);
    delete tracks;
    relay.onFavouriteChanged(QStringLiteral("t1"), true); // must not touch freed memory
    QVERIFY(true);
}

QTEST_GUILESS_MAIN(MusicUserDataRelayTest)
#include "tst_music_user_data_relay.moc"
```

Add to `tests/CMakeLists.txt`:

```cmake
strmqt_add_test(tst_music_user_data_relay unit/tst_music_user_data_relay.cpp)
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build --preset dev --target tst_music_user_data_relay`
Expected: a compile error (`MusicUserDataRelay.h` not found).

- [ ] **Step 3: Write the relay**

`src/app/music/MusicUserDataRelay.h`:

```cpp
#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QVariantList>

#include "app/music/UserDataPatch.h"

namespace strmqt {
class ItemActions;
class LiveUpdateService;
}

namespace strmqt::music {

class MusicModelBase;
class MusicRepository;

// The single user-data sink for music (Crate spec §3.4): a heart tap, a played
// toggle or a server push patches every live music model in place and drops
// the repository caches that hold the item. Pages never refetch for this.
class MusicUserDataRelay : public QObject
{
    Q_OBJECT

public:
    explicit MusicUserDataRelay(MusicRepository *repository, QObject *parent = nullptr);

    void addModel(MusicModelBase *model);
    void bind(ItemActions *actions, LiveUpdateService *live);
    void apply(const QString &itemId, const UserDataPatch &patch);

public slots:
    void onFavouriteChanged(const QString &itemId, bool favourite);
    void onPlayedChanged(const QString &itemId, bool played);
    void onUserDataPatched(const QVariantList &entries);

private:
    MusicRepository *m_repository;
    QList<QPointer<MusicModelBase>> m_models;
};

} // namespace strmqt::music
```

`src/app/music/MusicUserDataRelay.cpp`:

```cpp
#include "app/music/MusicUserDataRelay.h"

#include "app/ItemActions.h"
#include "app/controllers/LiveUpdateService.h"
#include "app/music/MusicRepository.h"
#include "app/music/models/MusicModelBase.h"
#include "server/dto/MediaItem.h"

namespace strmqt::music {

MusicUserDataRelay::MusicUserDataRelay(MusicRepository *repository, QObject *parent)
    : QObject(parent)
    , m_repository(repository)
{
}

void MusicUserDataRelay::addModel(MusicModelBase *model)
{
    if (model && !m_models.contains(model))
        m_models.append(model);
}

void MusicUserDataRelay::bind(ItemActions *actions, LiveUpdateService *live)
{
    if (actions) {
        connect(actions, &ItemActions::favoriteChanged, this, &MusicUserDataRelay::onFavouriteChanged);
        connect(actions, &ItemActions::playedChanged, this, &MusicUserDataRelay::onPlayedChanged);
    }
    if (live)
        connect(live, &LiveUpdateService::userDataPatched, this, &MusicUserDataRelay::onUserDataPatched);
}

void MusicUserDataRelay::apply(const QString &itemId, const UserDataPatch &patch)
{
    if (itemId.isEmpty())
        return;
    m_models.removeIf([](const QPointer<MusicModelBase> &model) { return model.isNull(); });
    for (const QPointer<MusicModelBase> &model : std::as_const(m_models))
        model->applyUserData(itemId, patch);
    if (m_repository)
        m_repository->noteUserDataChanged(itemId);
}

void MusicUserDataRelay::onFavouriteChanged(const QString &itemId, bool favourite)
{
    UserDataPatch patch;
    patch.favourite = favourite;
    apply(itemId, patch);
}

void MusicUserDataRelay::onPlayedChanged(const QString &itemId, bool played)
{
    UserDataPatch patch;
    patch.played = played;
    apply(itemId, patch);
}

void MusicUserDataRelay::onUserDataPatched(const QVariantList &entries)
{
    for (const QVariant &value : entries) {
        const QVariantMap entry = value.toMap();
        UserDataPatch patch;
        if (entry.contains(QStringLiteral("favorite")))
            patch.favourite = entry.value(QStringLiteral("favorite")).toBool();
        if (entry.contains(QStringLiteral("played")))
            patch.played = entry.value(QStringLiteral("played")).toBool();
        if (entry.contains(QStringLiteral("playCount")))
            patch.playCount = entry.value(QStringLiteral("playCount")).toInt();
        if (entry.contains(QStringLiteral("positionTicks")))
            patch.positionMs = entry.value(QStringLiteral("positionTicks")).toLongLong() / kTicksPerMs;
        apply(entry.value(QStringLiteral("itemId")).toString(), patch);
    }
}

} // namespace strmqt::music
```

Add to `strmqt_app`:

```cmake
    app/music/MusicUserDataRelay.h app/music/MusicUserDataRelay.cpp
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build --preset dev --target tst_music_user_data_relay && ctest --preset dev -R tst_music_user_data_relay --output-on-failure`
Expected: PASS (3 tests).

- [ ] **Step 5: Commit**

```bash
git add src/app/music/MusicUserDataRelay.* src/CMakeLists.txt tests/unit/tst_music_user_data_relay.cpp tests/CMakeLists.txt
git commit -m "feat(music): relay user-data changes to music models and caches

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 16: `PlayQueue::sourceLabel` and `playAllFromIfCurrent` provenance

**Files:**
- Modify: `src/app/PlayQueue.h`, `src/app/PlayQueue.cpp`
- Modify: `src/app/ItemActions.h:98`, `src/app/ItemActions.cpp:519-546`
- Test: `tests/unit/tst_play_queue.cpp`, `tests/integration/tst_item_actions_queue.cpp`

**Interfaces:**
- Consumes: none (independent of the music tasks).
- Produces:
  - `Q_PROPERTY(QString sourceLabel READ sourceLabel NOTIFY sourceLabelChanged)` on `PlayQueue`
  - `QString sourceLabel() const`, `void setSourceLabel(const QString &label)`, and the signal `sourceLabelChanged()`
  - `PlayQueue::Snapshot::sourceLabel`
  - `void ItemActions::playAllFromIfCurrent(const QVariantList &items, int startIndex, quint64 generation, const QString &sourceLabel = QString())`

Rules. These keep the existing header's warning about a remembered label outliving its queue true:
- `setItems` and `clear` reset the label to empty. A play verb sets it *after* replacing the queue.
- `addToQueue(list)` and `playNext(list)` reset it when they add at least one item, because the queue no longer came from one place.
- `removeAt`, `moveItem`, shuffle, repeat and `enrichEntry` keep it.
- `snapshot()`/`restore()` carry it, so a film interrupting a record gives the label back.
- `contextLabel` is unchanged. The Phase 5 player shows `sourceLabel` when it is set, and `contextLabel` otherwise.

- [x] **Step 1: Write the failing tests**

In `tests/unit/tst_play_queue.cpp`, add the slot `void sourceLabelFollowsTheQueueItDescribes();`:

```cpp
void PlayQueueTest::sourceLabelFollowsTheQueueItDescribes()
{
    PlayQueue queue;
    QSignalSpy changed(&queue, &PlayQueue::sourceLabelChanged);
    const auto record = [] {
        return QList<MediaItem>{track(QStringLiteral("a"), QStringLiteral("R"), QStringLiteral("X")),
                                track(QStringLiteral("b"), QStringLiteral("R"), QStringLiteral("X")),
                                track(QStringLiteral("c"), QStringLiteral("R"), QStringLiteral("X"))};
    };

    queue.setItems(record());
    queue.setSourceLabel(QStringLiteral("Station · Heavy rotation"));
    QCOMPARE(queue.sourceLabel(), QStringLiteral("Station · Heavy rotation"));
    QCOMPARE(changed.count(), 1);
    queue.setSourceLabel(QStringLiteral("Station · Heavy rotation"));
    QCOMPARE(changed.count(), 1); // no-op sets are silent

    queue.moveItem(2, 1);
    queue.removeAt(2);
    queue.setShuffled(true);
    QCOMPARE(queue.sourceLabel(), QStringLiteral("Station · Heavy rotation"));

    const PlayQueue::Snapshot snap = queue.snapshot();
    queue.setItems({episode(1)});
    QCOMPARE(queue.sourceLabel(), QString()); // a film/TV queue has no music provenance
    queue.restore(snap);
    QCOMPARE(queue.sourceLabel(), QStringLiteral("Station · Heavy rotation"));

    QCOMPARE(queue.addToQueue(QList<MediaItem>{track(QStringLiteral("z"), QStringLiteral("Q"), QStringLiteral("Y"))}), 1);
    QCOMPARE(queue.sourceLabel(), QString());

    queue.setSourceLabel(QStringLiteral("Sunburned Almanac"));
    QCOMPARE(queue.playNext(QList<MediaItem>{}), 0);
    QCOMPARE(queue.sourceLabel(), QStringLiteral("Sunburned Almanac")); // nothing added: kept
    queue.clear();
    QCOMPARE(queue.sourceLabel(), QString());
}
```

In `tests/integration/tst_item_actions_queue.cpp`, add the slot `void playAllFromIfCurrentStampsTheSourceLabel();`. Ids `301001`/`301002` already have PlaybackInfo routes in `init()`:

```cpp
void ItemActionsQueueTest::playAllFromIfCurrentStampsTheSourceLabel()
{
    const QVariantList items{QVariantMap{{"itemId", "301001"}, {"type", "Audio"}, {"name", "One"}},
                             QVariantMap{{"itemId", "301002"}, {"type", "Audio"}, {"name", "Two"}}};
    quint64 generation = m_actions->reservePlaybackIntent();
    m_actions->playAllFromIfCurrent(items, 0, generation, QStringLiteral("Radio · Björk"));
    QCOMPARE(m_player->queue()->sourceLabel(), QStringLiteral("Radio · Björk"));

    generation = m_actions->reservePlaybackIntent();
    m_actions->playAllFromIfCurrent(items, 1, generation);
    QCOMPARE(m_player->queue()->sourceLabel(), QString());

    // A superseded intent changes nothing, the label included.
    const quint64 stale = m_actions->reservePlaybackIntent();
    m_actions->reservePlaybackIntent();
    m_actions->playAllFromIfCurrent(items, 0, stale, QStringLiteral("Stale"));
    QCOMPARE(m_player->queue()->sourceLabel(), QString());
}
```

- [x] **Step 2: Run the tests to verify they fail**

Run: `cmake --build --preset dev --target tst_play_queue tst_item_actions_queue`
Expected: a compile error (`sourceLabel` is not a member of `PlayQueue`).

- [x] **Step 3: Implement it in `PlayQueue`**

`PlayQueue.h`:
- Next to `contextLabel`, add:

```cpp
    // Where the verb that filled this queue says it came from: "Sunburned
    // Almanac", "Station · Heavy rotation", "Radio · Björk". Unlike
    // contextLabel it is remembered, so it is dropped the moment the queue
    // stops being that one thing: a new queue, a clear, or anything added.
    Q_PROPERTY(QString sourceLabel READ sourceLabel NOTIFY sourceLabelChanged)
```

- In `public:`, add:

```cpp
    QString sourceLabel() const { return m_sourceLabel; }
    void setSourceLabel(const QString &label);
```

- Add `QString sourceLabel;` to `struct Snapshot`, after `repeatMode`.
- Add `void sourceLabelChanged();` to `signals:`.
- Add `QString m_sourceLabel;` to the private members.

`PlayQueue.cpp`:

```cpp
void PlayQueue::setSourceLabel(const QString &label)
{
    if (m_sourceLabel == label)
        return;
    m_sourceLabel = label;
    emit sourceLabelChanged();
}
```

Then make these edits:
- `setItems`: call `setSourceLabel({});` immediately before `emit queueChanged();`.
- `clear`: call `setSourceLabel({});` immediately before `emit queueChanged();`. It goes after the early return, so clearing an empty queue stays silent.
- `snapshot`: add `snap.sourceLabel = m_sourceLabel;` next to `snap.repeatMode = m_repeatMode;`.
- `restore`: add `setSourceLabel(snapshot.sourceLabel);` as the last statement.
- `addToQueue(const QList<MediaItem> &)` and `playNext(const QList<MediaItem> &)`: each ends with `return static_cast<int>(valid.size());`. Replace that line in both with:

```cpp
    setSourceLabel({});
    return static_cast<int>(valid.size());
```

The earlier `return 0;` paths stay as they are, so adding nothing keeps the label.

- [x] **Step 4: Implement it in `ItemActions`**

`ItemActions.h:98`:

```cpp
    // `sourceLabel` names where the queue came from ("Radio · Björk"); set after
    // the queue is replaced, so PlayQueue's reset does not wipe it.
    void playAllFromIfCurrent(const QVariantList &items, int startIndex, quint64 generation,
                              const QString &sourceLabel = QString());
```

`ItemActions.cpp`:
- Give the definition the same fourth parameter, without the default.
- Replace:

```cpp
    m_player->playQueue(playable, playableStart);
    emit queueChanged();
```

with:

```cpp
    m_player->playQueue(playable, playableStart);
    m_player->queue()->setSourceLabel(sourceLabel);
    emit queueChanged();
```

- [x] **Step 5: Run the tests to verify they pass**

Run: `cmake --build --preset dev --target tst_play_queue tst_item_actions_queue && ctest --preset dev -R "tst_play_queue|tst_item_actions_queue" --output-on-failure`
Expected: PASS, with every existing test still passing.

- [x] **Step 6: Commit**

```bash
git add src/app/PlayQueue.* src/app/ItemActions.* tests/unit/tst_play_queue.cpp tests/integration/tst_item_actions_queue.cpp
git commit -m "feat(queue): remember where a queue came from

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 17: `MusicPlayback`

**Files:**
- Create: `src/app/music/MusicPlayback.h`, `src/app/music/MusicPlayback.cpp`
- Modify: `src/CMakeLists.txt` (`strmqt_app`)
- Test: `tests/integration/tst_music_playback.cpp`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes:
  - `MusicRepository::albumTracks`, `resolveStation`, `sampleTracks` (Tasks 10–12)
  - `toMediaItem(const Track &)` (Task 3), `MediaItemModel::mapForItem` (Task 13), `StationModel::kindFromKey` (Task 14)
  - `ItemActions::reservePlaybackIntent`, `isPlaybackIntentCurrent`, `playAllFromIfCurrent(…, sourceLabel)` (Task 16), and the `actionFailed` signal
- Produces (namespace `strmqt::music`), `class MusicPlayback : QObject`, exposed to QML as `MusicPlay`:
  - `MusicPlayback(MusicRepository *repository, ItemActions *actions, QObject *parent = nullptr)`
  - `void playTracks(const QList<Track> &tracks, int startIndex, const QString &sourceLabel)`: synchronous; takes a fresh intent
  - `Q_INVOKABLE void playAlbum(const QString &albumId, const QString &title, int startIndex = 0)`: label = title
  - `Q_INVOKABLE void shuffleAlbum(const QString &albumId, const QString &title)`: label `"Shuffle · " + title`
  - `Q_INVOKABLE void radio(const QString &seedId, const QString &seedName)`: InstantMix via `StationKind::MoreLike`, label `"Radio · " + seedName`
  - `void playStation(const QString &libraryId, const Station &station)`: label `"Station · " + station.label`
  - `Q_INVOKABLE void playStationTile(const QString &libraryId, const QVariantMap &tile)`: takes a `StationModel::get(row)` map
  - `void shuffleQuery(const MusicQuery &query, const QString &label)`: `sampleTracks`, label `"Shuffle · " + label`
  - `static QVariantList toMaps(const QList<Track> &tracks)`

Rules:
- Every verb reserves a playback intent *before* its request. A reply for a superseded intent is dropped silently, which matches ItemActions' own rule: the last click wins.
- A failed request emits `ItemActions::actionFailed(tr("Couldn't start playback: %1").arg(error))`, and the queue is left alone.
- Shuffles shuffle the track list before queueing and start at row 0. The queue's own shuffle flag stays off, so ⇄ in the player still reorders the queue from that list.

- [ ] **Step 1: Write the failing test**

`tests/integration/tst_music_playback.cpp`:

```cpp
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

#include "FakePlayerBackend.h"
#include "MockEmbyServer.h"
#include "app/ItemActions.h"
#include "app/PlayQueue.h"
#include "app/controllers/PlayerController.h"
#include "app/music/MusicPlayback.h"
#include "app/music/MusicRepository.h"
#include "core/Settings.h"
#include "server/emby/EmbyClient.h"

using namespace strmqt;
using namespace strmqt::music;

namespace {

const auto kUserId = QStringLiteral("a1b2c3d4e5f60718293a4b5c6d7e8f90");
const auto kToken = QStringLiteral("not-a-real-token-fixture-only");
const auto kLibrary = QStringLiteral("1868998");

QString fixturePath(const QString &name)
{
    return QStringLiteral(STRMQT_FIXTURES_DIR "/") + name;
}

QString itemsPath() { return QStringLiteral("/Users/%1/Items").arg(kUserId); }

QJsonObject trackJson(const QString &id, const QString &albumId, int number)
{
    return {{"Id", id}, {"Name", "Track " + id}, {"Type", "Audio"}, {"AlbumId", albumId},
            {"Album", "Album " + albumId}, {"ParentIndexNumber", 1}, {"IndexNumber", number},
            {"RunTimeTicks", 4 * 60 * 10'000'000LL}};
}

QByteArray page(const QJsonArray &items)
{
    return QJsonDocument(QJsonObject{{"Items", items}, {"TotalRecordCount", items.size()}})
        .toJson(QJsonDocument::Compact);
}

QStringList queueIds(PlayQueue *queue)
{
    QStringList ids;
    for (int row = 0; row < queue->rowCount(); ++row)
        ids.append(queue->itemAt(row).value(QStringLiteral("itemId")).toString());
    return ids;
}

using Q = QList<QPair<QString, QString>>;

} // namespace

class MusicPlaybackTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void playAlbumQueuesInDiscOrderFromTheStart();
    void shuffleAlbumKeepsEveryTrack();
    void radioDeduplicatesAndLabels();
    void stationTileResolvesByKey();
    void failureLeavesTheQueueAndReports();
    void lastVerbWins();

private:
    PlayQueue *queue() const { return m_player->queue(); }

    MockEmbyServer *m_mock = nullptr;
    emby::EmbyClient *m_client = nullptr;
    FakePlayerBackend *m_backend = nullptr;
    QTemporaryDir *m_dir = nullptr;
    Settings *m_settings = nullptr;
    PlayerController *m_player = nullptr;
    ItemActions *m_actions = nullptr;
    MusicRepository *m_repo = nullptr;
    MusicPlayback *m_playback = nullptr;
};

void MusicPlaybackTest::init()
{
    m_mock = new MockEmbyServer(this);
    QVERIFY(m_mock->start());
    const QStringList ids{"a1", "a2", "a3", "b1", "b2", "m1", "m2"};
    for (const QString &id : ids) {
        QVERIFY(m_mock->addRouteFromFile("POST", QStringLiteral("/Items/%1/PlaybackInfo").arg(id),
                                         fixturePath(QStringLiteral("playback_info.json"))));
    }
    m_mock->addRoute("POST", "/Sessions/Playing", 204, {});
    m_mock->addRoute("POST", "/Sessions/Playing/Progress", 204, {});
    m_mock->addRoute("POST", "/Sessions/Playing/Stopped", 204, {});
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"ParentId", "alA"}}, 200,
                          page({trackJson("a1", "alA", 1), trackJson("a2", "alA", 2), trackJson("a3", "alA", 3)}));
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"ParentId", "alB"}}, 200,
                          page({trackJson("b1", "alB", 1), trackJson("b2", "alB", 2)}));
    m_mock->addQueryRoute("GET", itemsPath(), Q{{"ParentId", "gone"}}, 500, "{}");
    m_mock->addRoute("GET", "/Items/ar1/InstantMix", 200,
                     page({trackJson("m1", "x", 1), trackJson("m2", "x", 2), trackJson("m1", "x", 1)}));

    m_client = new emby::EmbyClient(this);
    m_client->setBaseUrl(m_mock->baseUrl());
    m_client->setDeviceId(QStringLiteral("test-device"));
    m_client->setSession(kToken, kUserId);

    m_backend = new FakePlayerBackend(this);
    m_dir = new QTemporaryDir;
    QVERIFY(m_dir->isValid());
    m_settings = new Settings(m_dir->filePath(QStringLiteral("settings.ini")), this);
    m_player = new PlayerController(m_client, m_backend, m_settings, this);
    m_actions = new ItemActions(m_client, m_player, this);
    m_repo = new MusicRepository(m_client, this);
    m_playback = new MusicPlayback(m_repo, m_actions, this);
}

void MusicPlaybackTest::cleanup()
{
    delete m_playback;
    delete m_repo;
    delete m_actions;
    delete m_player;
    delete m_settings;
    delete m_dir;
    delete m_backend;
    delete m_client;
    delete m_mock;
    m_playback = nullptr;
    m_repo = nullptr;
    m_actions = nullptr;
    m_player = nullptr;
    m_settings = nullptr;
    m_dir = nullptr;
    m_backend = nullptr;
    m_client = nullptr;
    m_mock = nullptr;
}

void MusicPlaybackTest::playAlbumQueuesInDiscOrderFromTheStart()
{
    m_playback->playAlbum(QStringLiteral("alA"), QStringLiteral("Sunburned Almanac"), 1);
    QTRY_COMPARE(queueIds(queue()), (QStringList{"a1", "a2", "a3"}));
    QCOMPARE(queue()->currentIndex(), 1);
    QCOMPARE(queue()->sourceLabel(), QStringLiteral("Sunburned Almanac"));
    QCOMPARE(queue()->itemAt(0).value("type").toString(), QStringLiteral("Audio"));
}

void MusicPlaybackTest::shuffleAlbumKeepsEveryTrack()
{
    m_playback->shuffleAlbum(QStringLiteral("alA"), QStringLiteral("Sunburned Almanac"));
    QTRY_COMPARE(queue()->rowCount(), 3);
    QStringList ids = queueIds(queue());
    ids.sort();
    QCOMPARE(ids, (QStringList{"a1", "a2", "a3"}));
    QCOMPARE(queue()->currentIndex(), 0);
    QVERIFY(!queue()->shuffled());
    QCOMPARE(queue()->sourceLabel(), QStringLiteral("Shuffle · Sunburned Almanac"));
}

void MusicPlaybackTest::radioDeduplicatesAndLabels()
{
    m_playback->radio(QStringLiteral("ar1"), QStringLiteral("Björk"));
    QTRY_COMPARE(queueIds(queue()), (QStringList{"m1", "m2"}));
    QCOMPARE(queue()->sourceLabel(), QStringLiteral("Radio · Björk"));
}

void MusicPlaybackTest::stationTileResolvesByKey()
{
    m_playback->playStationTile(kLibrary, QVariantMap{{"itemId", "moreLike"}, {"label", "More like Björk"},
                                                      {"seedId", "ar1"}});
    QTRY_COMPARE(queue()->rowCount(), 2);
    QCOMPARE(queue()->sourceLabel(), QStringLiteral("Station · More like Björk"));

    QSignalSpy failed(m_actions, &ItemActions::actionFailed);
    m_playback->playStationTile(kLibrary, QVariantMap{{"itemId", "bogus"}});
    QCOMPARE(failed.count(), 1);
}

void MusicPlaybackTest::failureLeavesTheQueueAndReports()
{
    m_playback->playAlbum(QStringLiteral("alB"), QStringLiteral("B"));
    QTRY_COMPARE(queue()->rowCount(), 2);
    QSignalSpy failed(m_actions, &ItemActions::actionFailed);
    m_playback->playAlbum(QStringLiteral("gone"), QStringLiteral("Gone"));
    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(queueIds(queue()), (QStringList{"b1", "b2"}));
    QCOMPARE(queue()->sourceLabel(), QStringLiteral("B"));
}

void MusicPlaybackTest::lastVerbWins()
{
    m_playback->playAlbum(QStringLiteral("alA"), QStringLiteral("A"));
    m_playback->playAlbum(QStringLiteral("alB"), QStringLiteral("B")); // before A's reply
    QTRY_COMPARE(queueIds(queue()), (QStringList{"b1", "b2"}));
    QTest::qWait(100); // A's reply, if it were not dropped, would land now
    QCOMPARE(queueIds(queue()), (QStringList{"b1", "b2"}));
    QCOMPARE(queue()->sourceLabel(), QStringLiteral("B"));
}

QTEST_GUILESS_MAIN(MusicPlaybackTest)
#include "tst_music_playback.moc"
```

Add to `tests/CMakeLists.txt`:

```cmake
strmqt_add_test(tst_music_playback
    integration/tst_music_playback.cpp
    mocks/MockEmbyServer.h mocks/MockEmbyServer.cpp
    mocks/FakePlayerBackend.h
)
target_include_directories(tst_music_playback PRIVATE mocks)
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build --preset dev --target tst_music_playback`
Expected: a compile error (`MusicPlayback.h` not found).

- [ ] **Step 3: Write `MusicPlayback`**

`src/app/music/MusicPlayback.h`:

```cpp
#pragma once

#include <QFuture>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include "core/Result.h"
#include "server/dto/music/MusicQuery.h"
#include "server/dto/music/MusicTypes.h"

namespace strmqt {
class ItemActions;
}

namespace strmqt::music {

class MusicRepository;

// Every music play verb in one place (exposed to QML as MusicPlay). Resolves
// tracks through the repository, converts them with toMediaItem and hands them
// to ItemActions with a source label, so ItemActions and PlayQueue never learn
// about music DTOs (Crate spec §3.6.1–2).
class MusicPlayback : public QObject
{
    Q_OBJECT

public:
    MusicPlayback(MusicRepository *repository, ItemActions *actions, QObject *parent = nullptr);

    void playTracks(const QList<Track> &tracks, int startIndex, const QString &sourceLabel);
    Q_INVOKABLE void playAlbum(const QString &albumId, const QString &title, int startIndex = 0);
    Q_INVOKABLE void shuffleAlbum(const QString &albumId, const QString &title);
    Q_INVOKABLE void radio(const QString &seedId, const QString &seedName);
    void playStation(const QString &libraryId, const Station &station);
    Q_INVOKABLE void playStationTile(const QString &libraryId, const QVariantMap &tile);
    void shuffleQuery(const MusicQuery &query, const QString &label);

    static QVariantList toMaps(const QList<Track> &tracks);

private:
    enum class Order { AsGiven, Shuffled };

    void playResolved(QFuture<Result<QList<Track>>> future, quint64 generation, int startIndex,
                      Order order, const QString &sourceLabel);
    void reportFailure(const QString &error);

    MusicRepository *m_repository;
    ItemActions *m_actions;
};

} // namespace strmqt::music
```

`src/app/music/MusicPlayback.cpp`:

```cpp
#include "app/music/MusicPlayback.h"

#include <QRandomGenerator>

#include <algorithm>

#include "app/ItemActions.h"
#include "app/models/MediaItemModel.h"
#include "app/music/MusicRepository.h"
#include "app/music/models/StationModel.h"
#include "server/dto/music/MusicMediaItem.h"

namespace strmqt::music {

MusicPlayback::MusicPlayback(MusicRepository *repository, ItemActions *actions, QObject *parent)
    : QObject(parent)
    , m_repository(repository)
    , m_actions(actions)
{
}

QVariantList MusicPlayback::toMaps(const QList<Track> &tracks)
{
    QVariantList maps;
    maps.reserve(tracks.size());
    for (const Track &track : tracks)
        maps.append(MediaItemModel::mapForItem(toMediaItem(track)));
    return maps;
}

void MusicPlayback::reportFailure(const QString &error)
{
    emit m_actions->actionFailed(tr("Couldn't start playback: %1").arg(error));
}

void MusicPlayback::playTracks(const QList<Track> &tracks, int startIndex, const QString &sourceLabel)
{
    const quint64 generation = m_actions->reservePlaybackIntent();
    m_actions->playAllFromIfCurrent(toMaps(tracks), startIndex, generation, sourceLabel);
}

void MusicPlayback::playResolved(QFuture<Result<QList<Track>>> future, quint64 generation,
                                 int startIndex, Order order, const QString &sourceLabel)
{
    future.then(this, [this, generation, startIndex, order, sourceLabel](Result<QList<Track>> result) {
        if (!m_actions->isPlaybackIntentCurrent(generation))
            return;
        if (!result.ok()) {
            reportFailure(result.error);
            return;
        }
        QList<Track> tracks = std::move(result.value);
        if (order == Order::Shuffled)
            std::shuffle(tracks.begin(), tracks.end(), *QRandomGenerator::global());
        m_actions->playAllFromIfCurrent(toMaps(tracks), order == Order::Shuffled ? 0 : startIndex,
                                        generation, sourceLabel);
    });
}

void MusicPlayback::playAlbum(const QString &albumId, const QString &title, int startIndex)
{
    const quint64 generation = m_actions->reservePlaybackIntent();
    playResolved(m_repository->albumTracks(albumId), generation, startIndex, Order::AsGiven, title);
}

void MusicPlayback::shuffleAlbum(const QString &albumId, const QString &title)
{
    const quint64 generation = m_actions->reservePlaybackIntent();
    playResolved(m_repository->albumTracks(albumId), generation, 0, Order::Shuffled,
                 tr("Shuffle · %1").arg(title));
}

void MusicPlayback::radio(const QString &seedId, const QString &seedName)
{
    const quint64 generation = m_actions->reservePlaybackIntent();
    Station station;
    station.kind = StationKind::MoreLike;
    station.seedId = seedId;
    playResolved(m_repository->resolveStation(QString(), station), generation, 0, Order::AsGiven,
                 tr("Radio · %1").arg(seedName));
}

void MusicPlayback::playStation(const QString &libraryId, const Station &station)
{
    const quint64 generation = m_actions->reservePlaybackIntent();
    // The repository already orders each station (heavy rotation is shuffled
    // there), so the list is queued as given.
    playResolved(m_repository->resolveStation(libraryId, station), generation, 0, Order::AsGiven,
                 tr("Station · %1").arg(station.label));
}

void MusicPlayback::playStationTile(const QString &libraryId, const QVariantMap &tile)
{
    const auto kind = StationModel::kindFromKey(tile.value(QStringLiteral("itemId")).toString());
    if (!kind) {
        reportFailure(tr("unknown station"));
        return;
    }
    Station station;
    station.kind = *kind;
    station.label = tile.value(QStringLiteral("label")).toString();
    station.seedId = tile.value(QStringLiteral("seedId")).toString();
    playStation(libraryId, station);
}

void MusicPlayback::shuffleQuery(const MusicQuery &query, const QString &label)
{
    const quint64 generation = m_actions->reservePlaybackIntent();
    // sampleTracks is already random server-side; no second shuffle.
    playResolved(m_repository->sampleTracks(query), generation, 0, Order::AsGiven,
                 tr("Shuffle · %1").arg(label));
}

} // namespace strmqt::music
```

Add to `strmqt_app`:

```cmake
    app/music/MusicPlayback.h app/music/MusicPlayback.cpp
```

`emit m_actions->actionFailed(...)` from outside `ItemActions` is legal because Qt 6 signals are public. It keeps a single failure toast path in `Main.qml`.

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build --preset dev --target tst_music_playback && ctest --preset dev -R tst_music_playback --output-on-failure`
Expected: PASS (6 tests).

If `lastVerbWins` shows `a1…` after the wait, `reservePlaybackIntent` is not being called before the request. Fix the order; do not loosen the test.

- [ ] **Step 5: Commit**

```bash
git add src/app/music/MusicPlayback.* src/CMakeLists.txt tests/integration/tst_music_playback.cpp tests/CMakeLists.txt
git commit -m "feat(music): add MusicPlayback verbs with queue provenance

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 18: Crate tokens in `Theme.qml`

**Files:**
- Modify: `src/ui/Theme.qml` (a new "Crate" section after "Layout metrics")
- Test: `tests/unit/tst_crate_tokens.cpp`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: none (independent).
- Produces: `Theme` singleton properties that every Crate control from Phase 2 onwards uses. Do not re-derive these values in a control.

| Property | Type | Value |
|---|---|---|
| `crateDisplayAxes` | `var` | `({ "wdth": 120, "wght": 820 })`, for `font.variableAxes` on `fontDisplay` |
| `crateDisplayWeight` | `int` | `820`, for `font.weight` when the variable axes are not honoured |
| `crateDisplayTracking` | `real` | `-0.02` (em; multiply by the pixel size for `font.letterSpacing`) |
| `crateHeroHome` / `crateHeroAlbum` / `crateHeroArtist` | `int` | `scale(52)` / `scale(58)` / `scale(84)` |
| `crateShelfHeading` | `int` | `scale(26)` |
| `crateStripSize` | `int` | `scale(15)` (section strip labels) |
| `crateKickerSize` | `int` | `scale(11)` |
| `crateKickerTracking` | `real` | `0.17` (em) |
| `crateBadgeSize` | `int` | `scale(10.5)` (11 px at comfortable: `pixelSize` is an integer) |
| `crateBadgeRadius` / `crateBadgeBorderWidth` | `int` | `3` / `1` |
| `crateBadgeBorder` | `color` | `theme.textDisabled` |
| `crateBadgeHiRes` | `color` | `theme.accentColor` |
| `crateSleeveRadius` | `int` | `3` |
| `crateSleeveElevation` | `var` | `theme.elevation4` |
| `crateSleeveSize` / `crateSleeveSizeLarge` | `int` | `scale(178)` / `scale(260)` (shelf / hero sleeve) |
| `cratePortraitSize` | `int` | `scale(150)` (circular artist portrait) |

Crate adds no colours (spec §2). The mockup badge border `#4a443d` is within 2/255 of the existing `textDisabled` (`#4A453F`), so the token aliases it rather than adding a literal.

- [x] **Step 1: Write the failing test**

`tests/unit/tst_crate_tokens.cpp`:

```cpp
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QTest>

namespace {

const char *kProbe = R"QML(
import QtQuick
import StrmQt

QtObject {
    readonly property var axes: Theme.crateDisplayAxes
    readonly property int weight: Theme.crateDisplayWeight
    readonly property real tracking: Theme.crateDisplayTracking
    readonly property int heroHome: Theme.crateHeroHome
    readonly property int heroAlbum: Theme.crateHeroAlbum
    readonly property int heroArtist: Theme.crateHeroArtist
    readonly property int kicker: Theme.crateKickerSize
    readonly property real kickerTracking: Theme.crateKickerTracking
    readonly property int badge: Theme.crateBadgeSize
    readonly property int badgeRadius: Theme.crateBadgeRadius
    readonly property color badgeBorder: Theme.crateBadgeBorder
    readonly property color disabled: Theme.textDisabled
    readonly property color hiRes: Theme.crateBadgeHiRes
    readonly property color accent: Theme.accentColor
    readonly property int sleeveRadius: Theme.crateSleeveRadius
    readonly property var sleeveElevation: Theme.crateSleeveElevation
    readonly property var elevation4: Theme.elevation4
    readonly property int sleeve: Theme.crateSleeveSize
    function tvHero() { Theme.densityMode = "tv"; const v = Theme.crateHeroArtist; Theme.densityMode = "comfortable"; return v; }
}
)QML";

} // namespace

class TestCrateTokens : public QObject
{
    Q_OBJECT

private slots:
    void tokensMatchTheSpec();
};

void TestCrateTokens::tokensMatchTheSpec()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString modulePath = dir.filePath(QStringLiteral("StrmQt"));
    QVERIFY(QDir().mkpath(modulePath));
    QVERIFY(QFile::copy(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/Theme.qml"),
                        modulePath + QStringLiteral("/Theme.qml")));
    QFile qmldir(modulePath + QStringLiteral("/qmldir"));
    QVERIFY(qmldir.open(QIODevice::WriteOnly));
    qmldir.write("module StrmQt\nsingleton Theme 1.0 Theme.qml\n");
    qmldir.close();

    QQmlEngine engine;
    engine.addImportPath(dir.path());
    QQmlComponent component(&engine);
    component.setData(kProbe, QUrl::fromLocalFile(dir.filePath(QStringLiteral("Probe.qml"))));
    std::unique_ptr<QObject> probe(component.create());
    QVERIFY2(probe, qPrintable(component.errorString()));

    const QVariantMap axes = probe->property("axes").toMap();
    QCOMPARE(axes.value("wdth").toInt(), 120);
    QCOMPARE(axes.value("wght").toInt(), 820);
    QCOMPARE(probe->property("weight").toInt(), 820);
    QCOMPARE(probe->property("tracking").toReal(), -0.02);
    QCOMPARE(probe->property("heroHome").toInt(), 52);
    QCOMPARE(probe->property("heroAlbum").toInt(), 58);
    QCOMPARE(probe->property("heroArtist").toInt(), 84);
    QCOMPARE(probe->property("kicker").toInt(), 11);
    QCOMPARE(probe->property("kickerTracking").toReal(), 0.17);
    QCOMPARE(probe->property("badge").toInt(), 11);
    QCOMPARE(probe->property("badgeRadius").toInt(), 3);
    QCOMPARE(probe->property("badgeBorder"), probe->property("disabled"));
    QCOMPARE(probe->property("hiRes"), probe->property("accent"));
    QCOMPARE(probe->property("sleeveRadius").toInt(), 3);
    QCOMPARE(probe->property("sleeveElevation").toMap(), probe->property("elevation4").toMap());
    QCOMPARE(probe->property("sleeve").toInt(), 178);

    QVariant tvHero;
    QVERIFY(QMetaObject::invokeMethod(probe.get(), "tvHero", Q_RETURN_ARG(QVariant, tvHero)));
    QCOMPARE(tvHero.toInt(), 97); // round(84 × 1.15): hero sizes follow density
}

QTEST_MAIN(TestCrateTokens)
#include "tst_crate_tokens.moc"
```

Add to `tests/CMakeLists.txt`:

```cmake
strmqt_add_test(tst_crate_tokens unit/tst_crate_tokens.cpp)
target_link_libraries(tst_crate_tokens PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick)
target_compile_definitions(tst_crate_tokens PRIVATE STRMQT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
set_tests_properties(tst_crate_tokens PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [x] **Step 2: Run the test to verify it fails**

Run: `cmake --build --preset dev --target tst_crate_tokens && ctest --preset dev -R tst_crate_tokens --output-on-failure`
Expected: FAIL, with the first `QCOMPARE` on `axes` reporting a missing value.

- [x] **Step 3: Add the tokens**

Append inside the root object of `src/ui/Theme.qml`, after `topBarHeight`:

```qml

    // ── Crate (music) ──────────────────────────────────────────────────────
    // Music's dialect of Projection Booth (Crate spec §2): the same ground,
    // accent and typefaces, set louder. Archivo is pushed wide and heavy for
    // headings and hero titles; data stays in Plex Mono. No colours are added
    // here: the badge border is textDisabled, the hi-res badge is the accent.
    readonly property var crateDisplayAxes: ({ "wdth": 120, "wght": 820 })
    readonly property int crateDisplayWeight: 820
    readonly property real crateDisplayTracking: -0.02 // em

    readonly property int crateHeroHome: scale(52)
    readonly property int crateHeroAlbum: scale(58)   // album page and player
    readonly property int crateHeroArtist: scale(84)
    readonly property int crateShelfHeading: scale(26)
    readonly property int crateStripSize: scale(15)

    readonly property int crateKickerSize: scale(11)
    readonly property real crateKickerTracking: 0.17 // em

    readonly property int crateBadgeSize: scale(10.5)
    readonly property int crateBadgeRadius: 3
    readonly property int crateBadgeBorderWidth: 1
    readonly property color crateBadgeBorder: theme.textDisabled
    readonly property color crateBadgeHiRes: theme.accentColor

    // A sleeve is a square cover with a deep shadow and no card behind it.
    readonly property int crateSleeveRadius: 3
    readonly property var crateSleeveElevation: theme.elevation4
    readonly property int crateSleeveSize: scale(178)
    readonly property int crateSleeveSizeLarge: scale(260)
    readonly property int cratePortraitSize: scale(150)
```

The root object's id is `theme`: `accentColor` and `scale()` already use it.

- [x] **Step 4: Run the test and the lint baseline**

Run: `cmake --build --preset dev --target tst_crate_tokens && ctest --preset dev -R "tst_crate_tokens|tst_qml_accessibility" --output-on-failure && bash scripts/check-qmllint-baseline.sh build/dev`
Expected: both tests PASS, and the baseline reports no new warnings.

- [x] **Step 5: Commit**

```bash
git add src/ui/Theme.qml tests/unit/tst_crate_tokens.cpp tests/CMakeLists.txt
git commit -m "feat(theme): add Crate music tokens

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 19: Application wiring and the Phase 1 gate

**Files:**
- Modify: `src/app/Application.h` (forward declarations, accessors, members)
- Modify: `src/app/Application.cpp` (construction, after `m_live` is bound)
- Modify: `src/app/main.cpp` (the `MusicPlay` context property)

**Interfaces:**
- Consumes: `MusicRepository` (Tasks 10–12), `MusicUserDataRelay` (Task 15), `MusicPlayback` (Task 17), `PlayQueue::currentItemChanged`, `LiveUpdateService::libraryInvalidated`/`refreshRequested`.
- Produces:
  - `music::MusicRepository *Application::musicRepository() const`
  - `music::MusicUserDataRelay *Application::musicRelay() const`
  - `music::MusicPlayback *Application::musicPlayback() const`
  - The QML context property `MusicPlay`.

  The Phase 2–5 controllers are built from these accessors and registered with `musicRelay()->addModel(...)`.

This task only wires objects together, so there is no new unit test. The phase gate below is its test: the full suite, the lint baseline and the self-test, where `Main.qml` constructs every page against the real context properties.

- [ ] **Step 1: Declare the members**

In `src/app/Application.h`, after the existing forward declarations inside `namespace strmqt {`:

```cpp
namespace music {
class MusicPlayback;
class MusicRepository;
class MusicUserDataRelay;
} // namespace music
```

Next to `MusicController *music() const`:

```cpp
    music::MusicRepository *musicRepository() const { return m_musicRepository; }
    music::MusicUserDataRelay *musicRelay() const { return m_musicRelay; }
    music::MusicPlayback *musicPlayback() const { return m_musicPlayback; }
```

Next to `MusicController *m_music = nullptr;`:

```cpp
    music::MusicRepository *m_musicRepository = nullptr;
    music::MusicUserDataRelay *m_musicRelay = nullptr;
    music::MusicPlayback *m_musicPlayback = nullptr;
```

- [ ] **Step 2: Construct and connect**

In `src/app/Application.cpp`, add these includes:

```cpp
#include "PlayQueue.h"
#include "music/MusicPlayback.h"
#include "music/MusicRepository.h"
#include "music/MusicUserDataRelay.h"
```

Insert this block immediately after `m_details->bindLiveUpdates(m_live);`:

```cpp
    // The music domain layer (Crate spec §3). One repository per account: it
    // clears itself on EmbyClient::identityChanged. The relay turns every
    // user-data change into in-place model patches plus cache invalidation;
    // MusicPlayback owns the play verbs and is exposed to QML as MusicPlay.
    m_musicRepository = new music::MusicRepository(m_client, this);
    m_musicRelay = new music::MusicUserDataRelay(m_musicRepository, this);
    m_musicRelay->bind(m_actions, m_live);
    m_musicPlayback = new music::MusicPlayback(m_musicRepository, m_actions, this);
    // What is playing changes the listening shelves (continue listening,
    // recently played, artists you play). Marking them stale costs nothing
    // until Music Home next asks.
    connect(m_player->queue(), &PlayQueue::currentItemChanged, m_musicRepository,
            [this] { m_musicRepository->markStale(music::Freshness::Listening); });
    connect(m_live, &LiveUpdateService::libraryInvalidated, m_musicRepository,
            [this](const QStringList &) { m_musicRepository->markStale(music::Freshness::Everything); });
    connect(m_live, &LiveUpdateService::refreshRequested, m_musicRepository,
            [this] { m_musicRepository->markStale(music::Freshness::Everything); });
```

Includes in `Application.cpp` are relative to `src/app/`, as the existing `"ItemActions.h"` and `"controllers/…"` includes show. If the music headers include one another with the `app/` prefix, both forms resolve, because `src/` is on the include path.

- [ ] **Step 3: Expose `MusicPlay`**

In `src/app/main.cpp`, add `#include "music/MusicPlayback.h"` and, after the `MusicCtl` line:

```cpp
    // Every music play verb (Crate spec §3.6): album, shuffle, radio, stations.
    engine.rootContext()->setContextProperty(QStringLiteral("MusicPlay"), app.musicPlayback());
```

- [ ] **Step 4: Run the Phase 1 gate**

```bash
cmake --preset dev && cmake --build --preset dev
ctest --preset dev --output-on-failure
bash scripts/check-qmllint-baseline.sh build/dev
STRMQT_SELFTEST=1 QT_QPA_PLATFORM=offscreen QT_ASSUME_STDERR_HAS_CONSOLE=1 ./build/dev/strmqt; echo "selftest exit $?"
```

Expected:
- The build has no warnings (warnings are errors).
- Every test passes, including the new `tst_music_mapper`, `tst_music_cache`, `tst_music_query_translator`, `tst_music_repository`, `tst_music_models`, `tst_music_user_data_relay`, `tst_music_playback`, `tst_crate_tokens`, and the extended `tst_emby_client`, `tst_play_queue` and `tst_item_actions_queue`.
- The lint baseline reports no new warnings.
- `selftest exit 0`.

If a test fails, fix the cause in the task that owns it. Do not skip or loosen the test.

- [ ] **Step 5: Commit**

```bash
git add src/app/Application.h src/app/Application.cpp src/app/main.cpp
git commit -m "feat(music): wire the music repository, relay and playback into the app

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

If the wave directories from parallel agents still exist, remove them in the same step as this commit: `rm -rf /tmp/w1*`.
