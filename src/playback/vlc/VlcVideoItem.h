#pragma once

#include <QQuickPaintedItem>
#include <QtQmlIntegration/qqmlintegration.h>

namespace strmqt {

class VlcPlayer;

// Software video plane for the VLC fallback engine: paints the latest vmem
// frame, letterboxed. Correctness-first; the mpv GL path is the fast one.
class VlcVideoItem : public QQuickPaintedItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(VlcVideo)
    Q_PROPERTY(QObject *player READ playerObject WRITE setPlayerObject NOTIFY playerChanged)

public:
    explicit VlcVideoItem(QQuickItem *parent = nullptr);

    void paint(QPainter *painter) override;

    QObject *playerObject() const;
    void setPlayerObject(QObject *player);

signals:
    void playerChanged();

private:
    VlcPlayer *m_player = nullptr;
};

} // namespace strmqt
