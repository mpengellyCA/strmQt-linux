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
