#pragma once

#include "MediaSource.h"

#include <QList>
#include <QString>
#include <QUrl>

namespace strmqt {

// How a stream candidate is delivered (Emby PlayMethod wire values).
enum class PlayMethod
{
    DirectPlay,   // original file over HTTP, no server processing
    DirectStream, // remux (container change), streams copied
    Transcode,    // full transcode, HLS
};

QString playMethodName(PlayMethod method); // "DirectPlay" | "DirectStream" | "Transcode"

// One playable URL, ready to hand to the player engine (absolute, auth included —
// the player does its own HTTP and cannot send our headers).
struct StreamCandidate
{
    PlayMethod method = PlayMethod::DirectPlay;
    QUrl url;
    QString container;
    QString mediaSourceId;
};

// One playable version plus *its own* delivery ladder. The ladder is ordered
// best-first (DirectPlay → DirectStream → Transcode) and never mixes sources:
// demoting a rung must degrade the version the user chose, not silently swap to
// a different version (ARCHITECTURE.md).
struct MediaSourceCandidates
{
    MediaSource source;
    QList<StreamCandidate> candidates;

    bool isValid() const { return !candidates.isEmpty(); }
};

// Result of PlaybackInfo: one ladder per media source, in server order.
struct PlaybackTicket
{
    QString playSessionId;
    QList<MediaSourceCandidates> sources;

    qsizetype sourceCount() const { return sources.size(); }
    // nullptr when the index is out of range — callers never index blindly.
    const MediaSourceCandidates *source(qsizetype index) const;

    // The source playback should start on: the server's first source that has at
    // least one playable candidate (a source with an empty ladder cannot be
    // played, so it is never the default). -1 when nothing is playable.
    qsizetype defaultSourceIndex() const;

    // Ladder walking, always scoped to one source.
    qsizetype rungCount(qsizetype sourceIndex) const;
    const StreamCandidate *candidate(qsizetype sourceIndex, qsizetype rung) const;

    // Runtime of one source (sources can differ in length: different cuts).
    qint64 runtimeTicks(qsizetype sourceIndex) const;

    // Index of the source with the given Emby MediaSource id, or -1.
    qsizetype indexOfSourceId(const QString &mediaSourceId) const;

    bool isValid() const { return defaultSourceIndex() >= 0; }
};

// Payload for /Sessions/Playing start/progress/stopped reports.
struct PlaybackProgress
{
    QString itemId;
    QString mediaSourceId;
    QString playSessionId;
    PlayMethod method = PlayMethod::DirectPlay;
    qint64 positionTicks = 0;
    bool paused = false;
};

} // namespace strmqt
