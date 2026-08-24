#include "VlcVideoItem.h"

#include "VlcPlayer.h"

#include <QPainter>

namespace strmqt {

VlcVideoItem::VlcVideoItem(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setRenderTarget(QQuickPaintedItem::FramebufferObject);
}

void VlcVideoItem::paint(QPainter *painter)
{
    painter->fillRect(boundingRect(), Qt::black);
    if (!m_player)
        return;
    const QImage frame = m_player->currentFrame();
    if (frame.isNull())
        return;

    QRectF target = boundingRect();
    const QSizeF scaled = QSizeF(frame.size()).scaled(target.size(), Qt::KeepAspectRatio);
    target = QRectF(target.center() - QPointF(scaled.width() / 2, scaled.height() / 2), scaled);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->drawImage(target, frame);
}

QObject *VlcVideoItem::playerObject() const
{
    return m_player;
}

void VlcVideoItem::setPlayerObject(QObject *player)
{
    auto *vlcPlayer = qobject_cast<VlcPlayer *>(player);
    if (vlcPlayer == m_player)
        return;
    if (m_player)
        disconnect(m_player, nullptr, this, nullptr);
    m_player = vlcPlayer;
    if (m_player)
        // VlcPlayer already coalesces decoder callbacks onto its GUI thread.
        // A second queued hop would reopen the very backlog that gate closes.
        connect(m_player, &VlcPlayer::frameReady, this, [this] { update(); });
    emit playerChanged();
    update();
}

} // namespace strmqt
