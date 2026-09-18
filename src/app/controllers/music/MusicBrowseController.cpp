#include "app/controllers/music/MusicBrowseController.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>

#include <algorithm>

#include "app/music/MusicFormat.h"
#include "app/music/MusicPlayback.h"
#include "app/music/MusicQueryTranslator.h"
#include "app/music/MusicRepository.h"
#include "core/Log.h"

namespace strmqt::music {

namespace {

constexpr int kSectionCount = 5;
constexpr int kPageSize = 100;
constexpr int kGenrePageSize = 48;
const auto kName = QStringLiteral("name");
const auto kRandom = QStringLiteral("random");
const auto kHome = QStringLiteral("home");

int slot(Section section) { return static_cast<int>(section); }

std::optional<Section> parseSection(const QString &key)
{
    if (key == QLatin1String("albums"))
        return Section::Albums;
    if (key == QLatin1String("artists"))
        return Section::Artists;
    if (key == QLatin1String("songs"))
        return Section::Songs;
    if (key == QLatin1String("genres"))
        return Section::Genres;
    if (key == QLatin1String("playlists"))
        return Section::Playlists;
    return std::nullopt;
}

QString keyOf(Section section)
{
    switch (section) {
    case Section::Albums: return QStringLiteral("albums");
    case Section::Artists: return QStringLiteral("artists");
    case Section::Songs: return QStringLiteral("songs");
    case Section::Genres: return QStringLiteral("genres");
    case Section::Playlists: return QStringLiteral("playlists");
    }
    return QStringLiteral("albums");
}

// Albums, Artists and Songs take the shared filters and a letter; Genres and
// Playlists take neither (the translator drops them too).
bool takesFilters(Section section)
{
    return section == Section::Albums || section == Section::Artists || section == Section::Songs;
}

std::optional<FormatFilter> parseFormat(const QString &key)
{
    if (key == QLatin1String("any"))
        return FormatFilter::Any;
    if (key == QLatin1String("lossless"))
        return FormatFilter::Lossless;
    if (key == QLatin1String("lossy"))
        return FormatFilter::Lossy;
    if (key == QLatin1String("hires"))
        return FormatFilter::HiRes;
    return std::nullopt;
}

QString formatKeyOf(FormatFilter format)
{
    switch (format) {
    case FormatFilter::Lossless: return QStringLiteral("lossless");
    case FormatFilter::Lossy: return QStringLiteral("lossy");
    case FormatFilter::HiRes: return QStringLiteral("hires");
    case FormatFilter::Any: break;
    }
    return QStringLiteral("any");
}

// A format is settable when some section can filter by it; the sections that
// cannot simply do not send it (queryFor).
bool formatSupported(FormatFilter format)
{
    return format == FormatFilter::Any
           || MusicQueryTranslator::formatOptions(Section::Albums).contains(format)
           || MusicQueryTranslator::formatOptions(Section::Songs).contains(format);
}

bool validDecade(int decade)
{
    return decade == kDecadeAny || decade == kDecadeEarlier
           || (decade >= 1950 && decade <= 2020 && decade % 10 == 0);
}

const QStringList &letterList()
{
    static const QStringList letters = [] {
        QStringList out{QStringLiteral("#")};
        for (char c = 'A'; c <= 'Z'; ++c)
            out.append(QString(QLatin1Char(c)));
        return out;
    }();
    return letters;
}

const QStringList &cycleKeys()
{
    static const QStringList keys{kHome, QStringLiteral("albums"), QStringLiteral("artists"),
                                  QStringLiteral("songs"), QStringLiteral("genres"),
                                  QStringLiteral("playlists")};
    return keys;
}

// Genres come whole from allGenres, so the Genres section sorts client-side.
// Size keeps names A–Z among equal counts in either direction.
QList<GenreBin> sortGenres(QList<GenreBin> genres, const QString &key, bool descending)
{
    const bool byName = key == kName;
    std::stable_sort(genres.begin(), genres.end(), [&](const GenreBin &a, const GenreBin &b) {
        if (!byName && a.recordCount != b.recordCount)
            return descending ? a.recordCount > b.recordCount : a.recordCount < b.recordCount;
        const int order = QString::localeAwareCompare(a.name, b.name);
        return byName && descending ? order > 0 : order < 0;
    });
    return genres;
}

} // namespace

MusicBrowseController::MusicBrowseController(MusicRepository *repository, MusicPlayback *playback,
                                             QObject *parent)
    : QObject(parent)
    , m_repository(repository)
    , m_playback(playback)
    , m_albums(new AlbumGridModel(this))
    , m_artists(new ArtistGridModel(this))
    , m_songs(new TrackListModel(this))
    , m_genres(new GenreBinModel(this))
    , m_playlists(new PlaylistGridModel(this))
{
    m_models = {m_albums, m_artists, m_songs, m_genres, m_playlists};
    for (int i = 0; i < kSectionCount; ++i) {
        const auto section = static_cast<Section>(i);
        m_lanes[i] = new MusicLane(m_models[i], this);
        connect(m_lanes[i], &MusicLane::retryRequested, this, [this, section] { retry(section); });
    }
    resetQuery(QString());
}

// ── Reading ─────────────────────────────────────────────────────────────────

QString MusicBrowseController::section() const { return keyOf(m_section); }
MusicLane *MusicBrowseController::currentLane() const { return m_lanes[slot(m_section)]; }

QList<MusicModelBase *> MusicBrowseController::models() const
{
    return QList<MusicModelBase *>(m_models.cbegin(), m_models.cend());
}

std::optional<SortKey> MusicBrowseController::sortFor(Section section, const QString &key) const
{
    const QList<SortKey> keys = MusicQueryTranslator::sortKeysFor(section);
    for (const SortKey &candidate : keys) {
        if (candidate.key == key)
            return candidate;
    }
    return std::nullopt;
}

QString MusicBrowseController::sortKey() const { return stateOf(m_section).sortKey; }
bool MusicBrowseController::sortDescending() const { return stateOf(m_section).descending; }

QVariantList MusicBrowseController::availableSorts() const
{
    QVariantList out;
    for (const SortKey &key : MusicQueryTranslator::sortKeysFor(m_section))
        out.append(QVariantMap{{QStringLiteral("key"), key.key}, {QStringLiteral("label"), key.label}});
    return out;
}

QString MusicBrowseController::sortLabel() const
{
    const auto key = sortFor(m_section, sortKey());
    return key ? key->label : QString();
}

QString MusicBrowseController::letter() const { return stateOf(m_section).letter; }
QStringList MusicBrowseController::letters() const { return letterList(); }

bool MusicBrowseController::letterStripVisible() const
{
    return takesFilters(m_section) && stateOf(m_section).sortKey == kName;
}

QString MusicBrowseController::genrePillText() const
{
    if (m_query.genreIds.isEmpty())
        return tr("Genre");
    const QString name = m_query.genreIds.size() == 1 ? m_genreNames.value(m_query.genreIds.first()) : QString();
    return name.isEmpty() ? tr("Genres: %1").arg(m_query.genreIds.size()) : tr("Genre: %1").arg(name);
}

QString MusicBrowseController::decadeLabel(int decade) const
{
    return decade == kDecadeEarlier ? tr("Earlier") : tr("%1s").arg(decade);
}

QString MusicBrowseController::decadePillText() const
{
    if (m_query.decade == kDecadeAny)
        return tr("Decade");
    if (m_query.decade == kDecadeEarlier)
        return tr("Decade: Earlier");
    return tr("Decade: %1s").arg(m_query.decade % 100, 2, 10, QLatin1Char('0'));
}

QString MusicBrowseController::format() const { return formatKeyOf(m_query.format); }

QString MusicBrowseController::formatLabel(FormatFilter format) const
{
    switch (format) {
    case FormatFilter::Lossless: return tr("Lossless");
    case FormatFilter::Lossy: return tr("Lossy");
    case FormatFilter::HiRes: return tr("Hi-res");
    case FormatFilter::Any: break;
    }
    return tr("Any");
}

QString MusicBrowseController::formatPillText() const
{
    return m_query.format == FormatFilter::Any ? tr("Format") : tr("Format: %1").arg(formatLabel(m_query.format));
}

bool MusicBrowseController::formatAvailable() const { return MusicQueryTranslator::formatFilterable(m_section); }
bool MusicBrowseController::decadeAvailable() const
{
    return m_section == Section::Albums || m_section == Section::Songs;
}
bool MusicBrowseController::filtersAvailable() const { return takesFilters(m_section); }

QString MusicBrowseController::artistMode() const
{
    return m_query.artistMode == ArtistMode::Everyone ? QStringLiteral("everyone") : QStringLiteral("albumArtists");
}

bool MusicBrowseController::filtered() const { return queryFor(m_section).hasFilters(); }

int MusicBrowseController::activeFilterCount() const
{
    const MusicQuery query = queryFor(m_section);
    return int(!query.genreIds.isEmpty()) + int(query.decade != kDecadeAny)
           + int(query.format != FormatFilter::Any) + int(query.favouritesOnly) + int(query.unplayedOnly);
}

QString MusicBrowseController::scopeLabel() const
{
    const MusicQuery scope = playScope();
    QStringList parts;
    if (scope.genreIds.size() == 1) {
        const QString name = m_genreNames.value(scope.genreIds.first());
        parts.append(name.isEmpty() ? tr("1 genre") : name);
    } else if (scope.genreIds.size() > 1) {
        parts.append(tr("%1 genres").arg(scope.genreIds.size()));
    }
    if (scope.decade == kDecadeEarlier)
        parts.append(tr("Before 1950"));
    else if (scope.decade > 0)
        parts.append(decadeLabel(scope.decade));
    if (scope.format != FormatFilter::Any)
        parts.append(formatLabel(scope.format));
    if (scope.favouritesOnly)
        parts.append(tr("Favourites"));
    if (scope.unplayedOnly)
        parts.append(tr("Unplayed"));
    return parts.isEmpty() ? tr("All music") : parts.join(QStringLiteral(" · "));
}

QString MusicBrowseController::routeState() const
{
    QJsonArray genres;
    for (const QString &id : m_query.genreIds)
        genres.append(id);
    QJsonArray sorts;
    for (const SectionState &state : m_sections) {
        sorts.append(QJsonObject{{QStringLiteral("k"), state.sortKey},
                                 {QStringLiteral("d"), state.descending},
                                 {QStringLiteral("l"), state.letter}});
    }
    const QJsonObject root{{QStringLiteral("v"), 1},
                           {QStringLiteral("g"), genres},
                           {QStringLiteral("d"), m_query.decade},
                           {QStringLiteral("f"), format()},
                           {QStringLiteral("fav"), m_query.favouritesOnly},
                           {QStringLiteral("un"), m_query.unplayedOnly},
                           {QStringLiteral("am"), artistMode()},
                           {QStringLiteral("s"), sorts}};
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

int MusicBrowseController::resultCount() const { return stateOf(m_section).resultCount; }
int MusicBrowseController::unfilteredCount() const { return stateOf(m_section).unfilteredCount; }

QString MusicBrowseController::countNoun(Section section, int count) const
{
    const bool one = count == 1;
    switch (section) {
    case Section::Albums: return one ? tr("RECORD") : tr("RECORDS");
    case Section::Artists: return one ? tr("ARTIST") : tr("ARTISTS");
    case Section::Songs: return one ? tr("SONG") : tr("SONGS");
    case Section::Genres: return one ? tr("GENRE") : tr("GENRES");
    case Section::Playlists: return one ? tr("PLAYLIST") : tr("PLAYLISTS");
    }
    return {};
}

QString MusicBrowseController::countText() const
{
    const SectionState &state = stateOf(m_section);
    if (state.resultCount < 0)
        return {};
    // The mono readout groups like the mockup, whatever the system locale.
    const QLocale en(QLocale::English, QLocale::UnitedStates);
    const QString noun = countNoun(m_section, state.resultCount);
    QString text = narrowed(m_section) && state.unfilteredCount >= 0
                       ? tr("%1 → %2 %3").arg(en.toString(state.unfilteredCount), en.toString(state.resultCount), noun)
                       : tr("%1 %2").arg(en.toString(state.resultCount), noun);
    text += tr(" · SORT: %1").arg(sortLabel().toUpper());
    if (state.sortKey != kRandom)
        text += state.descending ? QStringLiteral(" ↓") : QStringLiteral(" ↑");
    return text;
}

QVariantList MusicBrowseController::genreOptions() const
{
    QVariantList out;
    out.reserve(m_genreOptions.size());
    for (const GenreBin &genre : m_genreOptions) {
        out.append(QVariantMap{{QStringLiteral("id"), genre.id},
                               {QStringLiteral("name"), genre.name},
                               {QStringLiteral("count"), genre.recordCount},
                               {QStringLiteral("subtitle"), formatRecordCount(genre.recordCount)},
                               {QStringLiteral("selected"), m_query.genreIds.contains(genre.id)}});
    }
    return out;
}

QVariantList MusicBrowseController::decadeOptions() const
{
    QVariantList out;
    for (int decade = 1950; decade <= 2020; decade += 10)
        out.append(QVariantMap{{QStringLiteral("value"), decade}, {QStringLiteral("label"), decadeLabel(decade)}});
    out.append(QVariantMap{{QStringLiteral("value"), kDecadeEarlier}, {QStringLiteral("label"), decadeLabel(kDecadeEarlier)}});
    return out;
}

QVariantList MusicBrowseController::formatOptions() const
{
    QVariantList out;
    for (FormatFilter format : MusicQueryTranslator::formatOptions(m_section))
        out.append(QVariantMap{{QStringLiteral("key"), formatKeyOf(format)}, {QStringLiteral("label"), formatLabel(format)}});
    return out;
}

// ── Query model ─────────────────────────────────────────────────────────────

// What a section actually sends: the shared filters it can take plus its own
// sort and letter.
MusicQuery MusicBrowseController::queryFor(Section section) const
{
    const SectionState &state = stateOf(section);
    MusicQuery query = m_query;
    query.section = section;
    query.sortKey = state.sortKey;
    query.descending = state.descending;
    query.letter = takesFilters(section) && state.sortKey == kName ? state.letter : QString();
    if (!takesFilters(section)) {
        query.genreIds.clear();
        query.decade = kDecadeAny;
        query.format = FormatFilter::Any;
        query.favouritesOnly = false;
        query.unplayedOnly = false;
    }
    if (section == Section::Artists)
        query.decade = kDecadeAny; // the /Artists endpoints ignore years
    else
        query.artistMode = ArtistMode::AlbumArtists;
    if (!MusicQueryTranslator::formatFilterable(section))
        query.format = FormatFilter::Any;
    return query;
}

// The same section with nothing narrowing it: the left half of the readout.
MusicQuery MusicBrowseController::baseFor(Section section) const
{
    MusicQuery base = queryFor(section);
    base.genreIds.clear();
    base.decade = kDecadeAny;
    base.format = FormatFilter::Any;
    base.favouritesOnly = false;
    base.unplayedOnly = false;
    base.letter.clear();
    base.sortKey = kName;
    base.descending = false;
    return base;
}

// ▶ Play and ⇄ Shuffle: the Songs scope of the shared filters. Songs keeps its
// own order; any other section plays in album order.
MusicQuery MusicBrowseController::playScope() const
{
    MusicQuery scope = queryFor(Section::Songs);
    scope.letter.clear();
    if (m_section != Section::Songs) {
        scope.sortKey = QStringLiteral("album");
        scope.descending = false;
    }
    return scope;
}

bool MusicBrowseController::narrowed(Section section) const
{
    const MusicQuery query = queryFor(section);
    return query.hasFilters() || !query.letter.isEmpty();
}

MusicBrowseController::Snapshot MusicBrowseController::snapshot() const
{
    Snapshot snapshot;
    for (int i = 0; i < kSectionCount; ++i) {
        snapshot.queries[i] = queryFor(static_cast<Section>(i));
        snapshot.bases[i] = baseFor(static_cast<Section>(i));
    }
    return snapshot;
}

// Every mutation ends here: sections whose effective query moved are
// invalidated, the visible one refetches, and the readout is re-derived.
void MusicBrowseController::commit(const Snapshot &before)
{
    for (int i = 0; i < kSectionCount; ++i) {
        const auto section = static_cast<Section>(i);
        SectionState &state = m_sections[i];
        if (baseFor(section) != before.bases[i]) {
            state.unfilteredCount = -1;
            ++state.countGeneration;
            state.countPending = false;
        }
        if (queryFor(section) != before.queries[i])
            invalidate(section);
    }
    emit queryChanged();
    emit genreOptionsChanged(); // `selected` follows the query
    ensureVisible();
    ensureCounts();
    emit countsChanged();
}

bool MusicBrowseController::moveTo(const QString &sectionKey)
{
    const auto parsed = parseSection(sectionKey);
    if (!parsed || *parsed == m_section)
        return false;
    m_section = *parsed;
    return true;
}

void MusicBrowseController::resetQuery(const QString &libraryId)
{
    m_query = MusicQuery{};
    m_query.libraryId = libraryId;
    for (int i = 0; i < kSectionCount; ++i) {
        SectionState fresh;
        const QList<SortKey> keys = MusicQueryTranslator::sortKeysFor(static_cast<Section>(i));
        if (!keys.isEmpty()) {
            fresh.sortKey = keys.first().key;
            fresh.descending = keys.first().defaultDescending;
        }
        fresh.countGeneration = m_sections[i].countGeneration + 1;
        m_sections[i] = fresh;
    }
}

void MusicBrowseController::resetForLibrary(const QString &libraryId)
{
    ++m_epoch;
    resetQuery(libraryId);
    for (int i = 0; i < kSectionCount; ++i) {
        m_lanes[i]->reset();
        clearModel(static_cast<Section>(i));
    }
    m_genreOptions.clear();
    m_genreNames.clear();
    m_genreOptionsLoaded = false;
    m_genreOptionsLoading = false;
    m_genreOptionsFailed = false;
    emit genreOptionsChanged();
    loadGenreOptions();
}

void MusicBrowseController::applyRouteState(const QString &state)
{
    const QJsonDocument document = QJsonDocument::fromJson(state.toUtf8());
    if (!document.isObject() || document.object().value(QStringLiteral("v")).toInt() != 1)
        return;
    const QJsonObject root = document.object();

    QStringList genres;
    for (const QJsonValue &value : root.value(QStringLiteral("g")).toArray()) {
        const QString id = value.toString();
        if (!id.isEmpty() && !genres.contains(id))
            genres.append(id);
    }
    m_query.genreIds = genres;
    const int decade = root.value(QStringLiteral("d")).toInt(kDecadeAny);
    m_query.decade = validDecade(decade) ? decade : kDecadeAny;
    const auto format = parseFormat(root.value(QStringLiteral("f")).toString());
    m_query.format = format && formatSupported(*format) ? *format : FormatFilter::Any;
    m_query.favouritesOnly = root.value(QStringLiteral("fav")).toBool(false);
    m_query.unplayedOnly = root.value(QStringLiteral("un")).toBool(false);
    m_query.artistMode = root.value(QStringLiteral("am")).toString() == QLatin1String("everyone")
                             ? ArtistMode::Everyone
                             : ArtistMode::AlbumArtists;

    const QJsonArray sorts = root.value(QStringLiteral("s")).toArray();
    for (int i = 0; i < kSectionCount && i < sorts.size(); ++i) {
        const auto section = static_cast<Section>(i);
        const QJsonObject entry = sorts.at(i).toObject();
        SectionState &target = m_sections[i];
        if (const auto sort = sortFor(section, entry.value(QStringLiteral("k")).toString())) {
            target.sortKey = sort->key;
            target.descending = sort->key != kRandom
                                && entry.value(QStringLiteral("d")).toBool(sort->defaultDescending);
        }
        const QString letter = entry.value(QStringLiteral("l")).toString();
        target.letter = takesFilters(section) && target.sortKey == kName && letterList().contains(letter)
                            ? letter
                            : QString();
    }
}

// ── Mutations ───────────────────────────────────────────────────────────────

// Section signals go out after commit(): by then the section's lane has
// already begun (or kept) its fetch, so a view built on sectionChanged never
// sees a stale model that is about to be cleared.
void MusicBrowseController::open(const QString &libraryId, const QString &section)
{
    const Snapshot before = snapshot();
    const bool libraryMoved = libraryId != m_query.libraryId;
    if (libraryMoved)
        resetForLibrary(libraryId);
    const bool sectionMoved = moveTo(section);
    commit(before);
    if (libraryMoved)
        emit libraryChanged();
    if (libraryMoved || sectionMoved)
        emit sectionChanged();
}

void MusicBrowseController::restore(const QString &libraryId, const QString &section, const QString &state)
{
    const Snapshot before = snapshot();
    const bool libraryMoved = libraryId != m_query.libraryId;
    if (libraryMoved)
        resetForLibrary(libraryId);
    const bool sectionMoved = moveTo(section);
    applyRouteState(state);
    commit(before);
    if (libraryMoved)
        emit libraryChanged();
    if (libraryMoved || sectionMoved)
        emit sectionChanged();
}

void MusicBrowseController::setSection(const QString &section)
{
    if (!moveTo(section))
        return;
    ensureVisible();
    ensureCounts();
    emit sectionChanged();
    emit queryChanged();
    emit genreOptionsChanged();
    emit countsChanged();
}

void MusicBrowseController::setSortKey(const QString &key)
{
    const auto sort = sortFor(m_section, key);
    SectionState &state = stateOf(m_section);
    if (!sort || state.sortKey == key)
        return;
    const Snapshot before = snapshot();
    state.sortKey = sort->key;
    state.descending = sort->defaultDescending;
    if (key != kName)
        state.letter.clear();
    commit(before);
}

void MusicBrowseController::setSortDescending(bool descending)
{
    SectionState &state = stateOf(m_section);
    if (state.sortKey == kRandom || state.descending == descending)
        return;
    const Snapshot before = snapshot();
    state.descending = descending;
    commit(before);
}

void MusicBrowseController::setLetter(const QString &letter)
{
    SectionState &state = stateOf(m_section);
    if (!letter.isEmpty() && (!letterStripVisible() || !letterList().contains(letter)))
        return;
    if (state.letter == letter)
        return;
    const Snapshot before = snapshot();
    state.letter = letter;
    commit(before);
}

void MusicBrowseController::toggleLetter(const QString &letter)
{
    setLetter(letter == stateOf(m_section).letter ? QString() : letter);
}

bool MusicBrowseController::jumpLetter(int step)
{
    if (!letterStripVisible() || step == 0)
        return false;
    const QStringList &all = letterList();
    const int last = static_cast<int>(all.size()) - 1;
    const int current = all.indexOf(stateOf(m_section).letter);
    // From no letter, forward starts at A and back at Z; then it clamps, and a
    // clamped step is still handled so the trigger never falls through to paging.
    const int next = current < 0 ? (step > 0 ? 1 : last) : std::clamp(current + step, 0, last);
    setLetter(all.at(next));
    return true;
}

QString MusicBrowseController::cycleSection(int step)
{
    const QStringList &keys = cycleKeys();
    const int count = static_cast<int>(keys.size());
    const int current = keys.indexOf(section());
    const QString key = keys.at(((current + step) % count + count) % count);
    if (key != kHome)
        setSection(key);
    return key;
}

void MusicBrowseController::setGenres(const QStringList &genreIds)
{
    QStringList unique;
    for (const QString &id : genreIds) {
        if (!id.isEmpty() && !unique.contains(id))
            unique.append(id);
    }
    if (unique == m_query.genreIds)
        return;
    const Snapshot before = snapshot();
    m_query.genreIds = unique;
    commit(before);
}

void MusicBrowseController::toggleGenre(const QString &genreId)
{
    QStringList ids = m_query.genreIds;
    if (!ids.removeAll(genreId))
        ids.append(genreId);
    setGenres(ids);
}

void MusicBrowseController::clearGenres() { setGenres({}); }

void MusicBrowseController::setDecade(int decade)
{
    if (!validDecade(decade) || decade == m_query.decade)
        return;
    const Snapshot before = snapshot();
    m_query.decade = decade;
    commit(before);
}

void MusicBrowseController::setFormat(const QString &key)
{
    const auto format = parseFormat(key);
    if (!format || !formatSupported(*format) || *format == m_query.format)
        return;
    const Snapshot before = snapshot();
    m_query.format = *format;
    commit(before);
}

void MusicBrowseController::setFavouritesOnly(bool on)
{
    if (on == m_query.favouritesOnly)
        return;
    const Snapshot before = snapshot();
    m_query.favouritesOnly = on;
    commit(before);
}

void MusicBrowseController::setUnplayedOnly(bool on)
{
    if (on == m_query.unplayedOnly)
        return;
    const Snapshot before = snapshot();
    m_query.unplayedOnly = on;
    commit(before);
}

void MusicBrowseController::setArtistMode(const QString &mode)
{
    ArtistMode parsed;
    if (mode == QLatin1String("everyone"))
        parsed = ArtistMode::Everyone;
    else if (mode == QLatin1String("albumArtists"))
        parsed = ArtistMode::AlbumArtists;
    else
        return;
    if (parsed == m_query.artistMode)
        return;
    const Snapshot before = snapshot();
    m_query.artistMode = parsed;
    commit(before);
}

void MusicBrowseController::clearFilters()
{
    const Snapshot before = snapshot();
    m_query.genreIds.clear();
    m_query.decade = kDecadeAny;
    m_query.format = FormatFilter::Any;
    m_query.favouritesOnly = false;
    m_query.unplayedOnly = false;
    commit(before);
}

void MusicBrowseController::openGenre(const QString &genreId, const QString &genreName)
{
    if (genreId.isEmpty())
        return;
    const Snapshot before = snapshot();
    if (!genreName.isEmpty())
        m_genreNames.insert(genreId, genreName);
    // "Show me this genre": nothing else may hide part of it.
    m_query.genreIds = {genreId};
    m_query.decade = kDecadeAny;
    m_query.format = FormatFilter::Any;
    m_query.favouritesOnly = false;
    m_query.unplayedOnly = false;
    stateOf(Section::Albums).letter.clear();
    const bool sectionMoved = m_section != Section::Albums;
    m_section = Section::Albums;
    commit(before);
    if (sectionMoved)
        emit sectionChanged();
}

void MusicBrowseController::playFiltered()
{
    if (!m_query.libraryId.isEmpty())
        m_playback->playQuery(playScope(), scopeLabel());
}

void MusicBrowseController::shuffleFiltered()
{
    if (!m_query.libraryId.isEmpty())
        m_playback->shuffleQuery(playScope(), scopeLabel());
}

void MusicBrowseController::notePlaylistsMutated()
{
    invalidate(Section::Playlists);
    ensureVisible();
    emit countsChanged(); // invalidate() dropped the record count with the rows
}

void MusicBrowseController::resetSessionState()
{
    resetForLibrary(QString());
    m_section = Section::Albums;
    ++m_collectGeneration;
    emit libraryChanged();
    emit sectionChanged();
    emit queryChanged();
    emit countsChanged();
}

// ── Fetching ────────────────────────────────────────────────────────────────

void MusicBrowseController::invalidate(Section section)
{
    SectionState &state = stateOf(section);
    state.stale = true;
    state.resultCount = -1;
    // reset() bumps the generation, so a page still in flight for the old
    // query lands on a dead generation and is dropped.
    m_lanes[slot(section)]->reset();
    clearModel(section);
}

void MusicBrowseController::clearModel(Section section)
{
    switch (section) {
    case Section::Albums: m_albums->clear(); break;
    case Section::Artists: m_artists->clear(); break;
    case Section::Songs: m_songs->clear(); break;
    case Section::Genres: m_genres->clear(); break;
    case Section::Playlists: m_playlists->clear(); break;
    }
}

void MusicBrowseController::ensureVisible()
{
    if (!m_query.libraryId.isEmpty() && stateOf(m_section).stale)
        fetch(m_section, 0);
}

void MusicBrowseController::loadMore()
{
    const int i = slot(m_section);
    if (m_query.libraryId.isEmpty() || m_lanes[i]->loading() || !m_models[i]->canLoadMore())
        return;
    fetch(m_section, m_models[i]->count());
}

void MusicBrowseController::retry(Section section)
{
    const MusicModelBase *model = m_models[slot(section)];
    if (model->count() == 0)
        fetch(section, 0);
    else if (model->canLoadMore())
        fetch(section, model->count());
}

void MusicBrowseController::fetch(Section section, int startIndex)
{
    if (m_query.libraryId.isEmpty())
        return;
    stateOf(section).stale = false;
    const MusicQuery query = queryFor(section);
    // Begin before asking: a cached repository future can resolve synchronously.
    const quint64 generation = m_lanes[slot(section)]->begin();
    const quint64 epoch = m_epoch;
    switch (section) {
    case Section::Albums:
        m_repository->browseAlbums(query, startIndex, kPageSize)
            .then(this, [this, generation, epoch, query, startIndex](Result<Page<Album>> result) {
                acceptPage(Section::Albums, generation, epoch, query, startIndex, result, m_albums);
            });
        break;
    case Section::Artists:
        m_repository->browseArtists(query, startIndex, kPageSize)
            .then(this, [this, generation, epoch, query, startIndex](Result<Page<Artist>> result) {
                acceptPage(Section::Artists, generation, epoch, query, startIndex, result, m_artists);
            });
        break;
    case Section::Songs:
        m_repository->browseTracks(query, startIndex, kPageSize)
            .then(this, [this, generation, epoch, query, startIndex](Result<Page<Track>> result) {
                acceptPage(Section::Songs, generation, epoch, query, startIndex, result, m_songs);
            });
        break;
    case Section::Genres:
        fetchGenres(generation, epoch, query, startIndex);
        break;
    case Section::Playlists:
        m_repository->browsePlaylists(query, startIndex, kPageSize)
            .then(this, [this, generation, epoch, query, startIndex](Result<Page<Playlist>> result) {
                acceptPage(Section::Playlists, generation, epoch, query, startIndex, result, m_playlists);
            });
        break;
    }
}

template<class T, class Model>
void MusicBrowseController::acceptPage(Section section, quint64 generation, quint64 epoch,
                                       const MusicQuery &query, int startIndex,
                                       const Result<Page<T>> &result, Model *model)
{
    MusicLane *lane = m_lanes[slot(section)];
    if (!lane->isCurrent(generation))
        return;
    if (epoch != m_epoch || query != queryFor(section) || startIndex != model->count()) {
        // A hidden section whose query moved while it was loading: settle the
        // lane and keep nothing. It refetches when it is shown.
        lane->succeed(generation);
        return;
    }
    if (!result.ok()) {
        lane->fail(generation, result.error);
        return;
    }
    const Page<T> &page = result.value;
    const int shown = startIndex + static_cast<int>(page.items.size());
    // Random is one sample, never paged: its model total is what arrived.
    const int total = query.sortKey == kRandom ? shown : page.totalRecordCount;
    if (startIndex == 0)
        model->setItems(page.items, total);
    else
        model->appendItems(page.items, total);

    SectionState &state = stateOf(section);
    state.resultCount = page.totalRecordCount;
    if (!narrowed(section))
        state.unfilteredCount = page.totalRecordCount;
    lane->succeed(generation);
    ensureCounts();
    emit countsChanged();
}

void MusicBrowseController::fetchGenres(quint64 generation, quint64 epoch, const MusicQuery &query, int startIndex)
{
    m_repository->allGenres(query.libraryId)
        .then(this, [this, generation, epoch, query, startIndex](Result<QList<GenreBin>> all) {
            MusicLane *lane = m_lanes[slot(Section::Genres)];
            if (!lane->isCurrent(generation))
                return;
            if (epoch != m_epoch || query != queryFor(Section::Genres) || startIndex != m_genres->count()) {
                lane->succeed(generation);
                return;
            }
            if (!all.ok()) {
                lane->fail(generation, all.error);
                return;
            }
            rememberGenreNames(all.value);
            const QList<GenreBin> sorted = sortGenres(all.value, query.sortKey, query.descending);
            const int total = static_cast<int>(sorted.size());
            // coverGenres never fails from a cover-sample error: a bin whose
            // sample failed is drawn bare. It can still resolve
            // failure("request canceled") on its own epoch guard (T10/P3-R1)
            // when the session identity changes mid-flight — that is dropped
            // exactly like any other superseded reply, never shown as an error.
            m_repository->coverGenres(query.libraryId, sorted.mid(startIndex, kGenrePageSize))
                .then(this, [this, generation, epoch, query, startIndex, total](Result<QList<GenreBin>> covered) {
                    MusicLane *lane = m_lanes[slot(Section::Genres)];
                    if (!lane->isCurrent(generation))
                        return;
                    if (!covered.ok()) {
                        lane->succeed(generation);
                        return;
                    }
                    Page<GenreBin> page;
                    page.items = covered.value;
                    page.totalRecordCount = total;
                    page.startIndex = startIndex;
                    acceptPage(Section::Genres, generation, epoch, query, startIndex,
                               Result<Page<GenreBin>>::success(page), m_genres);
                });
        });
}

// The readout's left half when the view opened already narrowed (a restored
// route or openGenre): one Limit=1 request for the unnarrowed total.
void MusicBrowseController::ensureCounts()
{
    const Section section = m_section;
    SectionState &state = stateOf(section);
    if (m_query.libraryId.isEmpty() || !narrowed(section) || state.unfilteredCount >= 0 || state.countPending)
        return;
    const quint64 countGeneration = state.countGeneration;
    const quint64 epoch = m_epoch;
    const MusicQuery base = baseFor(section);
    auto accept = [this, section, countGeneration, epoch](const QString &error, int total) {
        acceptCount(section, countGeneration, epoch, error, total);
    };
    switch (section) {
    case Section::Albums:
        state.countPending = true;
        m_repository->browseAlbums(base, 0, 1).then(this, [accept](Result<Page<Album>> r) {
            accept(r.error, r.value.totalRecordCount);
        });
        break;
    case Section::Artists:
        state.countPending = true;
        m_repository->browseArtists(base, 0, 1).then(this, [accept](Result<Page<Artist>> r) {
            accept(r.error, r.value.totalRecordCount);
        });
        break;
    case Section::Songs:
        state.countPending = true;
        m_repository->browseTracks(base, 0, 1).then(this, [accept](Result<Page<Track>> r) {
            accept(r.error, r.value.totalRecordCount);
        });
        break;
    case Section::Genres:
    case Section::Playlists:
        break; // never narrowed
    }
}

void MusicBrowseController::acceptCount(Section section, quint64 countGeneration, quint64 epoch,
                                        const QString &error, int total)
{
    SectionState &state = stateOf(section);
    if (epoch != m_epoch || countGeneration != state.countGeneration)
        return;
    state.countPending = false;
    if (!error.isEmpty()) {
        // The readout degrades to the filtered count alone.
        qCWarning(logApp) << "music: unfiltered count failed" << error;
        return;
    }
    state.unfilteredCount = total;
    emit countsChanged();
}

// ── Genre options, album tracks ─────────────────────────────────────────────

void MusicBrowseController::rememberGenreNames(const QList<GenreBin> &genres)
{
    for (const GenreBin &genre : genres)
        m_genreNames.insert(genre.id, genre.name);
}

void MusicBrowseController::loadGenreOptions()
{
    if (m_query.libraryId.isEmpty() || m_genreOptionsLoading)
        return;
    m_genreOptionsLoading = true;
    m_genreOptionsFailed = false;
    emit genreOptionsChanged();
    const quint64 epoch = m_epoch;
    m_repository->allGenres(m_query.libraryId).then(this, [this, epoch](Result<QList<GenreBin>> result) {
        if (epoch != m_epoch)
            return;
        m_genreOptionsLoading = false;
        if (!result.ok()) {
            qCWarning(logApp) << "music: genre options failed" << result.error;
            m_genreOptionsFailed = true;
            emit genreOptionsChanged();
            return;
        }
        m_genreOptions = result.value;
        m_genreOptionsLoaded = true;
        rememberGenreNames(result.value);
        emit genreOptionsChanged();
        emit queryChanged(); // the pill and scope texts name genres
    });
}

void MusicBrowseController::ensureGenreOptions()
{
    if (!m_genreOptionsLoaded)
        loadGenreOptions();
}

void MusicBrowseController::collectAlbumTracks(const QString &albumId, const QString &name)
{
    if (albumId.isEmpty())
        return;
    const quint64 generation = ++m_collectGeneration;
    const quint64 epoch = m_epoch;
    m_repository->albumTracks(albumId).then(this, [this, generation, epoch, name](Result<QList<Track>> result) {
        if (generation != m_collectGeneration || epoch != m_epoch)
            return;
        if (!result.ok()) {
            emit actionFailed(tr("Couldn't read that album: %1").arg(result.error));
            return;
        }
        QStringList ids;
        for (const Track &track : std::as_const(result.value)) {
            if (!track.id.isEmpty())
                ids.append(track.id);
        }
        if (ids.isEmpty()) {
            emit actionFailed(tr("That album has no tracks to add."));
            return;
        }
        emit albumTracksCollected(name, ids);
    });
}

} // namespace strmqt::music
