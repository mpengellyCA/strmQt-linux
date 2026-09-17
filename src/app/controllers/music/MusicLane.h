#pragma once

#include <QObject>
#include <QString>

#include "app/music/models/MusicModelBase.h"

namespace strmqt::music {

// One independently loading shelf or section (Crate spec §4). The owner calls
// begin() before a request and succeed()/fail() with the returned generation;
// a reply for an older generation changes nothing. `empty` is the one flag a
// shelf needs to hide itself: not loading, no error, no rows.
class MusicLane : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *model READ model CONSTANT)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY stateChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged)

public:
    explicit MusicLane(MusicModelBase *model, QObject *parent = nullptr);

    QObject *model() const { return m_model; }
    MusicModelBase *typedModel() const { return m_model; }
    bool loading() const { return m_loading; }
    QString error() const { return m_error; }
    bool ready() const { return m_ready; }
    bool empty() const;

    quint64 begin();
    // A refetch of a lane already on screen: loading stays false and a failure
    // keeps the rows. A lane that never loaded, or last failed, gets begin().
    quint64 beginRefresh();
    bool isCurrent(quint64 generation) const { return generation == m_generation; }
    void succeed(quint64 generation);
    void fail(quint64 generation, const QString &error);
    void reset();

    Q_INVOKABLE void retry();

signals:
    void stateChanged();
    void retryRequested();

private:
    MusicModelBase *m_model;
    quint64 m_generation = 0;
    bool m_loading = false;
    bool m_quiet = false;
    bool m_ready = false;
    QString m_error;
};

} // namespace strmqt::music
