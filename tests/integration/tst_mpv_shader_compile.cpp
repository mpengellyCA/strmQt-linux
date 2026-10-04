#include <QtTest>

#include <algorithm>

#include "playback/mpv/GpuTimer.h"
#include "playback/mpv/MpvPlayer.h"
#include "playback/upscale/UpscaleCatalog.h"

#include <QElapsedTimer>
#include <QFile>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>
#include <QTemporaryDir>

#include <mpv/client.h>
#include <mpv/render_gl.h>

using namespace strmqt;

// Every bundled shader chain must compile on the libmpv/Mesa of every target
// distro (spec 2026-10-03 §6). A real render context on an offscreen surface
// with Qt's default format — StrmQt's own path — and MpvPlayer driven
// directly, which bypasses UpscalePolicy's software-renderer rule on purpose:
// CI only has llvmpipe, and llvmpipe compiles what a GPU compiles.

namespace {

QStringList g_errors;
QtMessageHandler g_previous = nullptr;

void captureErrors(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    static const QStringList markers{QStringLiteral("compile log"), QStringLiteral("Hook tried"),
                                     QStringLiteral("INVALID_OPERATION")};
    for (const QString &marker : markers) {
        if (message.contains(marker))
            g_errors << message;
    }
    if (g_previous)
        g_previous(type, context, message);
}

void *glProcAddress(void *, const char *name)
{
    QOpenGLContext *context = QOpenGLContext::currentContext();
    return context ? reinterpret_cast<void *>(context->getProcAddress(name)) : nullptr;
}

} // namespace

// A missing GL context or ffmpeg skips the test where that is expected (a
// package build), and fails it where the gate exists to prove the chains
// compile: CI and check.sh under Xvfb export STRMQT_REQUIRE_GL=1.
#define SKIP_UNLESS_GL_REQUIRED(message)                                                           \
    do {                                                                                           \
        if (qEnvironmentVariableIsSet("STRMQT_REQUIRE_GL"))                                        \
            QFAIL(message);                                                                        \
        QSKIP(message);                                                                            \
    } while (false)

class MpvShaderCompileTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void chainCompiles_data();
    void chainCompiles();
    void brokenShaderIsReported();

private:
    struct RunResult
    {
        int frames = 0;
        std::vector<double> gpuMs;
        int failures = 0;
        bool timerAvailable = false;
        QSize videoSize;
    };
    RunResult renderFor(const VideoProcessing &chain, int milliseconds);

    QTemporaryDir m_dir;
    QString m_clip;
    QOpenGLContext m_context;
    QOffscreenSurface m_surface;
};

void MpvShaderCompileTest::initTestCase()
{
    if (!m_context.create())
        SKIP_UNLESS_GL_REQUIRED(
            "no OpenGL context: the shader compile test needs one (Mesa's llvmpipe is enough)");
    m_surface.setFormat(m_context.format());
    m_surface.create();
    if (!m_surface.isValid() || !m_context.makeCurrent(&m_surface))
        SKIP_UNLESS_GL_REQUIRED("no current OpenGL context on an offscreen surface");

    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty())
        SKIP_UNLESS_GL_REQUIRED("ffmpeg is required to generate the 360p test clip");
    m_clip = m_dir.filePath(QStringLiteral("clip360.avi"));
    QProcess process;
    process.start(ffmpeg, {QStringLiteral("-y"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                           QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                           QStringLiteral("testsrc2=duration=10:size=640x360:rate=24"),
                           QStringLiteral("-c:v"), QStringLiteral("mpeg4"), m_clip});
    QVERIFY(process.waitForFinished(30000));
    QCOMPARE(process.exitCode(), 0);
    g_previous = qInstallMessageHandler(captureErrors);
}

void MpvShaderCompileTest::cleanupTestCase()
{
    qInstallMessageHandler(g_previous);
}

