#include "app/controllers/music/MusicLane.h"

#include "core/Log.h"

namespace strmqt::music {

MusicLane::MusicLane(MusicModelBase *model, QObject *parent)
    : QObject(parent)
    , m_model(model)
{
    connect(m_model, &MusicModelBase::countChanged, this, &MusicLane::stateChanged);
}

bool MusicLane::empty() const
{
    return !m_loading && m_error.isEmpty() && m_model->count() == 0;
}

quint64 MusicLane::begin()
{
    ++m_generation;
    m_quiet = false;
    m_loading = true;
    m_error.clear();
    emit stateChanged();
    return m_generation;
}

quint64 MusicLane::beginRefresh()
{
    if (!m_ready || !m_error.isEmpty())
        return begin();
    ++m_generation;
    m_quiet = true;
    return m_generation;
}

void MusicLane::succeed(quint64 generation)
{
    if (!isCurrent(generation))
        return;
    m_loading = false;
    m_quiet = false;
    m_ready = true;
    m_error.clear();
    emit stateChanged();
}

void MusicLane::fail(quint64 generation, const QString &error)
{
    if (!isCurrent(generation))
        return;
    if (m_quiet) {
        // The shelf is already showing what it had; a failed background
        // refetch is not worth replacing it with an error line.
        m_quiet = false;
        qCWarning(logApp) << "music: shelf refresh failed" << error;
        return;
    }
    m_loading = false;
    m_error = error.isEmpty() ? tr("Couldn't load") : error;
    emit stateChanged();
}

void MusicLane::reset()
{
    ++m_generation;
    m_loading = false;
    m_quiet = false;
    m_ready = false;
    m_error.clear();
    emit stateChanged();
}

void MusicLane::retry()
{
    emit retryRequested();
}

} // namespace strmqt::music
