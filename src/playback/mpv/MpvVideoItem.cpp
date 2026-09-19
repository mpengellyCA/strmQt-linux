#include "MpvVideoItem.h"

#include "MpvPlayer.h"
#include "core/Log.h"

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QThread>

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
class MpvRenderer;

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

    // GUI thread only. Set while this item's renderer lives on the GUI thread
    // too, which is the basic render loop: the one case where the item may
    // call into its renderer directly (see MpvVideoItem::onRenderHandleChanged).
    MpvRenderer *sameThreadRenderer = nullptr;

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
    explicit MpvRenderer(std::shared_ptr<MpvUpdateBridge> bridge) : m_bridge(std::move(bridge))
    {
        if (QThread::currentThread() == m_bridge->thread())
            m_bridge->sameThreadRenderer = this;
    }

    ~MpvRenderer() override
    {
        if (m_bridge->sameThreadRenderer == this)
            m_bridge->sameThreadRenderer = nullptr;
        freeContext();
        letGo();
        m_window.store(nullptr, std::memory_order_release);
    }

    // Basic render loop only, on the GUI (= render) thread, between frames and
    // with the window's GL context made current by the caller. No frame is
    // being recorded, so there are no external commands to fence.
    void releaseBetweenFrames()
    {
        freeContext(/*insideFrame=*/false);
        letGo();
    }

    void synchronize(QQuickFramebufferObject *item) override
    {
        // Qt blocks the GUI thread while synchronize() runs. This is the one
        // supported boundary for copying QQuickItem/QObject state to the render
        // thread; render() never dereferences either object.
        auto *videoItem = static_cast<MpvVideoItem *>(item);
        MpvPlayer *player = videoItem->player();
        mpv_handle *handle = player ? player->handle() : nullptr;
        m_window.store(videoItem->window(), std::memory_order_release);
        if (handle == m_handle)
            return;
        freeContext();
        letGo();
        // Registered before anything can create a context on it, and while the
        // GUI thread is blocked, so the player can never see zero holders while
        // this renderer still has its handle.
        if (handle) {
            m_link = player->renderLink();
            m_link->acquire();
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
    void freeContext(bool insideFrame = true)
    {
        if (!m_context)
            return;
        QQuickWindow *win = insideFrame ? m_window.load(std::memory_order_acquire) : nullptr;
        if (win)
            win->beginExternalCommands();
        mpv_render_context_set_update_callback(m_context, nullptr, nullptr);
        mpv_render_context_free(m_context);
        if (win)
            win->endExternalCommands();
        m_context = nullptr;
    }

    // After freeContext(): the player may destroy the core as soon as the last
    // holder lets go, and mpv requires the context to be gone by then.
    void letGo()
    {
        if (m_link)
            m_link->release();
        m_link.reset();
        m_handle = nullptr;
    }

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
    std::shared_ptr<mpvdetail::RenderLink> m_link;
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
    if (m_player)
        disconnect(m_player, nullptr, this, nullptr);
    m_player = mpvPlayer;
    if (m_player)
        connect(m_player, &MpvPlayer::renderHandleChanged, this,
                &MpvVideoItem::onRenderHandleChanged);
    emit playerChanged();
    update();
}

void MpvVideoItem::onRenderHandleChanged()
{
    update();
    // A release is waiting for this item's renderer, which lets go at its next
    // synchronize(). An unexposed window (hidden, minimised) never gets one,
    // and would keep the whole core alive until it is shown again.
    QQuickWindow *win = window();
    if (!win || win->isExposed() || !m_player || m_player->handle() || !m_player->hasCore()
        || m_player->renderLink()->holders() == 0)
        return;

    if (MpvRenderer *renderer = m_bridge->sameThreadRenderer) {
        // Basic render loop: this thread is the render thread, and its
        // releaseResources() only trims caches, whatever the persistence
        // flags say, so it would never drop the renderer. Nothing renders
        // between two calls on this thread, so the renderer can free its mpv
        // context right here, once the window's own GL context is current. A
        // pbuffer-backed offscreen surface stands in for the unexposed window.
        // doneCurrent() afterwards makes the scene graph make its context
        // current again at the start of its next frame, rather than trusting
        // a binding it did not make; a context something else had current is
        // put back. If the context cannot be made current, nothing is freed:
        // the core then stays until the window is exposed again, which is safe.
        auto *context = static_cast<QOpenGLContext *>(win->rendererInterface()->getResource(
            win, QSGRendererInterface::OpenGLContextResource));
        if (!context)
            return;
        QOpenGLContext *previous = QOpenGLContext::currentContext();
        QSurface *previousSurface = previous ? previous->surface() : nullptr;
        QOffscreenSurface surface(win->screen());
        surface.setFormat(context->format());
        surface.create();
        if (!context->makeCurrent(&surface)) {
            qCWarning(logPlayback) << "could not free the video renderer of an unexposed window;"
                                   << "the mpv core stays until it is shown";
            return;
        }
        renderer->releaseBetweenFrames();
        context->doneCurrent();
        if (previous && previousSurface && previous != context)
            previous->makeCurrent(previousSurface);
        return;
    }

    // Threaded render loop: dropping the window's scene graph deletes every
    // node on the render thread with its GL context current, and this item's
    // node takes the renderer, and so the mpv render context, with it. Qt
    // wipes the scene graph only when it is not persistent, and additionally
    // destroys the graphics context (the QRhi and its GL context) unless THAT
    // is persistent, so both flags are forced for the one call and restored:
    // the scene graph goes, the graphics context stays.
    //
    // The wipe drops every node, texture and glyph cache in the window, not
    // only the video's, so the next expose rebuilds them all: a one-off hitch,
    // at most once per pending release while the window is unexposed.
    const bool persistentSceneGraph = win->isPersistentSceneGraph();
    const bool persistentGraphics = win->isPersistentGraphics();
    win->setPersistentGraphics(true);
    win->setPersistentSceneGraph(false);
    win->releaseResources();
    win->setPersistentSceneGraph(persistentSceneGraph);
    win->setPersistentGraphics(persistentGraphics);
}

} // namespace strmqt
