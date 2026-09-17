#include "NavigationKeyHandler.h"

#include "core/Log.h"
#include "input/GamepadDecision.h"
#include "input/InputMap.h"
#include "input/KeyDelivery.h"

namespace strmqt {

NavigationKeyHandler::NavigationKeyHandler(InputMap *input, QObject *parent)
    : QObject(parent)
    , m_input(input)
{
}

bool NavigationKeyHandler::invokeAction(const QString &actionId, bool autoRepeat)
{
    if (!m_input || !InputMap::isNavigationAction(actionId))
        return false;

    const int key = m_input->keyFor(actionId);
    if (key == 0) {
        qCDebug(logApp) << "input: no single-key binding for" << actionId;
        return true;
    }
    const int modifiers = m_input->modifiersFor(actionId);

    QWindow *window = keydelivery::targetWindow();
    if (!window)
        return true;

    // Same rule as the pad (GamepadDecision.h): a key a text field would type
    // must not land in the search box as a letter.
    if (shouldSuppressKey(key, modifiers, keydelivery::textInputFocused(window))) {
        qCDebug(logApp) << "input: not delivering typable key for" << actionId;
        return true;
    }

    // A complete press and release: nothing is left held down. Held directions
    // with a commit on release are the gamepad's own business (GamepadManager).
    keydelivery::postKey(window, key, modifiers, true, autoRepeat);
    keydelivery::postKey(window, key, modifiers, false, autoRepeat);
    return true;
}

} // namespace strmqt
