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
    connect(fetcher, &EmbyImageFetcher::imageDecoded, this, &CoverTintService::onImageDecoded);
}

qreal CoverTintService::maxWashOpacity()
{
    return covertint::kMaxWashOpacity;
}

QColor CoverTintService::tintFor(const QString &source) const
{
    if (source.isEmpty())
        return QColor(Qt::transparent);
    const QColor tint = m_tints.value(cacheKey(source));
    return tint.isValid() ? tint : QColor(Qt::transparent);
}

void CoverTintService::onImageDecoded(const QString &id, const QImage &image)
{
    if (id.isEmpty() || m_tints.contains(id))
        return;
    remember(id, covertint::dominantWashTint(image));
}

void CoverTintService::remember(const QString &id, const QColor &tint)
{
    m_tints.insert(id, tint);
    m_order.enqueue(id);
    while (m_order.size() > kMaxEntries)
        m_tints.remove(m_order.dequeue());

    // A remembered failure changes nothing on screen — the fallback was already
    // being drawn — so it does not wake every wash binding in the app.
    if (!tint.isValid())
        return;
    ++m_revision;
    emit revisionChanged();
}

} // namespace strmqt
