// What happens to an arrow nobody claimed, and to focus nobody is holding
// (src/input/FocusNavigator.h), on a real QQuickWindow with real key events.
//
// The scenes are plain QtQuick on purpose: the navigator must work from what
// every item already says about itself — visible, enabled, a Tab stop, where
// it is — and not from anything a page has to remember to add.

#include "input/FocusNavigator.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

#include <memory>

namespace {

struct Scene
{
    QQmlEngine engine;
    QQuickWindow window;
    std::unique_ptr<QQuickItem> root;
    strmqt::FocusNavigator *navigator = nullptr;

    explicit Scene(const char *qml)
    {
        QQmlComponent component(&engine);
        component.setData(qml, QUrl(QStringLiteral("qrc:/scene.qml")));
        root.reset(qobject_cast<QQuickItem *>(component.create()));
        if (!root) {
            qWarning() << component.errorString();
            return;
        }
        window.resize(800, 600);
        root->setParentItem(window.contentItem());
        navigator = new strmqt::FocusNavigator(&window, &window);
        navigator->setRecoveryDelayMs(0);
        window.show();
        (void)QTest::qWaitForWindowExposed(&window);
    }

    QQuickItem *item(const char *name) const
    {
        return root->findChild<QQuickItem *>(QLatin1String(name));
    }
    QString focused() const
    {
        QQuickItem *focus = window.activeFocusItem();
        return focus ? focus->objectName() : QStringLiteral("<none>");
    }
    void focus(const char *name) { item(name)->forceActiveFocus(); }
    void press(Qt::Key key) { QTest::keyClick(&window, key); }
};

} // namespace

class FocusNavigatorTest : public QObject
{
    Q_OBJECT

private slots:
    void declinedArrowMovesToTheNearestControl();
    void claimedArrowIsLeftAlone();
    void hiddenAndDisabledControlsAreSkipped();
    void explicitNeighbourWinsWhileItIsLive();
    void regionStatesWhereItsEdgeLeads();
    void namedChainSkipsWhatIsNotThere();
    void containerFocusStepsInside();
    void hiddenFocusRecoversToTheNearestControl();
    void destroyedFocusRecovers();
    void modalSurfaceConfinesAndReclaims();
    void popupConfinesNavigation();
    void heldDirectionStopsAtAViewEdge();
    void focusedControlIsScrolledIntoView();
    void viewCursorIsTheSource();
};

void FocusNavigatorTest::declinedArrowMovesToTheNearestControl()
{
    Scene s(R"(
        import QtQuick
        Item {
            width: 800; height: 600
            Item { objectName: "a"; x: 0;   y: 0;   width: 100; height: 40; activeFocusOnTab: true }
            Item { objectName: "b"; x: 200; y: 0;   width: 100; height: 40; activeFocusOnTab: true }
            Item { objectName: "c"; x: 0;   y: 200; width: 100; height: 40; activeFocusOnTab: true }
            Item { objectName: "plain"; x: 400; y: 0; width: 100; height: 40 }
        })");
    QVERIFY(s.root);
    s.focus("a");
    s.press(Qt::Key_Right);
    QCOMPARE(s.focused(), QStringLiteral("b"));
    // Nothing further right: the item that is not a Tab stop is not a target.
    s.press(Qt::Key_Right);
    QCOMPARE(s.focused(), QStringLiteral("b"));
    s.press(Qt::Key_Left);
    QCOMPARE(s.focused(), QStringLiteral("a"));
    s.press(Qt::Key_Down);
    QCOMPARE(s.focused(), QStringLiteral("c"));
    s.press(Qt::Key_Up);
    QCOMPARE(s.focused(), QStringLiteral("a"));
}

void FocusNavigatorTest::claimedArrowIsLeftAlone()
{
    Scene s(R"(
        import QtQuick
        Item {
            width: 800; height: 600
            Item {
                objectName: "a"; width: 100; height: 40; activeFocusOnTab: true
                property int rights: 0
                Keys.onRightPressed: rights++
            }
            Item { objectName: "b"; x: 200; width: 100; height: 40; activeFocusOnTab: true }
        })");
    QVERIFY(s.root);
    s.focus("a");
    s.press(Qt::Key_Right);
    QCOMPARE(s.focused(), QStringLiteral("a"));
    QCOMPARE(s.item("a")->property("rights").toInt(), 1);
}

