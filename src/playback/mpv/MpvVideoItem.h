#pragma once

#include <QQuickFramebufferObject>
#include <QPointer>
#include <QSize>
#include <QString>
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
    // Upscaling inputs (spec 2026-10-03 §4.3–4.4), forwarded to
    // PlayerController by the QML plane. The item measures; it decides nothing.
    Q_PROPERTY(QSize outputPixelSize READ outputPixelSize NOTIFY outputPixelSizeChanged)
    Q_PROPERTY(QString gpuRenderer READ gpuRenderer NOTIFY gpuRendererChanged)

public:
    explicit MpvVideoItem(QQuickItem *parent = nullptr);

    Renderer *createRenderer() const override;

    QObject *playerObject() const;
    void setPlayerObject(QObject *player);
    MpvPlayer *player() const;

    // The video rect in device pixels: the size mpv renders into.
    QSize outputPixelSize() const { return m_outputPixelSize; }
    // GL_RENDERER of the context mpv renders with; empty until the first frame.
    QString gpuRenderer() const { return m_gpuRenderer; }

    // Test seam: raises exactly the redraw request mpv's update callback
    // raises, from whatever thread the caller is on. Reaching the real callback
    // needs a live render context and decoded frames, while the rule it proves
    // — an off-thread frame notification must dirty the ITEM, not merely the
    // window — is the one that silently freezes video when it regresses.
    void requestRedrawForTests();

signals:
    void playerChanged();
    void outputPixelSizeChanged();
    void gpuRendererChanged();
    // Twice a second while frames render: p95 and mean GPU milliseconds of
    // mpv's render call, over `samples` frames. samples < 0: no timer queries.
    void gpuTimingSampled(qreal p95Ms, qreal meanMs, int samples);

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;

private:
    friend class MpvUpdateBridge;
    void onRenderHandleChanged();
    void updateOutputPixelSize();
    void deliverGpuRenderer(const QString &renderer);

    QPointer<MpvPlayer> m_player;
    // Shared with the render thread; see MpvUpdateBridge in the .cpp for why
    // the redraw request cannot go straight to this item.
    std::shared_ptr<MpvUpdateBridge> m_bridge;
    QSize m_outputPixelSize;
    QString m_gpuRenderer;
};

} // namespace strmqt
