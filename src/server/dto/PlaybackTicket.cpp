#include "PlaybackTicket.h"

namespace strmqt {

QString playMethodName(PlayMethod method)
{
    switch (method) {
    case PlayMethod::DirectPlay:
        return QStringLiteral("DirectPlay");
    case PlayMethod::DirectStream:
        return QStringLiteral("DirectStream");
    case PlayMethod::Transcode:
        return QStringLiteral("Transcode");
    }
    return QStringLiteral("DirectPlay");
}

const MediaSourceCandidates *PlaybackTicket::source(qsizetype index) const
{
    if (index < 0 || index >= sources.size())
        return nullptr;
    return &sources[index];
}

qsizetype PlaybackTicket::defaultSourceIndex() const
{
    for (qsizetype i = 0; i < sources.size(); ++i) {
        if (sources[i].isValid())
            return i;
    }
    return -1;
}

qsizetype PlaybackTicket::rungCount(qsizetype sourceIndex) const
{
    const MediaSourceCandidates *entry = source(sourceIndex);
    return entry ? entry->candidates.size() : 0;
}

const StreamCandidate *PlaybackTicket::candidate(qsizetype sourceIndex, qsizetype rung) const
{
    const MediaSourceCandidates *entry = source(sourceIndex);
    if (!entry || rung < 0 || rung >= entry->candidates.size())
        return nullptr;
    return &entry->candidates[rung];
}

qint64 PlaybackTicket::runtimeTicks(qsizetype sourceIndex) const
{
    const MediaSourceCandidates *entry = source(sourceIndex);
    return entry ? entry->source.runtimeTicks : 0;
}

qsizetype PlaybackTicket::indexOfSourceId(const QString &mediaSourceId) const
{
    if (mediaSourceId.isEmpty())
        return -1;
    for (qsizetype i = 0; i < sources.size(); ++i) {
        if (sources[i].source.id == mediaSourceId)
            return i;
    }
    return -1;
}

} // namespace strmqt
