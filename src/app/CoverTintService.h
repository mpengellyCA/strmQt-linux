#pragma once

#include <QColor>
#include <QHash>
#include <QObject>
#include <QQueue>
#include <QString>

class QImage;

namespace strmqt {

class EmbyImageFetcher;

// The QML face of the cover wash (MUSIC.md §4, Rule 2). Caches one clamped
// tint per image id and hands it to whatever wants to light a room with it.
//
// ── Why this costs no network ────────────────────────────────────────────────
// Nothing here fetches. EmbyImageFetcher already decodes a QImage for every
// cover the interface draws, and the now-playing hero, the docked bar and the
// album page header are all drawing the very cover their wash wants. So the
// fetcher publishes each decode and this listens: the tint is computed from
// pixels that were downloaded anyway. A cover that is never drawn is never
// sampled, which is correct — nothing is waiting on its colour.
//
// ── Why QML reads it through `revision` ──────────────────────────────────────
// A tint arrives after the binding that wants it has already run: the panel
// asks, misses, and the cover finishes decoding a moment later. `revision`
// bumps on every new entry, so a binding that touches it re-evaluates when one
// lands. CoverWash.qml is the one file that does that, so the idiom lives in
// exactly one place.
class CoverTintService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(qreal maxWashOpacity READ maxWashOpacity CONSTANT)

public:
    explicit CoverTintService(EmbyImageFetcher *fetcher, QObject *parent = nullptr);

    // `source` is either an image://emby/{itemId}/{imageType}/{tag} URL as the
    // models publish it, or the bare "{itemId}/{imageType}/{tag}" id.
    //
    // Returns an opaque colour inside the clamp box, or TRANSPARENT when there
    // is no usable tint — because the cover has not been decoded yet, or
    // because it has no colour that can meet the clamp. QML renders transparent
    // as the Theme.surfaceColor fallback; the two cases are deliberately not
    // distinguished, since both mean "do not draw a sampled wash".
    Q_INVOKABLE QColor tintFor(const QString &source) const;

    int revision() const { return m_revision; }
    static qreal maxWashOpacity();

signals:
    void revisionChanged();

private:
    void onImageDecoded(const QString &id, const QImage &image);
    void remember(const QString &id, const QColor &tint);

    // Roughly a library page's worth of covers. Bounded because every image the
    // app draws passes through here, and an unbounded map of every cover a long
    // session scrolled past is a leak with a colour in it.
    static constexpr int kMaxEntries = 512;

    // An entry with an invalid colour is a REMEMBERED failure: the cover was
    // sampled and had no tint that could meet the clamp. Keeping it stops the
    // same greyscale sleeve being re-sampled on every redraw.
    QHash<QString, QColor> m_tints;
    QQueue<QString> m_order;
    int m_revision = 0;
};

} // namespace strmqt
