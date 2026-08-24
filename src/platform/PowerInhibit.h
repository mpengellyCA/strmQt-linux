#pragma once

#include <QObject>
#include <QString>

#include <optional>

namespace strmqt {

// Blocks the screensaver/DPMS while video plays. Calls are asynchronous: a
// missing or wedged desktop service must never stall playback transport.
class PowerInhibit : public QObject
{
    Q_OBJECT

public:
    enum class Backend
    {
        ScreenSaver,
        PowerManagement,
    };

    explicit PowerInhibit(QObject *parent = nullptr);
    ~PowerInhibit() override;

    void acquire(const QString &reason);
    void release();
    bool active() const { return m_cookie.has_value(); }

protected:
    // Narrow test seam around transport only. State, fallback ordering and the
    // release-before-reply generation stay in this class and are exercised by
    // unit tests without requiring a real desktop D-Bus service.
    virtual void requestAcquire(Backend backend, const QString &reason, quint64 generation);
    virtual void requestRelease(Backend backend, quint32 cookie);
    void completeAcquire(Backend backend, quint64 generation, bool success, quint32 cookie,
                         const QString &error = {});

private:
    bool m_wanted = false;
    bool m_acquirePending = false;
    quint64 m_generation = 0;
    QString m_reason;
    std::optional<Backend> m_backend;
    std::optional<quint32> m_cookie;
};

} // namespace strmqt
