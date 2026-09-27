#include "SkipKeyFilter.h"

#include "input/InputMap.h"

#include <QEvent>
#include <QKeyEvent>

#include <utility>

namespace strmqt {

SkipKeyFilter::SkipKeyFilter(InputMap *input, Target target, QObject *parent)
    : QObject(parent), m_input(input), m_target(std::move(target))
{
}

bool SkipKeyFilter::invokeAction(const QString &actionId, bool autoRepeat)
{
    const bool forward = actionId == QLatin1String(kSkipForward);
    if (!forward && actionId != QLatin1String(kSkipBack))
        return false;
    if (!m_target.live || !m_target.live())
        return false;
    // A held pad button or remote tile repeats; a skip per repeat would race
    // through a film's chapters. Spent, like the player's toggles.
    if (autoRepeat)
        return true;
    if (forward) {
        if (m_target.skipForward)
            m_target.skipForward();
    } else if (m_target.skipBack) {
        m_target.skipBack();
    }
    return true;
}

QString SkipKeyFilter::actionFor(const QKeyEvent *event) const
{
    if (!m_input || event->key() == 0 || event->key() == Qt::Key_unknown)
        return {};
    const QString action = m_input->actionForKey(event->key(), int(event->modifiers()));
    if (action == QLatin1String(kSkipForward) || action == QLatin1String(kSkipBack))
        return action;
    return {};
}

bool SkipKeyFilter::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease)
        return QObject::eventFilter(watched, event);

    auto *key = static_cast<QKeyEvent *>(event);
    const QString action = actionFor(key);
    if (action.isEmpty() || !m_target.live || !m_target.live())
        return QObject::eventFilter(watched, event);

    if (event->type() == QEvent::KeyPress && !key->isAutoRepeat())
        invokeAction(action, false);
    // Repeats and releases are consumed too: the focused item never saw the
    // press, so it has no business with the rest of it.
    return true;
}

} // namespace strmqt