MpvShaderCompileTest::RunResult MpvShaderCompileTest::renderFor(const VideoProcessing &chain,
                                                                int milliseconds)
{
    RunResult result;
    MpvPlayer player;
    QSignalSpy failures(&player, &PlayerBackend::videoShaderFailed);
    player.setVideoProcessing(chain);
    // mpv initialises its video output the moment a file opens, and fails it
    // with "No render context set" if the render context does not exist yet.
    // load() is what creates the core, so a file that cannot open creates it
    // first (load id 1); the context goes on it; then the clip plays (id 2).
    player.load(QUrl::fromLocalFile(QStringLiteral("/strmqt-test-missing-media")), 0, 1, true);
    mpv_handle *handle = player.handle();
    if (!handle)
        return result;
    player.renderLink()->acquire();

    mpv_opengl_init_params glParams{glProcAddress, nullptr};
    mpv_render_param createParams[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glParams},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    mpv_render_context *render = nullptr;
    if (mpv_render_context_create(&render, handle, createParams) < 0) {
        player.renderLink()->release();
        return result;
    }

    // Let the first load's failure drain, so its late end-file event cannot be
    // mistaken for the clip's.
    QElapsedTimer settle;
    settle.start();
    while (player.state() != PlayerBackend::State::Error && settle.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(2);
    }
    player.load(QUrl::fromLocalFile(m_clip), 0, 2);

    QOpenGLFramebufferObject fbo(1280, 720);
    GpuTimer timer;
    timer.initialize(&m_context);
    result.timerAvailable = timer.available();
    mpv_opengl_fbo mpvFbo{static_cast<int>(fbo.handle()), fbo.width(), fbo.height(), 0};
    int flipY = 0;
    int block = 0;
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &mpvFbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flipY},
        {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &block},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };

    QElapsedTimer clock;
    clock.start();
    // A cold shader cache makes the first frame slow (fsrcnnx16 on a fresh
    // Mesa cache took over a second), so `milliseconds` counts from the first
    // frame, with a hard cap on the wait for it.
    constexpr qint64 kFirstFrameCapMs = 30000;
    qint64 firstFrameAt = -1;
    while (firstFrameAt < 0 ? clock.elapsed() < kFirstFrameCapMs
                            : clock.elapsed() - firstFrameAt < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        // Sampled here, not after the loop: mpv drops the video params when
        // the clip ends, and a cold shader cache on a software renderer can
        // spend most of the clip compiling.
        if (!player.videoSize().isEmpty())
            result.videoSize = player.videoSize();
        if (!(mpv_render_context_update(render) & MPV_RENDER_UPDATE_FRAME)) {
            QThread::msleep(2);
            continue;
        }
        timer.collect(result.gpuMs);
        const bool timed = timer.begin();
        mpv_render_context_render(render, params);
        if (timed) {
            timer.end();
            // Submit the end timestamp now, as MpvVideoItem::render() does.
            m_context.functions()->glFlush();
        }
        if (++result.frames == 1)
            firstFrameAt = clock.elapsed();
    }
    m_context.functions()->glFinish();
    timer.collect(result.gpuMs);
    QCoreApplication::processEvents(); // drain mpv's last log messages

    timer.release();
    mpv_render_context_free(render);
    player.renderLink()->release();
    result.failures = static_cast<int>(failures.count());
    player.stop();
    return result;
}

void MpvShaderCompileTest::chainCompiles_data()
{
    QTest::addColumn<QString>("id");
    const UpscaleCatalog catalog = UpscaleCatalog::load(QStringLiteral(STRMQT_SOURCE_DIR "/shaders"));
    QCOMPARE(catalog.entries().size(), UpscaleCatalog::builtinEntries().size());
    for (const UpscaleEntry &entry : catalog.entries())
        QTest::newRow(qPrintable(entry.id)) << entry.id;
}

void MpvShaderCompileTest::chainCompiles()
{
    QFETCH(QString, id);
    const UpscaleCatalog catalog = UpscaleCatalog::load(QStringLiteral(STRMQT_SOURCE_DIR "/shaders"));
    g_errors.clear();
    const RunResult run = renderFor(catalog.find(id)->chain(), 1000);
    QVERIFY2(run.frames > 0, "mpv rendered no frames");
    QCOMPARE(run.videoSize, QSize(640, 360));
    qInfo("%s: %d frames, %zu GPU samples, max %.3f ms, renderer %s", qPrintable(id), run.frames,
          run.gpuMs.size(),
          run.gpuMs.empty() ? 0.0 : *std::max_element(run.gpuMs.begin(), run.gpuMs.end()),
          reinterpret_cast<const char *>(m_context.functions()->glGetString(GL_RENDERER)));
    QVERIFY2(g_errors.isEmpty(), qPrintable(g_errors.join(u'\n')));
    QCOMPARE(run.failures, 0);
    if (run.timerAvailable) {
        // Spec §6: a timestamp-query pair returns a non-zero GPU time.
        QVERIFY2(!run.gpuMs.empty(), "timer queries are present but no GPU time came back");
        QVERIFY2(*std::max_element(run.gpuMs.begin(), run.gpuMs.end()) > 0.0,
                 "every GPU time was zero");
    } else {
        qWarning("no GPU timer queries on this context; timing unchecked");
    }
}

void MpvShaderCompileTest::brokenShaderIsReported()
{
    // The other half of the guard: a shader that cannot compile must come
    // back as videoShaderFailed, or the policy never learns to skip it.
    const QString path = m_dir.filePath(QStringLiteral("broken.glsl"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("//!HOOK MAIN\n//!BIND HOOKED\nvec4 hook() { return this is not glsl; }\n");
    file.close();
    g_errors.clear();
    const RunResult run = renderFor({{path}, QString(), QString()}, 1000);
    QVERIFY(run.frames > 0);
    QCOMPARE(run.failures, 1);
}

QTEST_MAIN(MpvShaderCompileTest)
#include "tst_mpv_shader_compile.moc"
