#pragma once

#include <QDateTime>
#include <QHash>
#include <QString>

#include <chrono>
#include <list>
#include <optional>

namespace strmqt::music {

// perf-fix-b (2026-09-16): the default entry cap shared by every TtlCache
// instance in MusicRepository. Named once here, rather than a literal at each
// of the eight construction sites, per
// .superpowers/sdd/2026-09-16-music-crate/perf-investigation-report.md
// ("Unbounded C++ caches").
//
// The three caches that actually grow with use (tracks per album, sleeves per
// album, covers per genre) are keyed one entry per distinct entity a session
// opens, so before this cap they grew for the life of the process. The other
// five (home:*/lib:* lists) are bounded by library count times a handful of
// page sizes and sit far below any plausible cap, so this is headroom for
// them rather than a constraint.
//
// What the cap is for, stated precisely, because the arithmetic is easy to get
// wrong in both directions: its job is to make growth BOUNDED, not to avoid
// eviction. The investigation's ~1.6 MB figure is a whole-process residual per
// cycle of `deepthenhome` — four pages pushed and Home pressed, one album and
// one artist opened — so it covers every per-entity structure the process
// keeps, not one entry's payload in one cache. An entry here is a QList of
// parsed structs (a few KB for an album's tracks, less for a cover list); the
// honest reading of the measurement is that ~200 distinct entities' worth of
// residual across ALL of these caches is on the order of 320 MB, and that is
// the ceiling this cap buys in exchange for re-fetching anything older.
//
// Eviction at 200 is therefore expected and fine, including on this owner's
// 289-genre library: browsing every genre evicts the 89 least recently seen
// and re-fetches them if revisited. Raise it only against a measurement, and
// remember that a generous cap here is paid for in resident memory, which is
// the whole complaint this change answers.
constexpr int kDefaultMaxEntries = 200;

// Per-account in-memory cache with a time-to-live and explicit staleness
// (Crate spec §3.5). Keys are "<kind>:<id>" so markStale("home:") invalidates
// one family. A negative TTL never expires (session caches).
//
// Bounded by entry count with LRU eviction (perf-fix-b, 2026-09-16): put()
// used to insert into the backing QHash with no eviction of any kind, so an
// entry past its TTL merely stopped being *returned* by get() while its full
// payload stayed resident for the life of the process — the thing that turned
// "high after deep browsing" into "high forever, and rising". Two changes fix
// that: get() now erases an expired entry instead of only reporting a miss,
// and put() evicts the least-recently-used entry once the cache holds more
// than its cap.
template<class T>
class TtlCache
{
public:
    explicit TtlCache(std::chrono::milliseconds ttl, int maxEntries = kDefaultMaxEntries)
        : m_ttl(ttl), m_maxEntries(maxEntries)
    {
    }

    std::optional<T> get(const QString &key, const QDateTime &now)
    {
        const auto it = m_entries.find(key);
        if (it == m_entries.end())
            return std::nullopt;
        if (m_ttl.count() >= 0 && it->storedAt.msecsTo(now) > m_ttl.count()) {
            // Erase eagerly: an entry past its TTL must not keep occupying
            // memory just because nothing else happened to evict it by count.
            eraseAt(it);
            return std::nullopt;
        }
        if (it->stale)
            return std::nullopt;
        touch(it);
        return it->value;
    }

    void put(const QString &key, T value, const QDateTime &now)
    {
        const auto it = m_entries.find(key);
        if (it != m_entries.end()) {
            it->value = std::move(value);
            it->storedAt = now;
            it->stale = false;
            touch(it);
            return;
        }
        m_order.push_back(key);
        m_entries.insert(key, Entry{std::move(value), now, false, std::prev(m_order.end())});
        evictIfNeeded();
    }

    void markStale(const QString &prefix = {})
    {
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
            if (prefix.isEmpty() || it.key().startsWith(prefix))
                it->stale = true;
        }
    }

    void remove(const QString &key)
    {
        const auto it = m_entries.find(key);
        if (it != m_entries.end())
            eraseAt(it);
    }

    template<class Pred>
    void removeIf(Pred pred)
    {
        for (auto it = m_entries.begin(); it != m_entries.end();) {
            if (pred(it.key(), it->value)) {
                m_order.erase(it->orderIt);
                it = m_entries.erase(it);
            } else {
                ++it;
            }
        }
    }

    void clear()
    {
        m_entries.clear();
        m_order.clear();
    }
    int size() const { return static_cast<int>(m_entries.size()); }

private:
    struct Entry
    {
        T value;
        QDateTime storedAt;
        bool stale = false;
        std::list<QString>::iterator orderIt;
    };
    using Iterator = typename QHash<QString, Entry>::iterator;

    // Marks key as most recently used by moving it to the back of m_order.
    void touch(Iterator it) { m_order.splice(m_order.end(), m_order, it->orderIt); }

    void eraseAt(Iterator it)
    {
        m_order.erase(it->orderIt);
        m_entries.erase(it);
    }

    void evictIfNeeded()
    {
        while (m_maxEntries > 0 && m_entries.size() > m_maxEntries) {
            const auto lru = m_entries.find(m_order.front());
            m_order.pop_front();
            m_entries.erase(lru);
        }
    }

    QHash<QString, Entry> m_entries;
    std::list<QString> m_order; // front = least recently used, back = most recently used
    std::chrono::milliseconds m_ttl;
    int m_maxEntries;
};

} // namespace strmqt::music