void FocusNavigatorTest::hiddenAndDisabledControlsAreSkipped()
{
    Scene s(R"(
        import QtQuick
        Item {
            width: 800; height: 600
            Item { objectName: "a"; width: 100; height: 40; activeFocusOnTab: true }
            Item { objectName: "hidden";   x: 150; width: 100; height: 40; activeFocusOnTab: true; visible: false }
            Item { objectName: "disabled"; x: 300; width: 100; height: 40; activeFocusOnTab: true; enabled: false }
            Item { x: 450; width: 100; height: 40; opacity: 0
                Item { objectName: "transparent"; anchors.fill: parent; activeFocusOnTab: true } }
            Item { objectName: "optedOut"; x: 550; width: 40; height: 40; activeFocusOnTab: true
                   property bool navStop: false }
            Item { objectName: "b"; x: 600; width: 100; height: 40; activeFocusOnTab: true }
        })");
    QVERIFY(s.root);
    s.focus("a");
    s.press(Qt::Key_Right);
    QCOMPARE(s.focused(), QStringLiteral("b"));
}

void FocusNavigatorTest::explicitNeighbourWinsWhileItIsLive()
{
    Scene s(R"(
        import QtQuick
        Item {
            width: 800; height: 600
            Item { objectName: "a"; width: 100; height: 40; activeFocusOnTab: true
                   property Item navDown: far }
            Item { objectName: "near"; y: 100; width: 100; height: 40; activeFocusOnTab: true }
            Item { id: far; objectName: "far"; x: 600; y: 400; width: 100; height: 40; activeFocusOnTab: true }
        })");
    QVERIFY(s.root);
    s.focus("a");
    s.press(Qt::Key_Down);
    QCOMPARE(s.focused(), QStringLiteral("far"));

    // A neighbour that is not there is not a dead end: geometry answers.
    s.item("far")->setVisible(false);
    s.focus("a");
    s.press(Qt::Key_Down);
    QCOMPARE(s.focused(), QStringLiteral("near"));
}

void FocusNavigatorTest::regionStatesWhereItsEdgeLeads()
{
    // The page says "below me is the docked bar". That is about the page's
    // bottom edge: Down from a control with more page under it stays in the
    // page, and only the last one leaves.
    Scene s(R"(
        import QtQuick
        Item {
            width: 800; height: 600
            Item {
                objectName: "page"
                width: 800; height: 400
                property Item navDown: bar
                Item { objectName: "hero";  x: 600; y: 0;   width: 100; height: 40; activeFocusOnTab: true }
                Item { objectName: "shelf"; x: 0;   y: 200; width: 100; height: 40; activeFocusOnTab: true }
            }
            Item { objectName: "nearer"; x: 600; y: 420; width: 100; height: 40; activeFocusOnTab: true }
            FocusScope {
                id: bar
                objectName: "bar"
                y: 500; width: 800; height: 60
                activeFocusOnTab: true
                property Item navEntry: play
                Item { objectName: "stop"; width: 60; height: 60; focus: true }
                Item { id: play; objectName: "play"; x: 100; width: 60; height: 60 }
            }
        })");
    QVERIFY(s.root);
    s.focus("hero");
    s.press(Qt::Key_Down);
    QCOMPARE(s.focused(), QStringLiteral("shelf"));
    // The edge: the named neighbour beats what geometry alone would pick, and
    // the bar is entered by the part it names rather than the part it had last.
    s.press(Qt::Key_Down);
    QCOMPARE(s.focused(), QStringLiteral("play"));
}

void FocusNavigatorTest::namedChainSkipsWhatIsNotThere()
{
    Scene s(R"(
        import QtQuick
        Item {
            width: 800; height: 600
            Item { objectName: "a"; width: 60; height: 40; property Item navRight: b }
            Item { id: b; objectName: "b"; x: 100; width: 60; height: 40; visible: false
                   property Item navRight: c }
            Item { id: c; objectName: "c"; x: 200; width: 60; height: 40 }
            Item { objectName: "stopFarRight"; x: 600; width: 60; height: 40; activeFocusOnTab: true }
        })");
    QVERIFY(s.root);
    s.focus("a");
    s.press(Qt::Key_Right);
    QCOMPARE(s.focused(), QStringLiteral("c"));
    // Nothing named beyond the end of the chain: geometry takes over.
    s.item("c")->setVisible(false);
    s.focus("a");
    s.press(Qt::Key_Right);
    QCOMPARE(s.focused(), QStringLiteral("stopFarRight"));
}

void FocusNavigatorTest::containerFocusStepsInside()
{
    // A page whose content failed to load holds the keyboard itself; its
    // Retry button is a Tab stop inside it and nothing wired an arrow to it.
    Scene s(R"(
        import QtQuick
        Item {
            width: 800; height: 600
            Item { objectName: "outside"; width: 100; height: 40; activeFocusOnTab: true }
            FocusScope {
                objectName: "page"; y: 100; width: 800; height: 500
                Item { objectName: "retry"; x: 300; y: 200; width: 100; height: 40; activeFocusOnTab: true }
            }
        })");
    QVERIFY(s.root);
    s.focus("page");
    QCOMPARE(s.focused(), QStringLiteral("page"));
    s.press(Qt::Key_Down);
    QCOMPARE(s.focused(), QStringLiteral("retry"));
}

