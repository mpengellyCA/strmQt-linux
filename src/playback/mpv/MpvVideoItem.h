#pragma once

#include <QQuickFramebufferObject>
#include <QPointer>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>

namespace strmqt {

class MpvPlayer;
class MpvUpdateBridge;

// Video plane: renders the attached MpvPlayer's output into the Qt Quick scene
// via mpv's OpenGL render API (scene graph pinned to GL, PLAN §3.2).
class MpvVideoItem : public QQuickFramebufferObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(MpvVideo)
    Q_PROPERTY(QObject *player READ playerObject WRITE setPlayerObject NOTIFY playerChanged)

public:
    explicit MpvVideoItem(QQuickItem *parent = nullptr);

    Renderer *createRenderer() const override;

    QObject *playerObject() const;
    void setPlayerObject(QObject *player);
    MpvPlayer *player() const;

    // Test seam: raises exactly the redraw request mpv's update callback
    // raises, from whatever thread the caller is on. Reaching the real callback
    // needs a live render context and decoded frames, while the rule it proves
    // — an off-thread frame notification must dirty the ITEM, not merely the
    // window — is the one that silently freezes video when it regresses.
    void requestRedrawForTests();

signals:
    void playerChanged();

private:
    QPointer<MpvPlayer> m_player;
    // Shared with the render thread; see MpvUpdateBridge in the .cpp for why
    // the redraw request cannot go straight to this item.
    std::shared_ptr<MpvUpdateBridge> m_bridge;
};

} // namespace strmqt
