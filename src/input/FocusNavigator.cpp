#include "FocusNavigator.h"

#include <algorithm>
#include <cstring>

#include <QCoreApplication>
#include <QKeyEvent>
#include <QLoggingCategory>
#include <QMetaProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QVariant>

// QT_LOGGING_RULES="strmqt.input.focus.debug=true" narrates every step the
// navigator takes: which control an arrow landed on and why focus was moved.
Q_LOGGING_CATEGORY(logFocus, "strmqt.input.focus", QtInfoMsg)

namespace strmqt {

namespace {

// Room kept between a revealed control and the edge of the view scrolling it.
constexpr qreal kRevealMargin = 32;
// While focus stays bad and nothing can take it (a page still loading), look
// again this often.
constexpr int kRecoveryRetryMs = 200;

bool directionFor(int key, NavDirection *out)
{
    switch (key) {
    case Qt::Key_Left:
        *out = NavDirection::Left;
        return true;
    case Qt::Key_Right:
        *out = NavDirection::Right;
        return true;
    case Qt::Key_Up:
        *out = NavDirection::Up;
        return true;
    case Qt::Key_Down:
        *out = NavDirection::Down;
        return true;
    default:
        return false;
    }
}

const char *neighbourProperty(NavDirection dir)
{
    switch (dir) {
    case NavDirection::Left:
        return "navLeft";
    case NavDirection::Right:
        return "navRight";
    case NavDirection::Up:
        return "navUp";
    case NavDirection::Down:
        return "navDown";
    }
    return "";
}

bool isAncestorOf(const QQuickItem *ancestor, const QQuickItem *item)
{
    for (const QQuickItem *p = item ? item->parentItem() : nullptr; p; p = p->parentItem()) {
        if (p == ancestor)
            return true;
    }
    return false;
}

bool isPopupItem(const QQuickItem *item)
{
    return item->inherits("QQuickPopupItem");
}

QList<QQuickItem *> childrenInPaintOrder(QQuickItem *item)
{
    QList<QQuickItem *> children = item->childItems();
    std::stable_sort(children.begin(), children.end(),
                     [](const QQuickItem *a, const QQuickItem *b) { return a->z() < b->z(); });
    return children;
}

} // namespace

FocusNavigator::FocusNavigator(QQuickWindow *window, QObject *parent)
    : QObject(parent)
    , m_window(window)
{
    Q_ASSERT(window);
    // The window sees every key first; its root item sees only the ones every
    // control declined, because Qt offers a key to the focused item and then
    // to each ancestor in turn until one accepts.
    window->installEventFilter(this);
    window->contentItem()->installEventFilter(this);

    m_evaluate.setSingleShot(true);
    m_evaluate.setInterval(0);
    connect(&m_evaluate, &QTimer::timeout, this, &FocusNavigator::evaluate);
    m_recover.setSingleShot(true);
    connect(&m_recover, &QTimer::timeout, this, [this] {
        if (focusIsLive())
            return;
        if (!recover())
            m_recover.start(kRecoveryRetryMs);
    });

    connect(window, &QQuickWindow::activeFocusItemChanged, this,
            &FocusNavigator::onActiveFocusItemChanged);
    onActiveFocusItemChanged();
}

// ── Duck-typed properties ───────────────────────────────────────────────────

QVariant FocusNavigator::duck(QObject *object, const char *name) const
{
    if (!object)
        return {};
    const QMetaObject *meta = object->metaObject();
    QHash<const QMetaObject *, int> &indexes = m_propertyIndex[QByteArray::fromRawData(
        name, static_cast<qsizetype>(std::strlen(name)))];
    auto it = indexes.constFind(meta);
    int index = it != indexes.constEnd() ? it.value() : -2;
    // A meta-object can be freed and its address reused by another type, so a
    // remembered index is only trusted if the name still matches.
    if (index >= 0
        && (index >= meta->propertyCount() || std::strcmp(meta->property(index).name(), name) != 0))
        index = -2;
    if (index == -2) {
        index = meta->indexOfProperty(name);
        indexes.insert(meta, index);
    }
    if (index < 0)
        return {};
    return meta->property(index).read(object);
}

QQuickItem *FocusNavigator::duckItem(QObject *object, const char *name) const
{
    const QVariant value = duck(object, name);
    if (!value.isValid())
        return nullptr;
    return qobject_cast<QQuickItem *>(value.value<QObject *>());
}

// ── What counts ─────────────────────────────────────────────────────────────

bool FocusNavigator::isStop(QQuickItem *item) const
{
    const QVariant declared = duck(item, "navStop");
    if (declared.isValid() && declared.typeId() == QMetaType::Bool)
        return declared.toBool();
    if (item->activeFocusOnTab())
        return true;
    // A list or a grid with something in it is somewhere to be, whether or
    // not anybody remembered to make it a Tab stop — and the shelves, grids
    // and track tables this app is made of mostly are not. A list that is
    // only decoration says so with `navStop: false`.
    if (item->inherits("QQuickItemView"))
        return duck(item, "count").toInt() > 0;
    return false;
}

// Can a key be answered from here? Visible and enabled are both effective
// values in QtQuick: an item under a hidden or disabled ancestor fails them.
bool FocusNavigator::isLive(QQuickItem *item) const
{
    if (!item || !m_window || item->window() != m_window)
        return false;
    if (item == m_window->contentItem())
        return false;
    return item->isVisible() && item->isEnabled();
}

QQuickItem *FocusNavigator::scopeRootFor(QQuickItem *item) const
{
    for (QQuickItem *p = item; p; p = p->parentItem()) {
        if (isPopupItem(p) || duck(p, "navModal").toBool())
            return p;
    }
    return m_window ? m_window->contentItem() : nullptr;
}

void FocusNavigator::collectModals(QQuickItem *root, QList<QQuickItem *> *out) const
{
    const QList<QQuickItem *> children = childrenInPaintOrder(root);
    for (QQuickItem *child : children) {
        if (!child->isVisible())
            continue;
        if (duck(child, "navModal").toBool())
            out->append(child);
        collectModals(child, out);
    }
}

// The modal surface drawn last: the one the user is looking at.
QQuickItem *FocusNavigator::topModalScope() const
{
    if (!m_window)
        return nullptr;
    QList<QQuickItem *> modals;
    collectModals(m_window->contentItem(), &modals);
    return modals.isEmpty() ? nullptr : modals.constLast();
}

void FocusNavigator::collectStops(QQuickItem *root, QList<QQuickItem *> *out) const
{
    const QList<QQuickItem *> children = root->childItems();
    for (QQuickItem *child : children) {
        // An invisible or disabled subtree has nothing to land on, and a
        // fully transparent one is a surface mid-load: live to Qt, absent to
        // the user.
        if (!child->isVisible() || !child->isEnabled() || child->opacity() <= 0)
            continue;
        if (isStop(child) && child->width() > 0 && child->height() > 0)
            out->append(child);
        collectStops(child, out);
    }
}

// Something that moves a cursor over items of its own: a ListView, a
// GridView, a tab bar. A StackView also has a current item, but no index —
// it shows one page, it does not walk a list of them.
bool FocusNavigator::isView(QQuickItem *item) const
{
    return duck(item, "currentItem").isValid() && duck(item, "currentIndex").isValid();
}

// The item whose rectangle says where "here" is. A view holds focus as a
// whole and draws its cursor on a delegate, so the delegate is here.
QQuickItem *FocusNavigator::cursorItem(QQuickItem *item) const
{
    if (!item)
        return nullptr;
    if (QQuickItem *declared = duckItem(item, "navCursor"))
        return declared;
    if (isView(item)) {
        QQuickItem *current = duckItem(item, "currentItem");
        if (current && current->isVisible())
            return current;
    }
    return item;
}

// The control the focused item belongs to. Active focus is often on something
// smaller than a control — an item view passes it down to its current
// delegate — and it is the control, not the delegate, that sits in the page's
// geometry, owns everything inside itself and has edges.
//
// Null when focus is on a bare container: nothing between it and the scope is
// a Tab stop or a view.
QQuickItem *FocusNavigator::ownerOf(QQuickItem *focus, QQuickItem *scope) const
{
    for (QQuickItem *p = focus; p; p = p->parentItem()) {
        if (isStop(p) || isView(p))
            return p;
        if (p == scope)
            break;
    }
    return nullptr;
}

QRectF FocusNavigator::sceneRect(QQuickItem *item) const
{
    return item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
}

// Where `item`'s cursor is on screen: its own rectangle, or a view's current
// delegate's.
//
// A view moves its current item at once and scrolls to it on the next frame,
// so between the two the delegate stands where the list would have it if
// nothing were clipped: thirty rows down a list six rows tall is a rectangle
// a thousand pixels below the window. Measured from there, "the nearest
// control to the left" is whatever is lowest on the page. The cursor is in
// the view, so its rectangle is brought to the nearest place inside it —
// which is where the next frame will draw it.
QRectF FocusNavigator::cursorRect(QQuickItem *item) const
{
    QQuickItem *cursor = cursorItem(item);
    QRectF rect = sceneRect(cursor);
    if (cursor == item || !isView(item) || !isAncestorOf(item, cursor))
        return rect;
    const QRectF view = sceneRect(item);
    if (rect.width() <= view.width())
        rect.moveLeft(qBound(view.left(), rect.left(), view.right() - rect.width()));
    if (rect.height() <= view.height())
        rect.moveTop(qBound(view.top(), rect.top(), view.bottom() - rect.height()));
    return rect;
}

// The neighbour `item` names for `dir`, if it leads anywhere. A named
// neighbour that is hidden or disabled is not a dead end. With `followChain`
// its own link in the same direction is followed, the way a row of buttons
// inside one control skips the one that is not on screen in this mode;
// without it the answer is null and geometry decides, which is right between
// controls — a chain written for one layout skips whole rows in another.
QQuickItem *FocusNavigator::explicitNeighbour(QQuickItem *item, QQuickItem *from,
                                              NavDirection dir, bool followChain) const
{
    const char *name = neighbourProperty(dir);
    QQuickItem *target = duckItem(item, name);
    for (int hops = 0; target && hops < 16; ++hops) {
        if (target != from && isLive(target) && target->opacity() > 0)
            return target;
        if (!followChain)
            return nullptr;
        target = duckItem(target, name);
    }
    return nullptr;
}

// A view that ran out of items in the direction it scrolls is an edge, and a
// held direction stops at an edge instead of spilling into whatever is next
// to the view. Crossing takes a fresh press.
bool FocusNavigator::isViewEdgeRepeat(QQuickItem *focus, NavDirection dir) const
{
    if (!isView(focus))
        return false;
    if (duck(focus, "cellWidth").isValid())
        return true; // a grid scrolls one way and wraps the other: both are its own
    const QVariant orientation = duck(focus, "orientation");
    if (!orientation.isValid())
        return true;
    const bool horizontalView = orientation.toInt() == Qt::Horizontal;
    return horizontalView == spatial::isHorizontal(dir);
}

// ── Acting ──────────────────────────────────────────────────────────────────

void FocusNavigator::give(QQuickItem *target, Qt::FocusReason reason)
{
    // A composite control says which of its parts takes the keyboard when it
    // is entered; without that a focus scope hands it to whichever child had
    // it last, which may be a part the user never meant to return to.
    if (QQuickItem *entry = duckItem(target, "navEntry")) {
        if (isLive(entry) && !target->hasActiveFocus())
            target = entry;
    }
    // A view is entered on its cursor. Focusing the view itself hands the
    // keyboard to whichever child of its focus scope had it last, and a
    // table's footer or header control can be that child.
    if (isView(target) && target->inherits("QQuickItemView")) {
        QQuickItem *current = duckItem(target, "currentItem");
        if (!current && duck(target, "count").toInt() > 0) {
            target->setProperty("currentIndex", 0);
            current = duckItem(target, "currentItem");
        }
        if (current && isLive(current))
            target = current;
    }
    m_giving = true;
    target->forceActiveFocus(reason);
    m_giving = false;
    reveal(m_window ? m_window->activeFocusItem() : target);
}

// Scroll every plain Flickable around the focused control far enough to show
// it. Item views are left alone: they follow their own current item, and a
// second opinion about contentY fights their snapping.
void FocusNavigator::reveal(QQuickItem *item)
{
    QQuickItem *shown = cursorItem(item);
    if (!shown)
        return;
    for (QQuickItem *p = shown->parentItem(); p; p = p->parentItem()) {
        if (!p->inherits("QQuickFlickable") || p->inherits("QQuickItemView"))
            continue;
        QQuickItem *content = duckItem(p, "contentItem");
        if (!content || !isAncestorOf(content, shown))
            continue;
        const QVariant declaredMargin = duck(p, "navRevealMargin");
        const qreal margin = declaredMargin.isValid() ? declaredMargin.toReal() : kRevealMargin;
        const QRectF rect = shown->mapRectToItem(content, QRectF(0, 0, shown->width(), shown->height()));

        const qreal contentY = p->property("contentY").toReal();
        const qreal originY = p->property("originY").toReal();
        const qreal maxY = std::max(originY, originY + p->property("contentHeight").toReal() - p->height());
        qreal wantedY = contentY;
        if (rect.height() + 2 * margin >= p->height() || rect.top() - margin < contentY)
            wantedY = rect.top() - margin;
        else if (rect.bottom() + margin > contentY + p->height())
            wantedY = rect.bottom() + margin - p->height();
        wantedY = std::clamp(wantedY, originY, maxY);
        if (!qFuzzyCompare(wantedY + 1, contentY + 1))
            p->setProperty("contentY", wantedY);

        const qreal contentX = p->property("contentX").toReal();
        const qreal originX = p->property("originX").toReal();
        const qreal maxX = std::max(originX, originX + p->property("contentWidth").toReal() - p->width());
        if (maxX > originX) {
            qreal wantedX = contentX;
            if (rect.width() + 2 * margin >= p->width() || rect.left() - margin < contentX)
                wantedX = rect.left() - margin;
            else if (rect.right() + margin > contentX + p->width())
                wantedX = rect.right() + margin - p->width();
            wantedX = std::clamp(wantedX, originX, maxX);
            if (!qFuzzyCompare(wantedX + 1, contentX + 1))
                p->setProperty("contentX", wantedX);
        }
    }
}

bool FocusNavigator::move(int qtKey, bool autoRepeat)
{
    NavDirection dir;
    if (!m_window || !directionFor(qtKey, &dir))
        return false;
    QQuickItem *focus = m_window->activeFocusItem();
    if (!isLive(focus))
        return recover();

    QQuickItem *scope = scopeRootFor(focus);
    if (QQuickItem *top = topModalScope(); top && scope == m_window->contentItem())
        return recover(); // focus leaked out from under a modal surface

    QQuickItem *owner = ownerOf(focus, scope);
    if (autoRepeat && owner && isViewEdgeRepeat(owner, dir))
        return false;

    const Qt::FocusReason reason =
        (dir == NavDirection::Left || dir == NavDirection::Up) ? Qt::BacktabFocusReason
                                                                : Qt::TabFocusReason;

    // What the focused item itself names comes first: it is the most
    // specific thing anybody said about this key.
    // The parts of a composite control (the docked bar's buttons) are not
    // candidates for geometry, so between them the chain is all there is.
    const bool insideComposite = owner && owner != focus && !isStop(focus);
    if (QQuickItem *target = explicitNeighbour(focus, focus, dir, insideComposite)) {
        qCDebug(logFocus) << "move" << qtKey << "named" << focus << "->" << target;
        give(target, reason);
        return true;
    }

    QList<QQuickItem *> stops;
    collectStops(scope, &stops);

    // A focused item that is neither a control nor a view is a container
    // holding the keyboard for its children: a page whose content failed to
    // load, an empty state. Any arrow steps into it.
    if (!owner) {
        QList<QQuickItem *> inside;
        QList<QRectF> insideRects;
        for (QQuickItem *stop : std::as_const(stops)) {
            if (isAncestorOf(focus, stop)) {
                inside.append(stop);
                insideRects.append(sceneRect(stop));
            }
        }
        if (!inside.isEmpty()) {
            const int nearest = m_lastRect.isValid() ? pickNearest(m_lastRect, insideRects) : 0;
            qCDebug(logFocus) << "move" << qtKey << "enter" << focus << "->" << inside.at(nearest);
            give(inside.at(nearest), reason);
            return true;
        }
    }

    QQuickItem *self = owner ? owner : focus;
    QList<QQuickItem *> candidates;
    QList<QRectF> rects;
    for (QQuickItem *stop : std::as_const(stops)) {
        // What the focused control contains is its own business, and what
        // contains it is where the keyboard already is.
        if (stop == self || isAncestorOf(self, stop) || isAncestorOf(stop, self))
            continue;
        candidates.append(stop);
        rects.append(sceneRect(stop));
    }
    const QRectF source = cursorRect(self);

    // A region that names a neighbour is saying where its EDGE leads. So
    // geometry is tried inside the region first, and the name is used only
    // when the region has nothing further that way — innermost region first.
    for (QQuickItem *region = focus->parentItem(); region; region = region->parentItem()) {
        if (duck(region, neighbourProperty(dir)).isValid()) {
            QList<int> within;
            QList<QRectF> withinRects;
            for (int i = 0; i < candidates.size(); ++i) {
                if (isAncestorOf(region, candidates.at(i))) {
                    within.append(i);
                    withinRects.append(rects.at(i));
                }
            }
            const int inner = pickSpatialTarget(source, withinRects, dir);
            if (inner >= 0) {
                QQuickItem *target = candidates.at(within.at(inner));
                qCDebug(logFocus) << "move" << qtKey << focus << "->" << target << "inside" << region;
                give(target, reason);
                return true;
            }
            if (QQuickItem *target = explicitNeighbour(region, focus, dir, true)) {
                if (!isAncestorOf(target, focus)) {
                    // A named edge leads to another surface — the page to
                    // the rail, the page to the docked bar — and a held
                    // direction stops there. The far side often hands the
                    // keyboard straight back on the same key, and a held
                    // one would swap the two several times a second.
                    if (autoRepeat)
                        return false;
                    qCDebug(logFocus) << "move" << qtKey << "edge of" << region << "->" << target;
                    give(target, reason);
                    return true;
                }
            }
        }
        if (region == scope)
            break;
    }

    const int picked = pickSpatialTarget(source, rects, dir);
    if (picked < 0) {
        qCDebug(logFocus) << "move" << qtKey << "nothing that way from" << focus;
        return false;
    }
    qCDebug(logFocus) << "move" << qtKey << focus << "->" << candidates.at(picked);
    give(candidates.at(picked), reason);
    return true;
}

bool FocusNavigator::focusIsLive() const
{
    if (!m_window)
        return true;
    QQuickItem *focus = m_window->activeFocusItem();
    if (!isLive(focus))
        return false;
    // Inside a popup is always somewhere: popups are drawn over everything
    // and manage their own modality.
    QQuickItem *scope = scopeRootFor(focus);
    if (scope != m_window->contentItem())
        return true;
    return topModalScope() == nullptr;
}

QQuickItem *FocusNavigator::recoveryTarget(QQuickItem *scope) const
{
    if (QQuickItem *entry = duckItem(scope, "navEntry")) {
        if (isLive(entry))
            return entry;
    }
    // The window says where the keyboard belongs when it has to be put
    // somewhere (`navHome`: the page area, as against the chrome around it).
    // Without that, "nearest to where it was" is taken across the whole
    // window — and signing in, where the thing that had focus is a button on
    // a page that no longer exists, landed on whichever rail entry happened
    // to be closest to where that button had been.
    QQuickItem *home = scope == m_window->contentItem() ? duckItem(m_window, "navHome") : nullptr;
    if (home && !isLive(home))
        home = nullptr;
    QList<QQuickItem *> stops;
    if (home) {
        // A control that vanished from under the cursor, on a page that is
        // still there: the nearest thing on that page. Anything else — the
        // page itself went, or focus was out in the chrome — is the page's
        // own to seat, and it is simply handed the keyboard.
        if (!m_lastLive || !isAncestorOf(home, m_lastLive))
            return home;
        collectStops(home, &stops);
        if (stops.isEmpty())
            return home;
    } else {
        collectStops(scope, &stops);
    }
    if (stops.isEmpty())
        return nullptr;
    if (!m_lastRect.isValid())
        return stops.constFirst();
    QList<QRectF> rects;
    rects.reserve(stops.size());
    for (QQuickItem *stop : std::as_const(stops))
        rects.append(sceneRect(stop));
    return stops.at(pickNearest(m_lastRect, rects));
}

bool FocusNavigator::recover()
{
    if (!m_window || focusIsLive())
        return false;
    QQuickItem *scope = topModalScope();
    if (!scope)
        scope = m_window->contentItem();
    QQuickItem *target = recoveryTarget(scope);
    if (!target) {
        qCDebug(logFocus) << "recover: nothing to focus in" << scope;
        return false;
    }
    qCDebug(logFocus) << "recover:" << m_window->activeFocusItem() << "->" << target;
    give(target, Qt::OtherFocusReason);
    emit recovered(target);
    return true;
}

// ── Watching ────────────────────────────────────────────────────────────────

void FocusNavigator::watch(QQuickItem *item)
{
    if (m_watched == item)
        return;
    if (m_watched)
        m_watched->disconnect(this);
    m_watched = item;
    if (!item)
        return;
    // Hiding the focused item does not take focus off it (Qt keeps
    // activeFocus on an invisible item and still delivers Return to it), so
    // the moment it stops being live has to be watched for.
    connect(item, &QQuickItem::visibleChanged, this, [this] { m_evaluate.start(); });
    connect(item, &QQuickItem::enabledChanged, this, [this] { m_evaluate.start(); });
}

// A modal surface is a boundary for keys as well as for candidates. Popups
// need it for a second reason: a popup accepts every key it is offered, so an
// arrow declined by its content never reaches the window's root item at all.
void FocusNavigator::guard(QQuickItem *scope)
{
    if (!scope || !m_window || scope == m_window->contentItem())
        return;
    m_guarded.removeIf([](const QPointer<QQuickItem> &item) { return item.isNull(); });
    if (m_guarded.contains(scope))
        return;
    m_guarded.append(scope);
    scope->installEventFilter(this);
    // Seen once, a modal surface is watched: it may be shown again without
    // anything handing it the keyboard.
    connect(scope, &QQuickItem::visibleChanged, this, [this] { m_evaluate.start(); });
}

void FocusNavigator::onActiveFocusItemChanged()
{
    QQuickItem *focus = m_window ? m_window->activeFocusItem() : nullptr;
    watch(focus);
    if (focus)
        guard(scopeRootFor(focus));
    m_evaluate.start();
}

void FocusNavigator::updateModalActive()
{
    bool modal = false;
    if (m_window) {
        QQuickItem *focus = m_window->activeFocusItem();
        modal = (focus && scopeRootFor(focus) != m_window->contentItem())
             || topModalScope() != nullptr;
    }
    if (modal == m_modalActive)
        return;
    m_modalActive = modal;
    emit modalActiveChanged();
}

void FocusNavigator::evaluate()
{
    if (!m_window)
        return;
    guard(topModalScope());
    updateModalActive();
    if (focusIsLive()) {
        m_recover.stop();
        QQuickItem *focus = m_window->activeFocusItem();
        m_lastRect = cursorRect(focus);
        m_lastLive = focus;
        if (!m_giving)
            reveal(focus);
        return;
    }
    // The page that just hid a control, closed a sheet or finished loading
    // usually re-seats focus itself within a frame or two. Give it that long.
    if (!m_recover.isActive())
        m_recover.start(m_recoveryDelayMs);
}

bool FocusNavigator::eventFilter(QObject *watched, QEvent *event)
{
    if (!m_window || event->type() != QEvent::KeyPress)
        return QObject::eventFilter(watched, event);
    auto *key = static_cast<QKeyEvent *>(event);
    NavDirection dir;
    const bool isDirection = key->modifiers() == Qt::NoModifier && directionFor(key->key(), &dir);

    if (watched == m_window) {
        // Before delivery. A key about to go to nothing is spent on getting
        // focus back; a direction is consumed doing so, since "move from
        // nowhere" has no meaning beyond "be somewhere".
        if (!focusIsLive()) {
            const bool acted = recover();
            if (acted && isDirection) {
                event->accept();
                return true;
            }
        } else if (QQuickItem *focus = m_window->activeFocusItem()) {
            // Where the cursor is as the key arrives: a view moves its
            // current item without focus changing hands.
            m_lastRect = cursorRect(focus);
            m_evaluate.start();
        }
        return QObject::eventFilter(watched, event);
    }

    if (m_forwarding)
        return QObject::eventFilter(watched, event);

    if (watched == m_window->contentItem()) {
        // The root item: every control declined this key.
        if (isDirection && move(key->key(), key->isAutoRepeat())) {
            event->accept();
            return true;
        }
        return QObject::eventFilter(watched, event);
    }

    // The edge of a modal surface: everything inside it declined the key.
    auto *boundary = qobject_cast<QQuickItem *>(watched);
    if (!boundary || !boundary->isVisible())
        return QObject::eventFilter(watched, event);
    const bool popup = isPopupItem(boundary);
    // Asked again each time: a sheet fading out is still visible and no
    // longer modal.
    if (!popup && !duck(boundary, "navModal").toBool())
        return QObject::eventFilter(watched, event);
    if (popup) {
        // The popup itself answers Esc and Tab and has no use for an arrow.
        if (isDirection) {
            move(key->key(), key->isAutoRepeat());
            event->accept();
            return true;
        }
        return QObject::eventFilter(watched, event);
    }
    // A `navModal` item may have key handlers of its own (its Esc, its
    // Return), and a filter runs before them — so it is given the key first,
    // and only what it also declines stops here.
    m_forwarding = true;
    event->accept();
    QCoreApplication::sendEvent(boundary, event);
    m_forwarding = false;
    if (!event->isAccepted() && isDirection)
        move(key->key(), key->isAutoRepeat());
    event->accept();
    return true;
}

} // namespace strmqt
