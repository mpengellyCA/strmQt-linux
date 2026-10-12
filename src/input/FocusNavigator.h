#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QTimer>

#include "input/SpatialDecision.h"

class QKeyEvent;
class QQuickItem;
class QQuickWindow;
struct QMetaObject;

namespace strmqt {

// Where a direction key goes when the focused control has no answer for it,
// and where focus goes when the thing holding it stops existing.
//
// Every non-pointer device ends up as an arrow key delivered to the focused
// item (ARCHITECTURE.md §5), so how reliable a gamepad is comes down to what
// happens to an arrow nobody claimed. That used to be decided page by page:
// a KeyNavigation chain, an index walk over a list of sections, a handler on
// the stack for two of the four edges. Each was right on the day it was
// written and wrong the day a region was hidden, loaded late, or was a Tab
// stop nobody had wired an arrow to — and Tab is not something a pad has.
//
// So the unclaimed arrow is answered once, here, from geometry:
//
//  - **Movement.** A direction key that reaches the window's root item was
//    declined by every control between the keyboard and the window. The
//    navigator then moves focus to the nearest control in that direction
//    (SpatialDecision.h). "Control" means any visible, enabled item that is a
//    Tab stop, so everything a keyboard could Tab to, a pad can walk to.
//  - **Modality.** While focus is inside a popup or an item marked
//    `navModal`, only controls inside it are candidates, and a key nothing
//    inside it claimed stops at its edge: neither an arrow nor Back can reach
//    the page under a scrim.
//  - **Recovery.** If the focused item is hidden, disabled or destroyed, or
//    focus is found outside a visible modal surface, focus moves to the
//    nearest live control to where it was. Nothing is ever "focused on
//    nothing".
//  - **Visibility.** The focused control is scrolled into view inside any
//    plain Flickable that contains it.
//
// A control speaks to it through plain properties, read by name — the same
// duck-typed convention `navigationFocusKey`, `cycleTab` and `pageBy` use, so
// no QML has to import anything and every test harness sees the same types:
//
//   property bool navStop        override "is somewhere focus can go" either
//                                way. By default that is any Tab stop, and
//                                any ListView or GridView that has items
//   property Item navUp/navDown/navLeft/navRight
//                                a named neighbour. On the focused item it
//                                is where that key goes. On an ancestor it is
//                                where the ancestor's EDGE leads: geometry is
//                                tried inside it first. A neighbour that is
//                                hidden or disabled is never a dead end:
//                                geometry decides instead (between the parts
//                                of one composite control, where geometry
//                                has no candidates, its own link the same
//                                way is followed first).
//   property bool navModal       confine navigation to this item while it is
//                                visible
//   property Item navEntry       the part of this item that takes the
//                                keyboard when it is entered: a composite
//                                control's first stop, a modal surface's
//                                default button
//   property Item navCursor      the item whose rectangle stands for this
//                                one's position (a view's current delegate is
//                                found without it)
class FocusNavigator : public QObject
{
    Q_OBJECT
    // True while navigation is confined to a popup or a `navModal` surface.
    // The shell folds it into the interaction context, so a shoulder button
    // cannot push a page underneath an open menu.
    Q_PROPERTY(bool modalActive READ modalActive NOTIFY modalActiveChanged)

public:
    explicit FocusNavigator(QQuickWindow *window, QObject *parent = nullptr);

    bool modalActive() const { return m_modalActive; }

    // One step in the direction of `qtKey` (Qt::Key_Left/Right/Up/Down) from
    // whatever holds focus. True when focus moved.
    Q_INVOKABLE bool move(int qtKey, bool autoRepeat = false);
    // Put focus on a live control if it is not on one. True when it had to act.
    Q_INVOKABLE bool recover();
    // Is focus somewhere a key can be answered from?
    Q_INVOKABLE bool focusIsLive() const;

    // How long focus may be in a bad state before recovery acts. The app's
    // own code usually re-seats it within a frame; this is the backstop.
    void setRecoveryDelayMs(int ms) { m_recoveryDelayMs = ms; }

signals:
    void modalActiveChanged();
    // Focus was put somewhere by recovery rather than by the user or a page.
    void recovered(QQuickItem *item);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void onActiveFocusItemChanged();
    void evaluate();
    void watch(QQuickItem *item);
    void guard(QQuickItem *scope);
    bool handleDeclined(QQuickItem *boundary, QKeyEvent *key);
    void updateModalActive();

    bool isStop(QQuickItem *item) const;
    bool isLive(QQuickItem *item) const;
    QQuickItem *scopeRootFor(QQuickItem *item) const;
    QQuickItem *topModalScope() const;
    void collectStops(QQuickItem *root, QList<QQuickItem *> *out) const;
    void collectModals(QQuickItem *root, QList<QQuickItem *> *out) const;
    QQuickItem *cursorItem(QQuickItem *item) const;
    QQuickItem *ownerOf(QQuickItem *focus, QQuickItem *scope) const;
    bool isView(QQuickItem *item) const;
    QRectF sceneRect(QQuickItem *item) const;
    QQuickItem *explicitNeighbour(QQuickItem *item, QQuickItem *from, NavDirection dir,
                                  bool followChain) const;
    QQuickItem *recoveryTarget(QQuickItem *scope) const;
    bool isViewEdgeRepeat(QQuickItem *focus, NavDirection dir) const;
    void give(QQuickItem *target, Qt::FocusReason reason);
    void reveal(QQuickItem *item);

    QVariant duck(QObject *object, const char *name) const;
    QQuickItem *duckItem(QObject *object, const char *name) const;

    QPointer<QQuickWindow> m_window;
    QPointer<QQuickItem> m_watched;
    QRectF m_lastRect;
    QTimer m_evaluate;
    QTimer m_recover;
    int m_recoveryDelayMs = 120;
    bool m_modalActive = false;
    bool m_giving = false;
    bool m_forwarding = false;
    // Popup items and `navModal` surfaces the navigator is filtering keys at.
    QList<QPointer<QQuickItem>> m_guarded;
    // Property index by meta-object, per name: the tree walk asks thousands of
    // items the same two questions on every edge.
    mutable QHash<QByteArray, QHash<const QMetaObject *, int>> m_propertyIndex;
};

} // namespace strmqt
