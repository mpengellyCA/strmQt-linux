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

// Lives on the render thread. Creates the mpv render context lazily on first
// render (a current GL context is guaranteed there) and frees it with the item.
class MpvRenderer : public QQuickFramebufferObject::Renderer
{
public:
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
        // QQuickWindow::update() is explicitly callable from any thread. The
        // atomic is nulled before unregistering this callback during teardown.
        auto *renderer = static_cast<MpvRenderer *>(ctx);
        if (QQuickWindow *window = renderer->m_window.load(std::memory_order_acquire))
            window->update();
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
        mpv_render_context_set_update_callback(m_context, onUpdate, this);
    }

    mpv_handle *m_handle = nullptr;
    std::atomic<QQuickWindow *> m_window = nullptr;
    mpv_render_context *m_context = nullptr;
};

MpvVideoItem::MpvVideoItem(QQuickItem *parent) : QQuickFramebufferObject(parent)
{
    // mpv renders top-down into the FBO; no mirroring needed (flipY=0 in render()).
    setTextureFollowsItemSize(true);
}

QQuickFramebufferObject::Renderer *MpvVideoItem::createRenderer() const
{
    return new MpvRenderer;
}

QObject *MpvVideoItem::playerObject() const
{
    return m_player;
}

MpvPlayer *MpvVideoItem::player() const
{
    return m_player.data();
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
