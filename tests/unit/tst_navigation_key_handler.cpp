// Do navigation actions invoked by id reach the focused item?
//
// The web remote and the gamepad no longer post keys themselves: they trigger
// an action, and the navigation ones come out of NavigationKeyHandler as the
// bound key delivered to the focused control. That has to hold with the app in
// the background (the phone is in the user's hand, the TV app is not active),
// and a typable binding must not type into a focused text field.

#include "WindowFocusKeeper.h"
#include "input/InputMap.h"
#include "input/NavigationKeyHandler.h"

#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <QWindow>

using strmqt::InputMap;
using strmqt::NavigationKeyHandler;

namespace {

class KeyRecorder : public QQuickItem
{
public:
    QList<int> presses;
    int releases = 0;
    bool acceptsText = false;

    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override
    {
        if (query == Qt::ImEnabled)
            return acceptsText;
        return QQuickItem::inputMethodQuery(query);
    }

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        presses.append(event->key());
        event->accept();
    }
    void keyReleaseEvent(QKeyEvent *event) override
    {
        ++releases;
        event->accept();
    }
};

} // namespace

class TestNavigationKeyHandler : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        QVERIFY(m_dir.isValid());
        m_map = std::make_unique<InputMap>(m_dir.filePath(QStringLiteral("input.ini")));
        m_map->registerHandler(new NavigationKeyHandler(m_map.get(), m_map.get()));

        m_window = std::make_unique<QQuickWindow>();
        m_window->resize(200, 200);
        m_window->installEventFilter(new strmqt::WindowFocusKeeper(m_window.get()));
        m_item = new KeyRecorder;
        m_item->setParentItem(m_window->contentItem());
        m_item->setFlag(QQuickItem::ItemAcceptsInputMethod);
        m_item->setFocus(true);
        m_window->show();
        m_window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(m_window.get()));
        QVERIFY(m_item->hasActiveFocus());
    }

    void cleanup()
    {
        m_window.reset();
        m_map.reset();
    }

    void deliversTheBoundKeyToABackgroundWindow()
    {
        // Another application takes activation, leaving this process with no
        // focus window. Stand-in: activate a second window, then close it. (While
        // it is open it is itself the focus window, and rightly the target.)
        {
            QWindow other;
            other.resize(100, 100);
            other.show();
            other.requestActivate();
            QVERIFY(QTest::qWaitForWindowActive(&other));
        }
        QCoreApplication::processEvents();
        QVERIFY(!m_window->isActive());
        QVERIFY(!QGuiApplication::focusWindow());

        QVERIFY(m_map->trigger(QStringLiteral("nav.down")));
        QVERIFY(m_map->trigger(QStringLiteral("nav.back")));
        QCoreApplication::processEvents();

        QCOMPARE(m_item->presses,
                 (QList<int>{m_map->keyFor(QStringLiteral("nav.down")),
                             m_map->keyFor(QStringLiteral("nav.back"))}));
        QCOMPARE(m_item->releases, 2);
    }

    void leavesCommandsToOtherHandlers()
    {
        QVERIFY(!m_map->trigger(QStringLiteral("app.fullscreen")));
        QCoreApplication::processEvents();
        QVERIFY(m_item->presses.isEmpty());
    }

    void doesNotTypeIntoATextField()
    {
        m_item->acceptsText = true;
        const QString select = QStringLiteral("nav.select");
        // Rebind Select to a letter: in a text field that would be typing.
        QVERIFY(m_map->setBinding(select, QStringLiteral("K")));
        QVERIFY(m_map->trigger(select));
        // Arrows stay deliverable: they move the caret, they do not type.
        QVERIFY(m_map->trigger(QStringLiteral("nav.left")));
        QCoreApplication::processEvents();
        QCOMPARE(m_item->presses, QList<int>{Qt::Key_Left});
    }

private:
    QTemporaryDir m_dir;
    std::unique_ptr<InputMap> m_map;
    std::unique_ptr<QQuickWindow> m_window;
    KeyRecorder *m_item = nullptr;
};

QTEST_MAIN(TestNavigationKeyHandler)
#include "tst_navigation_key_handler.moc"
