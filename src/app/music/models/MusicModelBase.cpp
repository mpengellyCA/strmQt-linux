#include "app/music/models/MusicModelBase.h"

namespace strmqt::music {

int MusicModelBase::indexOfNavigationIdentity(const QString &identity) const
{
    return m_rowByIdentity.value(identity, -1);
}

QString MusicModelBase::idAt(int row) const
{
    return row >= 0 && row < size() ? idOf(row) : QString();
}

void MusicModelBase::rebuildIndex()
{
    m_rowByIdentity.clear();
    m_rowsById.clear();
    extendIndex(0);
}

void MusicModelBase::extendIndex(int fromRow)
{
    for (int row = fromRow; row < size(); ++row)
        indexRow(row);
}

void MusicModelBase::indexRow(int row)
{
    const QString id = idOf(row);
    m_rowsById[id].append(row);
    const QString entry = playlistItemIdOf(row);
    const QString identity = entry.isEmpty() ? QStringLiteral("i:") + id : QStringLiteral("p:") + entry;
    if (!m_rowByIdentity.contains(identity))
        m_rowByIdentity.insert(identity, row);
}

void MusicModelBase::setTotal(int total)
{
    const int floored = qMax(total, size());
    if (floored == m_total)
        return;
    m_total = floored;
    emit totalRecordCountChanged();
}

} // namespace strmqt::music
