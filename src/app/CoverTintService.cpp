#include "CoverTintService.h"

#include "CoverTint.h"
#include "EmbyImageProvider.h"

#include <QImage>

namespace strmqt {

namespace {

// Models publish the provider URL; the fetcher keys by the id inside it. One
// cache, so both spellings have to arrive at the same key.
QString cacheKey(const QString &source)
{
    static const QString scheme = QStringLiteral("image://emby/");
    return source.startsWith(scheme) ? source.mid(scheme.size()) : source;
}

} // namespace

CoverTintService::CoverTintService(EmbyImageFetcher *fetcher, QObject *parent) : QObject(parent)
{
    if (fetcher == nullptr)
        return;
    m_partitionGeneration = fetcher->cachePartitionGeneration();
    connect(fetcher, &EmbyImageFetcher::imageDecoded, this, &CoverTintService::onImageDecoded);
    connect(fetcher, &EmbyImageFetcher::cachePartitionChanged, this,
            &CoverTintService::resetForPartition);
}

qreal CoverTintService::maxWashOpacity()
{
    return covertint::kMaxWashOpacity;
}

QColor CoverTintService::tintFor(const QString &source) const
{
    if (source.isEmpty())
        return QColor(Qt::transparent);
    const QString id = cacheKey(source);

    // Being asked is what marks a cover as still on screen. Recorded even on a
    // miss: the sleeve that has not decoded yet is exactly the one whose tint
    // has to survive the flood of decodes it is about to arrive in the middle
    // of.
    m_wanted = id;

    const auto entry = m_tints.constFind(id);
    if (entry == m_tints.cend())
        return QColor(Qt::transparent);
    touch(id);
    return entry->isValid() ? *entry : QColor(Qt::transparent);
}

void CoverTintService::onImageDecoded(const QString &id, const QImage &image, quint64 generation)
{
    if (id.isEmpty() || generation != m_partitionGeneration)
        return;
    if (m_tints.contains(id)) {
        // Already sampled, and being decoded again means it is back on screen.
        touch(id);
        return;
    }
    remember(id, covertint::dominantWashTint(image));
}

void CoverTintService::resetForPartition(quint64 generation)
{
    if (generation == m_partitionGeneration)
        return;
    m_partitionGeneration = generation;
    m_tints.clear();
    m_order.clear();
    m_wanted.clear();
    ++m_revision;
    emit revisionChanged();
}

// Youngest end of the queue. Const because it changes nothing anyone can see:
// the cache answers exactly the same questions afterwards, only for longer.
void CoverTintService::touch(const QString &id) const
{
    if (!m_order.isEmpty() && m_order.constLast() == id)
        return;
    m_order.removeOne(id);
    m_order.enqueue(id);
}

void CoverTintService::remember(const QString &id, const QColor &tint)
{
    m_tints.insert(id, tint);
    touch(id);
    while (m_order.size() > kMaxEntries) {
        const QString oldest = m_order.dequeue();
        // Never the cover something is currently waiting on. Ids in the queue
        // are unique, so at most one round trip is skipped and the loop still
        // ends.
        if (oldest == m_wanted) {
            m_order.enqueue(oldest);
            continue;
        }
        m_tints.remove(oldest);
    }

    // A remembered failure changes nothing on screen — the fallback was already
    // being drawn — so it does not wake every wash binding in the app.
    if (!tint.isValid())
        return;
    ++m_revision;
    emit revisionChanged();
}

} // namespace strmqt
