#pragma once

#include <QtGui/qopengl.h>

#include <array>
#include <vector>

class QOpenGLContext;

namespace strmqt {

// GPU time of mpv's render call, from a pair of GL_TIMESTAMP queries around
// it (spec 2026-10-03 §4.4). Render thread only, with the context current.
//
// NEVER GL_TIME_ELAPSED. mpv brackets its own passes with that query type, so
// a second one around mpv_render_context_render() is a nested begin, which GL
// forbids: every frame then logs "after creating texture: OpenGL error
// INVALID_OPERATION" and the user shaders silently stop running (spec §2.4).
// Timestamps do not nest, which is why they are safe here.
class GpuTimer
{
public:
    // False when the context has no timer queries: desktop GL below 3.3
    // without GL_ARB_timer_query, or GLES without GL_EXT_disjoint_timer_query.
    bool initialize(QOpenGLContext *context);
    bool available() const { return m_available; }
    // Reads every pair the GPU has finished into `outMs`. Never waits.
    void collect(std::vector<double> &outMs);
    // Brackets one render call. begin() is false when every slot is still in
    // flight; that frame goes untimed, and end() must then not be called.
    bool begin();
    void end();
    // Deletes the queries. The context must be current.
    void release();

private:
    static constexpr int kSlots = 4;
    struct Slot
    {
        GLuint begin = 0;
        GLuint end = 0;
        bool pending = false;
    };
    using GenQueries = void(QOPENGLF_APIENTRY *)(GLsizei, GLuint *);
    using DeleteQueries = void(QOPENGLF_APIENTRY *)(GLsizei, const GLuint *);
    using QueryCounter = void(QOPENGLF_APIENTRY *)(GLuint, GLenum);
    using GetQueryObjectuiv = void(QOPENGLF_APIENTRY *)(GLuint, GLenum, GLuint *);
    using GetQueryObjectui64v = void(QOPENGLF_APIENTRY *)(GLuint, GLenum, quint64 *);
    using GetIntegerv = void(QOPENGLF_APIENTRY *)(GLenum, GLint *);

    std::array<Slot, kSlots> m_slots{};
    int m_next = 0;
    bool m_available = false;
    GenQueries m_genQueries = nullptr;
    DeleteQueries m_deleteQueries = nullptr;
    QueryCounter m_queryCounter = nullptr;
    GetQueryObjectuiv m_getQueryObjectuiv = nullptr;
    GetQueryObjectui64v m_getQueryObjectui64v = nullptr;
    GetIntegerv m_getIntegerv = nullptr; // GLES only, for GL_GPU_DISJOINT_EXT
};

} // namespace strmqt
