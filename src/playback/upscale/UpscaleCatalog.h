#pragma once

#include "playback/upscale/VideoProcessing.h"

#include <QList>
#include <QSize>
#include <QString>
#include <QStringList>

namespace strmqt {

// What an item is, for choosing a ladder (spec §4.3). Unknown — no genres yet,
// or none that say animation — plays the live-action ladder.
enum class ContentType { Unknown, LiveAction, Animation };

// Which items an upscaler is offered for.
enum class UpscaleContent { LiveAction, Animation, Either };

// One row of spec §3's table.
struct UpscaleEntry
{
    QString id;
    QString name;
    UpscaleContent content = UpscaleContent::Either;
    int rung = -1;            // 1..4 on the Auto ladder; -1 = manual only (NIS)
    bool doubler = false;     // a 2× neural pass, only worth it from 1.5× up
    QStringList shaderFiles;  // bare file names in the shader directory, hook order
    QString licenseFile;      // bare file name; empty for a built-in mpv filter
    QString scale;            // mpv filters set alongside the shaders
    QString cscale;
    bool sdrOnly = false;     // skipped for HDR; no entry needs it today (spec §2.6)
    bool sdOnly = false;      // Auto picks it only for an SD source (smaller than
                              // 1280×720 both ways); a manual pick ignores this
    QStringList shaderPaths;  // absolute; filled in by UpscaleCatalog::load()

    bool manualOnly() const { return rung < 0; }
    bool offeredFor(ContentType type) const;
    VideoProcessing chain() const { return {shaderPaths, scale, cscale}; }
};

// The upscalers StrmQt ships, resolved against the installed shader files.
class UpscaleCatalog
{
public:
    // Spec §3's table, unresolved: shaderPaths is empty.
    static QList<UpscaleEntry> builtinEntries();
    // Resolves every entry's files under `shaderDirectory`. An entry whose
    // files are missing is dropped, with one warning naming them all.
    static UpscaleCatalog load(const QString &shaderDirectory);
    // Entries taken as given, without touching the disk. For tests.
    static UpscaleCatalog fromEntries(QList<UpscaleEntry> entries);
    // Where an installed StrmQt keeps its shaders; see the definition for the
    // search order. Empty when none of the candidates exists.
    static QString installedShaderDirectory();

    const QList<UpscaleEntry> &entries() const { return m_entries; }
    const UpscaleEntry *find(const QString &id) const;
    // The Auto entry for this rung and content type; nullptr when there is none.
    // An SD `source` (under 1280×720 both ways) prefers an sdOnly entry for the rung; any
    // other source, or an unknown (empty) one, never gets an sdOnly entry.
    const UpscaleEntry *atRung(ContentType type, int rung, QSize source) const;
    // Every entry whose chain includes this shader file. A failure report
    // names a file, not an entry, and two entries may share a file.
    QStringList idsUsingShader(const QString &path) const;
    QString shaderDirectory() const { return m_directory; }

private:
    QList<UpscaleEntry> m_entries;
    QString m_directory;
};

} // namespace strmqt