void FocusNavigatorTest::hiddenFocusRecoversToTheNearestControl()
{
    Scene s(R"(
        import QtQuick
        Item {
            width: 800; height: 600
            Item { objectName: "far";  x: 600; y: 500; width: 100; height: 40; activeFocusOnTab: true }
            Item { objectName: "a";    x: 0;   y: 0;   width: 100; height: 40; activeFocusOnTab: true }
            Item { objectName: "near"; x: 0;   y: 60;  width: 100; height: 40; activeFocusOnTab: true }
        })");
    QVERIFY(s.root);
    QSignalSpy recovered(s.navigator, &strmqt::FocusNavigator::recovered);
    s.focus("a");
    QTRY_VERIFY(s.navigator->focusIsLive());
    QCoreApplication::processEvents(); // let the navigator note where focus is

    // Qt leaves active focus on an item that is hidden. The navigator does not.
    s.item("a")->setVisible(false);
    QTRY_COMPARE(s.focused(), QStringLiteral("near"));
    QCOMPARE(recovered.count(), 1);

    s.item("near")->setEnabled(false);
    QTRY_VERIFY(s.focused() == QStringLiteral("far"));
}

void FocusNavigatorTest::destroyedFocusRecovers()
{
    Scene s(R"(
        import QtQuick
        Item {
            width: 800; height: 600
            Item { objectName: "a"; width: 100; height: 40; activeFocusOnTab: true }
            Item { objectName: "b"; x: 200; width: 100; height: 40; activeFocusOnTab: true }
        })");
    QVERIFY(s.root);
    s.focus("a");
    delete s.item("a");
    QTRY_COMPARE(s.focused(), QStringLiteral("b"));
}

void FocusNavigatorTest::modalSurfaceConfinesAndReclaims()
{
    Scene s(R"(
        import QtQuick
        Item {
            width: 800; height: 600
            property int escapes: 0
            Keys.onEscapePressed: escapes++
            Item { objectName: "page"; width: 100; height: 40; activeFocusOnTab: true }
            FocusScope {
                id: sheet
                objectName: "sheet"
                x: 200; y: 200; width: 400; height: 200
                visible: false
                property bool navModal: true
                property Item navEntry: ok
                property int returns: 0
                Keys.onReturnPressed: returns++
                Item { objectName: "cancel"; width: 100; height: 40; activeFocusOnTab: true }
                Item { id: ok; objectName: "ok"; x: 200; width: 100; height: 40; activeFocusOnTab: true }
            }
        })");
    QVERIFY(s.root);
    s.focus("page");
    QVERIFY(!s.navigator->modalActive());

    QCoreApplication::processEvents();

    // The sheet appears and nobody hands it the keyboard. The first key finds
    // focus outside it and is spent putting it inside.
    s.item("sheet")->setVisible(true);
    QCOMPARE(s.focused(), QStringLiteral("page"));
    s.press(Qt::Key_Right);
    QCOMPARE(s.focused(), QStringLiteral("ok"));
    QTRY_VERIFY(s.navigator->modalActive());

    s.press(Qt::Key_Left);
    QCOMPARE(s.focused(), QStringLiteral("cancel"));
    // Up and Left lead to the page geometrically, and must not.
    s.press(Qt::Key_Up);
    QCOMPARE(s.focused(), QStringLiteral("cancel"));
    s.press(Qt::Key_Left);
    QCOMPARE(s.focused(), QStringLiteral("cancel"));
    // Nor may any other key it did not claim reach what is underneath.
    s.press(Qt::Key_Escape);
    QCOMPARE(s.root->property("escapes").toInt(), 0);
    QCOMPARE(s.item("sheet")->property("returns").toInt(), 0);
    s.press(Qt::Key_Return);
    QCOMPARE(s.item("sheet")->property("returns").toInt(), 1);

    // Seen once, it is watched: shown again, it takes the keyboard unasked.
    s.item("sheet")->setVisible(false);
    QTRY_COMPARE(s.focused(), QStringLiteral("page"));
    s.item("sheet")->setVisible(true);
    QTRY_COMPARE(s.focused(), QStringLiteral("ok"));

    // Something pushes focus out from under the scrim; it comes back.
    s.focus("page");
    QTRY_VERIFY(s.focused() == QStringLiteral("ok"));

    s.item("sheet")->setVisible(false);
    QTRY_COMPARE(s.focused(), QStringLiteral("page"));
    QTRY_VERIFY(!s.navigator->modalActive());
}

