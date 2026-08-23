#include "MpvVideoItem.h"

#include "MpvPlayer.h"
#include "core/Log.h"

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QQuickWindow>

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
    explicit MpvRenderer(MpvVideoItem *item) : m_item(item) {}

    ~MpvRenderer() override
    {
        if (m_context)
            mpv_render_context_free(m_context);
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
        QQuickWindow *window = m_item->window();
        window->beginExternalCommands();
        mpv_render_context_render(m_context, params);
        window->endExternalCommands();
    }

private:
    static void onUpdate(void *ctx)
    {
        // Render-thread-agnostic: schedule a scene graph update on the GUI thread.
        auto *item = static_cast<MpvVideoItem *>(ctx);
        QMetaObject::invokeMethod(item, "update", Qt::QueuedConnection);
    }

    void ensureContext()
    {
        if (m_context || !m_item->player() || !m_item->player()->handle())
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

        const int rc = mpv_render_context_create(&m_context, m_item->player()->handle(), params);
        if (rc < 0) {
            qCCritical(logPlayback) << "mpv render context failed:" << mpv_error_string(rc);
            m_context = nullptr;
            return;
        }
        mpv_render_context_set_update_callback(m_context, onUpdate, m_item);
    }

    MpvVideoItem *m_item;
    mpv_render_context *m_context = nullptr;
};

MpvVideoItem::MpvVideoItem(QQuickItem *parent) : QQuickFramebufferObject(parent)
{
    // mpv renders top-down into the FBO; no mirroring needed (flipY=0 in render()).
    setTextureFollowsItemSize(true);
}

QQuickFramebufferObject::Renderer *MpvVideoItem::createRenderer() const
{
    return new MpvRenderer(const_cast<MpvVideoItem *>(this));
}

QObject *MpvVideoItem::playerObject() const
{
    return m_player;
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
