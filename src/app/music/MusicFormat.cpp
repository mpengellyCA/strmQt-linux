#include "app/music/MusicFormat.h"

#include "app/models/MediaItemModel.h"

namespace strmqt::music {

QString formatDuration(qint64 ms)
{
    if (ms <= 0)
        return {};
    const qint64 total = (ms + 500) / 1000;
    const qint64 hours = total / 3600;
    const qint64 minutes = (total % 3600) / 60;
    const qint64 seconds = total % 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3").arg(hours).arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
}

QString formatRuntime(qint64 ms)
{
    if (ms <= 0)
        return {};
    const qint64 minutes = qMax<qint64>(1, (ms + 30'000) / 60'000);
    if (minutes < 60)
        return QStringLiteral("%1 min").arg(minutes);
    if (minutes % 60 == 0)
        return QStringLiteral("%1 h").arg(minutes / 60);
    return QStringLiteral("%1 h %2 min").arg(minutes / 60).arg(minutes % 60);
}

QString formatRecordCount(int count)
{
    return count == 1 ? QStringLiteral("1 record") : QStringLiteral("%1 records").arg(count);
}

QString formatTrackCount(int count)
{
    return count == 1 ? QStringLiteral("1 track") : QStringLiteral("%1 tracks").arg(count);
}

QString joinNames(const QList<NamedRef> &names)
{
    QStringList list;
    for (const NamedRef &ref : names) {
        if (!ref.name.isEmpty())
            list.append(ref.name);
    }
    if (list.size() <= 1)
        return list.value(0);
    const QString last = list.takeLast();
    return list.join(QStringLiteral(", ")) + QStringLiteral(" & ") + last;
}

QString coverUrl(const ImageRef &ref)
{
    if (!ref.isValid())
        return {};
    return embyImageSource(ref.itemId, ref.imageType.isEmpty() ? QStringLiteral("Primary") : ref.imageType,
                           ref.tag);
}

} // namespace strmqt::music