void FocusNavigatorTest::popupConfinesNavigation()
{
    Scene s(R"(
        import QtQuick
        import QtQuick.Controls.Basic
        Item {
            width: 800; height: 600
            Item { objectName: "page"; width: 100; height: 40; activeFocusOnTab: true }
            Popup {
                id: popup
                objectName: "popup"
                x: 200; y: 200; width: 400; height: 200
                focus: true
                padding: 0
                contentItem: Item {
                    Item { objectName: "one"; width: 100; height: 40; activeFocusOnTab: true; focus: true }
                    Item { objectName: "two"; x: 200; width: 100; height: 40; activeFocusOnTab: true }
                }
            }
            function open() { popup.open() }
        })");
    QVERIFY(s.root);
    s.focus("page");
    QMetaObject::invokeMethod(s.root.get(), "open");
    QTRY_COMPARE(s.focused(), QStringLiteral("one"));
    QTRY_VERIFY(s.navigator->modalActive());
    s.press(Qt::Key_Right);
    QCOMPARE(s.focused(), QStringLiteral("two"));
    s.press(Qt::Key_Up);
    QCOMPARE(s.focused(), QStringLiteral("two"));
    s.press(Qt::Key_Left);
    QCOMPARE(s.focused(), QStringLiteral("one"));
    s.press(Qt::Key_Left);
    QCOMPARE(s.focused(), QStringLiteral("one"));
}

void FocusNavigatorTest::heldDirectionStopsAtAViewEdge()
{
    Scene s(R"(
        import QtQuick
        Item {
            width: 800; height: 600
            ListView {
                objectName: "list"
                width: 300; height: 60
                orientation: ListView.Horizontal
                activeFocusOnTab: true
                model: 3
                delegate: Item { width: 100; height: 60 }
            }
            Item { objectName: "beside"; x: 400; width: 100; height: 60; activeFocusOnTab: true }
            Item { objectName: "below";  y: 200; width: 100; height: 60; activeFocusOnTab: true }
        })");
    QVERIFY(s.root);
    s.focus("list");
    QQuickItem *list = s.item("list");
    list->setProperty("currentIndex", 1);

    // Still held when the list ran out: an edge, not somewhere to keep going.
    QTest::keyPress(&s.window, Qt::Key_Right);
    QCOMPARE(list->property("currentIndex").toInt(), 2);
    QTest::simulateEvent(&s.window, true, Qt::Key_Right, Qt::NoModifier, QString(), true);
    QVERIFY(list->hasActiveFocus());
    QTest::keyRelease(&s.window, Qt::Key_Right);

    // Across the list's axis a hold is just walking the page.
    QTest::simulateEvent(&s.window, true, Qt::Key_Down, Qt::NoModifier, QString(), true);
    QCOMPARE(s.focused(), QStringLiteral("below"));

    s.focus("list");
    s.press(Qt::Key_Right);
    QCOMPARE(s.focused(), QStringLiteral("beside"));
}

void FocusNavigatorTest::focusedControlIsScrolledIntoView()
{
    Scene s(R"(
        import QtQuick
        Flickable {
            objectName: "flick"
            width: 800; height: 600
            contentWidth: 800; contentHeight: 3000
            Item { objectName: "top";    y: 0;    width: 100; height: 40; activeFocusOnTab: true }
            Item { objectName: "bottom"; y: 2000; width: 100; height: 40; activeFocusOnTab: true }
        })");
    QVERIFY(s.root);
    s.focus("top");
    s.press(Qt::Key_Down);
    QCOMPARE(s.focused(), QStringLiteral("bottom"));
    const qreal contentY = s.root->property("contentY").toReal();
    QVERIFY2(contentY > 2000 + 40 - 600 && contentY < 2000, qPrintable(QString::number(contentY)));
    s.press(Qt::Key_Up);
    QCOMPARE(s.focused(), QStringLiteral("top"));
    QCOMPARE(s.root->property("contentY").toReal(), 0.0);
}

void FocusNavigatorTest::viewCursorIsTheSource()
{
    // Up from the third card of a shelf lands on what is above the third card,
    // not on what is above the middle of the shelf.
    Scene s(R"(
        import QtQuick
        Item {
            width: 800; height: 600
            Item { objectName: "left";  x: 0;   width: 100; height: 40; activeFocusOnTab: true }
            Item { objectName: "right"; x: 600; width: 100; height: 40; activeFocusOnTab: true }
            ListView {
                objectName: "list"
                y: 100; width: 800; height: 100
                orientation: ListView.Horizontal
                activeFocusOnTab: true
                model: 8
                delegate: Item { width: 100; height: 100 }
            }
        })");
    QVERIFY(s.root);
    s.focus("list");
    s.item("list")->setProperty("currentIndex", 6);
    s.press(Qt::Key_Up);
    QCOMPARE(s.focused(), QStringLiteral("right"));
    s.focus("list");
    s.item("list")->setProperty("currentIndex", 0);
    s.press(Qt::Key_Up);
    QCOMPARE(s.focused(), QStringLiteral("left"));
}

QTEST_MAIN(FocusNavigatorTest)
#include "tst_focus_navigator.moc"
