#pragma once

#include <QObject>
#include <QString>

namespace strmqt {

class InputMap;

// The handler for the navigation actions (InputMap::isNavigationAction): the
// arrows, Select, Back, paging and the context menu.
//
// Those are the actions whose meaning belongs to the focused control — Down
// moves a grid's cursor, steps a slider, leaves a text field — and Qt's own key
// handling already routes that. So this handler carries them out by delivering
// the key currently bound to the action, press then release, to the app
// window's focused item. A rebound Back is still Back on the phone.
//
// Registered by Application before any QML exists, which makes it the handler
// asked last: a page that wants to answer a navigation action by id can.
class NavigationKeyHandler : public QObject
{
    Q_OBJECT

public:
    explicit NavigationKeyHandler(InputMap *input, QObject *parent = nullptr);

    Q_INVOKABLE bool invokeAction(const QString &actionId, bool autoRepeat);

private:
    InputMap *m_input = nullptr;
};

} // namespace strmqt
