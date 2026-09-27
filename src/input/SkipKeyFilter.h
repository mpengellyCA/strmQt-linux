#pragma once

#include <QObject>
#include <QPointer>

#include <functional>

class QKeyEvent;

namespace strmqt {

class InputMap;

// A remote's ⏭ / ⏮ (player.skipForward / player.skipBack), caught at the
// window. ⏭ is reported as press and release — PlayerController tells a tap
// (skip) from a hold (fast-forward) — and ⏮ is a skip on its press.
//
// Not a Shortcut, for two reasons. A Shortcut sees a press and never a release,
// and ⏭ means one thing tapped and another held (PlayerController's hold to
// fast-forward), which only the release can tell apart. And a window filter
// sees the key before the focused item does: the player page spends any key it
// does not know on waking its OSD, which would eat the first ⏭ of every visit.
//
// The keys are whatever the input map binds to the two actions, so a rebind
// moves them. They are consumed only while `live()` — something is playing —
// and pass through untouched otherwise.
//
// It is also the handler for the two action ids (InputMap::trigger), so a web
// remote, a pad or the command palette asking for "next" gets the same rule as
// the key. That is always a tap: nothing but a key has a release to wait for.
class SkipKeyFilter final : public QObject
{
    Q_OBJECT

public:
    // What the keys drive. PlayerController in the app; plain callbacks so the
    // key handling is testable without a player.
    struct Target
    {
        std::function<bool()> live;
        std::function<void()> skipForward; // a tap of ⏭ (the action id)
        std::function<void()> skipBack;    // ⏮, key or action id
        // ⏭ as a key: its press and its release, and a press whose release
        // will never arrive because the window lost focus in between.
        std::function<void()> forwardPressed;
        std::function<void()> forwardReleased;
        std::function<void()> forwardCancelled;
    };

    SkipKeyFilter(InputMap *input, Target target, QObject *parent = nullptr);

    // InputMap handler contract (see InputMap::registerHandler).
    Q_INVOKABLE bool invokeAction(const QString &actionId, bool autoRepeat);

    static constexpr auto kSkipForward = "player.skipForward";
    static constexpr auto kSkipBack = "player.skipBack";

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // The skip action this key event is bound to, or empty.
    QString actionFor(const QKeyEvent *event) const;

    static void call(const std::function<void()> &fn)
    {
        if (fn)
            fn();
    }

    QPointer<InputMap> m_input;
    Target m_target;
    // ⏭ is down: its release is owed to the target even if playback stopped
    // in between, so the hold is never left running.
    bool m_forwardDown = false;
};

} // namespace strmqt
