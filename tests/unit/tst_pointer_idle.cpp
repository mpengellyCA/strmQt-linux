// A pointer nobody is moving stops hovering things and stops being drawn
// (src/input/PointerIdle.h).

#include "input/PointerIdle.h"

#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QtTest>

class PointerIdleTest : public QObject
{
    Q_OBJECT

private slots:
    void idlePointerLetsGoAndHides();
    void restingJitterDoesNotWake();
    void aHeldButtonIsInUse();
};

namespace {

struct Scene
{
    QQmlEngine engine;
    QQuickWindow window;
    QScopedPointer<QQuickItem> root;
    strmqt::PointerIdle *idle = nullptr;

    Scene()
    {
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            Item {
                width: 400; height: 300
                Item {
                    id: card
                    objectName: "card"
                    x: 50; y: 50; width: 100; height: 100
                    property alias hovered: hover.hovered
                    HoverHandler { id: hover }
                }
                // Slides under a pointer that is not moving, as a row does
                // when a pad scrolls the page.
                Item {
                    objectName: "passing"
                    x: 300; y: 50; width: 100; height: 100
                    property alias hovered: passingHover.hovered
                    HoverHandler { id: passingHover }
                }
            })",
                          QUrl(QStringLiteral("qrc:/scene.qml")));
        root.reset(qobject_cast<QQuickItem *>(component.create()));
        if (!root) {
            qWarning() << component.errorString();
            return;
        }
        window.resize(400, 300);
        root->setParentItem(window.contentItem());
        idle = new strmqt::PointerIdle(&window, &window);
        idle->setIdleMs(60);
        window.show();
        (void)QTest::qWaitForWindowExposed(&window);
    }

    bool hovered(const char *name) const
    {
        return root->findChild<QQuickItem *>(QLatin1String(name))->property("hovered").toBool();
    }
};

bool cursorBlanked()
{
    const QCursor *cursor = QGuiApplication::overrideCursor();
    return cursor && cursor->shape() == Qt::BlankCursor;
}

} // namespace

void PointerIdleTest::idlePointerLetsGoAndHides()
{
    Scene s;
    QVERIFY(s.root);
    QTest::mouseMove(&s.window, QPoint(80, 80));
    QTest::mouseMove(&s.window, QPoint(100, 100));
    QTRY_VERIFY(s.hovered("card"));
    QVERIFY(!s.idle->parked());
    QVERIFY(!cursorBlanked());

    QTRY_VERIFY_WITH_TIMEOUT(s.idle->parked(), 2000);
    QVERIFY(cursorBlanked());
    QTRY_VERIFY(!s.hovered("card"));

    // The scene moves under the parked pointer. Nothing lights up.
    QQuickItem *passing = s.root->findChild<QQuickItem *>(QStringLiteral("passing"));
    passing->setX(50);
    s.window.update();
    QTest::qWait(120);
    QVERIFY(!s.hovered("passing"));
    QVERIFY(!s.hovered("card"));

    // Real movement brings both back.
    QTest::mouseMove(&s.window, QPoint(120, 120));
    QVERIFY(!s.idle->parked());
    QVERIFY(!cursorBlanked());
    QTRY_VERIFY(s.hovered("card") || s.hovered("passing"));
}

void PointerIdleTest::restingJitterDoesNotWake()
{
    Scene s;
    QVERIFY(s.root);
    QTest::mouseMove(&s.window, QPoint(100, 100));
    QTRY_VERIFY_WITH_TIMEOUT(s.idle->parked(), 2000);

    // A mouse at rest still reports the odd count.
    QTest::mouseMove(&s.window, QPoint(101, 100));
    QTest::mouseMove(&s.window, QPoint(100, 101));
    QVERIFY(s.idle->parked());
    QVERIFY(cursorBlanked());
    QVERIFY(!s.hovered("card"));

    QTest::mouseMove(&s.window, QPoint(110, 110));
    QVERIFY(!s.idle->parked());
}

void PointerIdleTest::aHeldButtonIsInUse()
{
    Scene s;
    QVERIFY(s.root);
    QTest::mouseMove(&s.window, QPoint(100, 100));
    QTest::mousePress(&s.window, Qt::LeftButton, {}, QPoint(100, 100));
    QTest::qWait(250);
    QVERIFY(!s.idle->parked());
    QTest::mouseRelease(&s.window, Qt::LeftButton, {}, QPoint(100, 100));
    QTRY_VERIFY_WITH_TIMEOUT(s.idle->parked(), 2000);
}

QTEST_MAIN(PointerIdleTest)
#include "tst_pointer_idle.moc"
