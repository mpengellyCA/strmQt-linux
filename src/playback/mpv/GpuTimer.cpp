#include "GpuTimer.h"

#include <QOpenGLContext>

namespace strmqt {

namespace {
constexpr GLenum kTimestamp = 0x8E28;            // GL_TIMESTAMP
constexpr GLenum kQueryResult = 0x8866;          // GL_QUERY_RESULT
constexpr GLenum kQueryResultAvailable = 0x8867; // GL_QUERY_RESULT_AVAILABLE
constexpr GLenum kGpuDisjoint = 0x8FBB;          // GL_GPU_DISJOINT_EXT
} // namespace

bool GpuTimer::initialize(QOpenGLContext *context)
{
    release();
    if (!context)
        return false;
    const bool gles = context->isOpenGLES();
    QByteArray suffix;
    if (gles) {
        if (!context->hasExtension(QByteArrayLiteral("GL_EXT_disjoint_timer_query")))
            return false;
        suffix = QByteArrayLiteral("EXT");
    } else if (context->format().version() < qMakePair(3, 3)
               && !context->hasExtension(QByteArrayLiteral("GL_ARB_timer_query"))) {
        return false;
    }
    const auto resolve = [&](const char *name) {
        return context->getProcAddress(QByteArray(name) + suffix);
    };
    m_genQueries = reinterpret_cast<GenQueries>(resolve("glGenQueries"));
    m_deleteQueries = reinterpret_cast<DeleteQueries>(resolve("glDeleteQueries"));
    m_queryCounter = reinterpret_cast<QueryCounter>(resolve("glQueryCounter"));
    m_getQueryObjectuiv = reinterpret_cast<GetQueryObjectuiv>(resolve("glGetQueryObjectuiv"));
    m_getQueryObjectui64v = reinterpret_cast<GetQueryObjectui64v>(resolve("glGetQueryObjectui64v"));
    m_getIntegerv = gles ? reinterpret_cast<GetIntegerv>(context->getProcAddress("glGetIntegerv"))
                         : nullptr;
    if (!m_genQueries || !m_deleteQueries || !m_queryCounter || !m_getQueryObjectuiv
        || !m_getQueryObjectui64v || (gles && !m_getIntegerv))
        return false;
    GLuint ids[kSlots * 2] = {};
    m_genQueries(kSlots * 2, ids);
    for (int i = 0; i < kSlots; ++i)
        m_slots[i] = {ids[2 * i], ids[2 * i + 1], false};
    m_available = true;
    return true;
}

void GpuTimer::collect(std::vector<double> &outMs)
{
    if (!m_available)
        return;
    // GLES: a disjoint event (a clock change, a context switch) voids every
    // result in flight. Reading the flag also clears it.
    bool disjoint = false;
    if (m_getIntegerv) {
        GLint value = 0;
        m_getIntegerv(kGpuDisjoint, &value);
        disjoint = value != 0;
    }
    for (Slot &slot : m_slots) {
        if (!slot.pending)
            continue;
        GLuint ready = 0;
        m_getQueryObjectuiv(slot.end, kQueryResultAvailable, &ready);
        if (!ready)
            continue; // still in flight; looked at again next frame
        quint64 begin = 0;
        quint64 end = 0;
        m_getQueryObjectui64v(slot.begin, kQueryResult, &begin);
        m_getQueryObjectui64v(slot.end, kQueryResult, &end);
        slot.pending = false;
        if (!disjoint && end > begin)
            outMs.push_back(static_cast<double>(end - begin) / 1.0e6);
    }
}

bool GpuTimer::begin()
{
    if (!m_available || m_slots[m_next].pending)
        return false;
    m_queryCounter(m_slots[m_next].begin, kTimestamp);
    return true;
}

void GpuTimer::end()
{
    Slot &slot = m_slots[m_next];
    m_queryCounter(slot.end, kTimestamp);
    slot.pending = true;
    m_next = (m_next + 1) % kSlots;
}

void GpuTimer::release()
{
    if (m_available && m_deleteQueries) {
        GLuint ids[kSlots * 2];
        for (int i = 0; i < kSlots; ++i) {
            ids[2 * i] = m_slots[i].begin;
            ids[2 * i + 1] = m_slots[i].end;
        }
        m_deleteQueries(kSlots * 2, ids);
    }
    m_slots = {};
    m_next = 0;
    m_available = false;
}

} // namespace strmqt
