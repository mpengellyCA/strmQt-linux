#pragma once

#include <QFuture>
#include <QMetaObject>
#include <QObject>
#include <QPointer>

#include <functional>
#include <memory>

#include "core/Result.h"

namespace strmqt::music {

// Parallel request join (Crate spec §3.5). Each add() delivers its result to a
// sink on the context's thread; done runs once, queued, after seal() and the
// last delivery. Partial failure is the sinks' business: Fanout never fails.
class Fanout : public std::enable_shared_from_this<Fanout>
{
public:
    static std::shared_ptr<Fanout> create(QObject *context, std::function<void()> done)
    {
        return std::shared_ptr<Fanout>(new Fanout(context, std::move(done)));
    }

    template<class T>
    void add(QFuture<Result<T>> future, std::function<void(Result<T>)> sink)
    {
        if (!m_context)
            return;
        ++m_pending;
        auto self = shared_from_this();
        auto delivered = std::make_shared<bool>(false);
        future
            .then(m_context.data(),
                  [self, sink, delivered](Result<T> result) {
                      // The context's QPointer clears before ~QObject emits
                      // destroyed(), and Qt cancels a future's chain (thus
                      // running its .onCanceled handler) as part of that same
                      // teardown. A callback reached that way must not touch
                      // the (partially) destroyed context or its sink.
                      if (!self->m_context)
                          return;
                      *delivered = true;
                      sink(std::move(result));
                      self->finishOne();
                  })
            .onCanceled(m_context.data(), [self, sink, delivered] {
                if (!self->m_context)
                    return;
                if (*delivered)
                    return;
                *delivered = true;
                sink(Result<T>::failure(QStringLiteral("cancelled")));
                self->finishOne();
            });
    }

    void seal()
    {
        m_sealed = true;
        maybeFire();
    }

private:
    Fanout(QObject *context, std::function<void()> done)
        : m_context(context), m_done(std::move(done))
    {
    }

    void finishOne()
    {
        --m_pending;
        maybeFire();
    }

    void maybeFire()
    {
        if (!m_sealed || m_pending > 0 || m_fired || !m_context)
            return;
        m_fired = true;
        auto self = shared_from_this();
        QMetaObject::invokeMethod(
            m_context.data(), [self] { self->m_done(); }, Qt::QueuedConnection);
    }

    QPointer<QObject> m_context;
    std::function<void()> m_done;
    int m_pending = 0;
    bool m_sealed = false;
    bool m_fired = false;
};

} // namespace strmqt::music
