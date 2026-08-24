#include "MpvVideoItem.h"

#include "MpvPlayer.h"
#include "core/Log.h"

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QQuickWindow>

#include <atomic>

namespace strmqt {

namespace {

void *glProcAddress(void *, const char *name)
{
    QOpenGLContext *context = QOpenGLContext::currentContext();
    if (!context)
        return nullptr;
    return reinterpret_cast<void *>(context->getProcAddress(name));
}

} // namespace

// Lives on the GUI thread and owned jointly by the item and its renderer.
//
// mpv's update callback fires from whichever thread produced the frame, and the
// only thing that actually schedules a new pass over the FBO is marking the
// ITEM dirty: QQuickWindow::update() alone raises a frame, but the framebuffer
// node skips rendering unless updatePaintNode() ran, so the video freezes on
// its last drawn frame. Posting through this bridge keeps that item update on
// the GUI thread without ever dereferencing a QQuickItem from the render side:
// the bridge outlives the render context (both holders keep it alive), Qt drops
// any queued call if it is destroyed first, and the QPointer is only read on
// the thread that can destroy the item.
class MpvUpdateBridge : public QObject
{
public:
    explicit MpvUpdateBridge(MpvVideoItem *item) : m_item(item) {}

    // Any thread. Coalesces: one queued redraw request is as good as ten.
    void requestUpdate()
    {
        if (m_queued.exchange(true, std::memory_order_acq_rel))
            return;
        QMetaObject::invokeMethod(this, [this] { deliverUpdate(); }, Qt::QueuedConnection);
    }

private:
    void deliverUpdate()
    {
        m_queued.store(false, std::memory_order_release);
        if (m_item)
            m_item->update();
    }

    QPointer<MpvVideoItem> m_item;
    std::atomic<bool> m_queued = false;
};

// Lives on the render thread. Creates the mpv render context lazily on first
// render (a current GL context is guaranteed there) and frees it with the item.
class MpvRenderer : public QQuickFramebufferObject::Renderer
{
public:
    explicit MpvRenderer(std::shared_ptr<MpvUpdateBridge> bridge) : m_bridge(std::move(bridge)) {}

    ~MpvRenderer() override
    {
        m_window.store(nullptr, std::memory_order_release);
        if (m_context) {
            mpv_render_context_set_update_callback(m_context, nullptr, nullptr);
            mpv_render_context_free(m_context);
        }
    }

    void synchronize(QQuickFramebufferObject *item) override
    {
        // Qt blocks the GUI thread while synchronize() runs. This is the one
        // supported boundary for copying QQuickItem/QObject state to the render
        // thread; render() never dereferences either object.
        auto *videoItem = static_cast<MpvVideoItem *>(item);
        mpv_handle *handle = videoItem->player() ? videoItem->player()->handle() : nullptr;
        m_window.store(videoItem->window(), std::memory_order_release);
        if (handle == m_handle)
            return;
        if (m_context) {
            mpv_render_context_set_update_callback(m_context, nullptr, nullptr);
            mpv_render_context_free(m_context);
            m_context = nullptr;
        }
        m_handle = handle;
    }

    void render() override
    {
        ensureContext();
        if (!m_context)
            return;

        QOpenGLFramebufferObject *fbo = framebufferObject();
        mpv_opengl_fbo mpvFbo{static_cast<int>(fbo->handle()), fbo->width(), fbo->height(), 0};
        int flipY = 0;

        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_OPENGL_FBO, &mpvFbo},
            {MPV_RENDER_PARAM_FLIP_Y, &flipY},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };
        // mpv issues raw GL alongside the RHI — fence it off from Qt's own state.
        QQuickWindow *window = m_window.load(std::memory_order_acquire);
        if (!window)
            return;
        window->beginExternalCommands();
        mpv_render_context_render(m_context, params);
        window->endExternalCommands();
    }

private:
    static void onUpdate(void *ctx)
    {
        // Called from an arbitrary mpv thread. The context is the bridge, not
        // the renderer: the renderer holds a reference to it for exactly as
        // long as the render context that can raise this callback exists.
        static_cast<MpvUpdateBridge *>(ctx)->requestUpdate();
    }

    void ensureContext()
    {
        if (m_context || !m_handle)
            return;

        mpv_opengl_init_params glParams{glProcAddress, nullptr};
        // Simple-control mode: mpv drives frame timing itself and the update
        // callback just requests redraws. Advanced control requires a
        // mpv_render_context_update() protocol we don't need for v1.
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
            {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glParams},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };

        const int rc = mpv_render_context_create(&m_context, m_handle, params);
        if (rc < 0) {
            qCCritical(logPlayback) << "mpv render context failed:" << mpv_error_string(rc);
            m_context = nullptr;
            return;
        }
        mpv_render_context_set_update_callback(m_context, onUpdate, m_bridge.get());
    }

    std::shared_ptr<MpvUpdateBridge> m_bridge;
    mpv_handle *m_handle = nullptr;
    std::atomic<QQuickWindow *> m_window = nullptr;
    mpv_render_context *m_context = nullptr;
};

MpvVideoItem::MpvVideoItem(QQuickItem *parent)
    : QQuickFramebufferObject(parent),
      // deleteLater(), because the renderer may drop the last reference from
      // the render thread and a QObject must be destroyed on its own.
      m_bridge(new MpvUpdateBridge(this),
               [](MpvUpdateBridge *bridge) { bridge->deleteLater(); })
{
    // mpv renders top-down into the FBO; no mirroring needed (flipY=0 in render()).
    setTextureFollowsItemSize(true);
}

QQuickFramebufferObject::Renderer *MpvVideoItem::createRenderer() const
{
    return new MpvRenderer(m_bridge);
}

QObject *MpvVideoItem::playerObject() const
{
    return m_player;
}

MpvPlayer *MpvVideoItem::player() const
{
    return m_player.data();
}

void MpvVideoItem::requestRedrawForTests()
{
    m_bridge->requestUpdate();
}

void MpvVideoItem::setPlayerObject(QObject *player)
{
    auto *mpvPlayer = qobject_cast<MpvPlayer *>(player);
    if (mpvPlayer == m_player)
        return;
    m_player = mpvPlayer;
    emit playerChanged();
    update();
}

} // namespace strmqt
