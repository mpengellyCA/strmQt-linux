#include "playback/upscale/UpscaleCatalog.h"

#include "core/Log.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace strmqt {

namespace {

UpscaleEntry makeEntry(const char *id, const char *name, UpscaleContent content, int rung,
                       bool doubler, const QStringList &files, const char *license,
                       const QString &scale = {}, const QString &cscale = {})
{
    UpscaleEntry entry;
    entry.id = QString::fromLatin1(id);
    entry.name = QString::fromLatin1(name);
    entry.content = content;
    entry.rung = rung;
    entry.doubler = doubler;
    entry.shaderFiles = files;
    entry.licenseFile = QString::fromLatin1(license);
    entry.scale = scale;
    entry.cscale = cscale;
    return entry;
}

} // namespace

bool UpscaleEntry::offeredFor(ContentType type) const
{
    if (content == UpscaleContent::Either)
        return true;
    if (type == ContentType::Animation)
        return content == UpscaleContent::Animation;
    return content == UpscaleContent::LiveAction;
}

QList<UpscaleEntry> UpscaleCatalog::builtinEntries()
{
    // A neural rung doubles the luma; ewa_lanczossharp covers whatever factor
    // is left over, and the chroma (spec §3, "Neural rungs").
    const QString ewa = QStringLiteral("ewa_lanczossharp");
    using C = UpscaleContent;
    // Order is the picker's: best first, manual-only NIS just above EWA.
    return {
        makeEntry("fsrcnnx16", "FSRCNNX 16", C::LiveAction, 4, true,
                  {QStringLiteral("FSRCNNX_x2_16-0-4-1.glsl")}, "FSRCNNX.LICENSE", ewa, ewa),
        makeEntry("fsrcnnx8", "FSRCNNX 8", C::LiveAction, 3, true,
                  {QStringLiteral("FSRCNNX_x2_8-0-4-1.glsl")}, "FSRCNNX.LICENSE", ewa, ewa),
        makeEntry("anime4k-vl", "Anime4K CNN VL", C::Animation, 4, true,
                  {QStringLiteral("Anime4K_Upscale_CNN_x2_VL.glsl")}, "Anime4K.LICENSE", ewa, ewa),
        makeEntry("anime4k-m", "Anime4K CNN M", C::Animation, 3, true,
                  {QStringLiteral("Anime4K_Upscale_CNN_x2_M.glsl")}, "Anime4K.LICENSE", ewa, ewa),
        makeEntry("fsr", "FSR 1", C::Either, 2, false, {QStringLiteral("FSR.glsl")},
                  "FSR.LICENSE"),
        makeEntry("nis", "NVIDIA Image Scaling", C::Either, -1, false,
                  {QStringLiteral("NVScaler.glsl")}, "NVScaler.LICENSE"),
        makeEntry("ewa", "EWA Lanczos", C::Either, 1, false, {}, "", ewa, ewa),
    };
}

UpscaleCatalog UpscaleCatalog::load(const QString &shaderDirectory)
{
    UpscaleCatalog catalog;
    catalog.m_directory = shaderDirectory;
    const QDir dir(shaderDirectory);
    QStringList missing;
    for (UpscaleEntry entry : builtinEntries()) {
        bool complete = true;
        for (const QString &file : entry.shaderFiles) {
            const QString path = dir.absoluteFilePath(file);
            if (shaderDirectory.isEmpty() || !QFileInfo(path).isFile()) {
                missing << file;
                complete = false;
                continue;
            }
            entry.shaderPaths << path;
        }
        if (complete)
            catalog.m_entries.append(entry);
    }
    if (!missing.isEmpty()) {
        missing.removeDuplicates();
        qCWarning(logPlayback).noquote()
            << "upscaling shaders missing, entries dropped:" << missing.join(QStringLiteral(", "))
            << "(looked in" << (shaderDirectory.isEmpty() ? QStringLiteral("<none>") : shaderDirectory)
            << ")";
    }
    return catalog;
}

UpscaleCatalog UpscaleCatalog::fromEntries(QList<UpscaleEntry> entries)
{
    UpscaleCatalog catalog;
    catalog.m_entries = std::move(entries);
    return catalog;
}

QString UpscaleCatalog::installedShaderDirectory()
{
    // 1. An explicit override, for a developer trying a shader out.
    // 2. Next to the binary: /usr/bin → /usr/share, and the same for the
    //    Flatpak's /app and the AppImage's usr/.
    // 3. The source tree, for ./build/dev/strmqt and the tests.
    // 4. The XDG data directories, for any other prefix.
    QStringList candidates;
    const QString env = qEnvironmentVariable("STRMQT_SHADER_DIR");
    if (!env.isEmpty())
        candidates << env;
    if (QCoreApplication::instance())
        candidates << QCoreApplication::applicationDirPath()
                          + QStringLiteral("/../share/strmqt/shaders");
#ifdef STRMQT_DEV_SHADER_DIR
    candidates << QStringLiteral(STRMQT_DEV_SHADER_DIR);
#endif
    candidates << QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                         QStringLiteral("strmqt/shaders"),
                                         QStandardPaths::LocateDirectory);
    for (const QString &candidate : candidates) {
        if (!candidate.isEmpty() && QFileInfo(candidate).isDir())
            return QDir(candidate).canonicalPath();
    }
    return {};
}

const UpscaleEntry *UpscaleCatalog::find(const QString &id) const
{
    for (const UpscaleEntry &entry : m_entries) {
        if (entry.id == id)
            return &entry;
    }
    return nullptr;
}

const UpscaleEntry *UpscaleCatalog::atRung(ContentType type, int rung) const
{
    for (const UpscaleEntry &entry : m_entries) {
        if (entry.rung == rung && entry.offeredFor(type))
            return &entry;
    }
    return nullptr;
}

QStringList UpscaleCatalog::idsUsingShader(const QString &path) const
{
    QStringList ids;
    for (const UpscaleEntry &entry : m_entries) {
        if (entry.shaderPaths.contains(path))
            ids << entry.id;
    }
    return ids;
}

} // namespace strmqt
