#pragma once

#include <QObject>

namespace strmqt {

// Blocks the screensaver/DPMS while video plays (org.freedesktop.ScreenSaver).
// Degrades silently when the service is missing (PLAN platform rule).
class PowerInhibit : public QObject
{
    Q_OBJECT

public:
    explicit PowerInhibit(QObject *parent = nullptr);
    ~PowerInhibit() override;

    void acquire(const QString &reason);
    void release();
    bool active() const { return m_cookie != 0; }

private:
    quint32 m_cookie = 0;
};

} // namespace strmqt
