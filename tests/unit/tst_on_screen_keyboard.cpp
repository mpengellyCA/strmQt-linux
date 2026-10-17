// The on-screen keyboard (src/input/OnScreenKeyboard.h): its layout, the walk
// over it, and what it does to a real TextInput on a real QQuickWindow.
//
// A pad, the phone and a remote's OK reach the window as keys the window system
// did not send, so they are staged here with sendEvent (not spontaneous). A
// real keyboard is QTest's key events, which go through the window system
// interface and are spontaneous.

#include "input/KeyDelivery.h"
#include "input/OnScreenKeyboard.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "QmlShimStaging.h"

#include <memory>

using strmqt::OnScreenKeyboard;

namespace {

// `seen` records every key that got past the field to the page.
const char *const kScene = R"(
import QtQuick
Item {
    id: root
    width: 800; height: 600
    property var seen: []
    property int edits: 0
    property int accepts: 0
    Keys.onPressed: event => { root.seen = root.seen.concat([event.key]); }
    TextInput {
        objectName: "field"
        width: 300; height: 40
        onTextEdited: root.edits++
        onAccepted: root.accepts++
    }
    TextInput {
        objectName: "other"
        x: 400; width: 300; height: 40
    }
    TextInput {
        objectName: "password"
        y: 100; width: 300; height: 40
        echoMode: TextInput.Password
    }
    TextInput {
        objectName: "readOnly"
        y: 200; width: 300; height: 40
        readOnly: true
        text: "fixed"
    }
    Item {
        property bool onScreenKeyboard: false
        y: 300; width: 300; height: 40
        TextInput {
            objectName: "optedOut"
            anchors.fill: parent
            onAccepted: root.accepts++
        }
    }
}
)";

struct Scene
{
    QQmlEngine engine;
    QQuickWindow window;
    std::unique_ptr<QQuickItem> root;
    OnScreenKeyboard keyboard;

    Scene()
    {
        QQmlComponent component(&engine);
        component.setData(kScene, QUrl(QStringLiteral("qrc:/scene.qml")));
        root.reset(qobject_cast<QQuickItem *>(component.create()));
        if (!root) {
            qWarning() << component.errorString();
            return;
        }
        window.resize(800, 600);
        root->setParentItem(window.contentItem());
        keyboard.attach(&window);
        window.show();
        window.requestActivate();
        (void)QTest::qWaitForWindowActive(&window);
    }

    QQuickItem *item(const char *name) const
    {
        return root->findChild<QQuickItem *>(QLatin1String(name));
    }
    QString text(const char *name) const { return item(name)->property("text").toString(); }
    void focus(const char *name) { item(name)->forceActiveFocus(); }

    // A key from the pad, the phone or a remote: not from the window system.
    void pad(Qt::Key key)
    {
        strmqt::keydelivery::DeliveredKey press(QEvent::KeyPress, key, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &press);
        strmqt::keydelivery::DeliveredKey release(QEvent::KeyRelease, key, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &release);
    }
    void pad(Qt::Key key, int times)
    {
        for (int i = 0; i < times; ++i)
            pad(key);
    }
    // A key from a real keyboard.
    void type(Qt::Key key) { QTest::keyClick(&window, key); }
    void type(char character) { QTest::keyClick(&window, character); }

    QList<int> seen() const
    {
        QList<int> keys;
        const QVariantList list = root->property("seen").toList();
        for (const QVariant &key : list)
            keys.append(key.toInt());
        return keys;
    }
    int counter(const char *name) const { return root->property(name).toInt(); }
};

// Repeater delegates are not QObject children of what they are drawn in, so
// the drawn keys are found by walking the items.
QHash<QString, QQuickItem *> drawnKeys(QQuickItem *item)
{
    QHash<QString, QQuickItem *> keys;
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children) {
        if (child->objectName().startsWith(QLatin1String("key-")))
            keys.insert(child->objectName(), child);
        keys.insert(drawnKeys(child));
    }
    return keys;
}

} // namespace

class OnScreenKeyboardTest : public QObject
{
    Q_OBJECT

private slots:
    // Layout and walk.
    void everyPrintableCharacterCanBeTyped();
    void everyRowIsTheSameWidth();
    void stepsStopAtTheEdges();
    void verticalStepKeepsTheColumn();

    // On a window.
    void padSelectOpensAndTypes();
    void keyboardReturnStillSubmits();
    void backClosesAndKeepsTheText();
    void doneSubmitsOnceAndCloses();
    void shiftIsForOneLetter();
    void symbolsLayerTypesSymbols();
    void editsThatTheFieldDeclinesStayInTheField();
    void otherPadKeysDoNotReachThePage();
    void realKeyboardTakesOver();
    void focusLeavingTheFieldCloses();
    void passwordIsShownAsBullets();
    void readOnlyAndOptedOutFieldsAreLeftAlone();
    void pointerPressTypesTheKey();

    // The drawn keyboard (src/ui/shell/OnScreenKeyboard.qml).
    void panelDrawsTheKeysAndAClickTypes();
};

void OnScreenKeyboardTest::everyPrintableCharacterCanBeTyped()
{
    QSet<QChar> typable;
    for (const bool symbols : {false, true}) {
        for (const bool shifted : {false, true}) {
            for (const auto &row : OnScreenKeyboard::layoutRows(symbols, shifted)) {
                for (const auto &key : row) {
                    if (!key.text.isEmpty())
                        typable.insert(key.text.at(0));
                }
            }
        }
    }
    for (char16_t code = 0x20; code < 0x7f; ++code)
        QVERIFY2(typable.contains(QChar(code)), qPrintable(QString(QChar(code))));
}

void OnScreenKeyboardTest::everyRowIsTheSameWidth()
{
    for (const bool symbols : {false, true}) {
        for (const auto &row : OnScreenKeyboard::layoutRows(symbols, false)) {
            double width = 0.0;
            for (const auto &key : row)
                width += key.width;
            QCOMPARE(width, OnScreenKeyboard::kRowWidth);
        }
    }
}

void OnScreenKeyboardTest::stepsStopAtTheEdges()
{
    const auto layout = OnScreenKeyboard::layoutRows(false, false);
    const OnScreenKeyboard::Cursor topLeft{0, 0, 0.5};
    QCOMPARE(OnScreenKeyboard::step(layout, topLeft, -1, 0), topLeft);
    QCOMPARE(OnScreenKeyboard::step(layout, topLeft, 0, -1), topLeft);
    const int last = int(layout.size()) - 1;
    const OnScreenKeyboard::Cursor bottomRight{last, int(layout.at(last).size()) - 1, 9.25};
    QCOMPARE(OnScreenKeyboard::step(layout, bottomRight, 1, 0), bottomRight);
    QCOMPARE(OnScreenKeyboard::step(layout, bottomRight, 0, 1), bottomRight);
}

void OnScreenKeyboardTest::verticalStepKeepsTheColumn()
{
    const auto layout = OnScreenKeyboard::layoutRows(false, false);
    // "v": the shift key, then z x c v.
    OnScreenKeyboard::Cursor cursor{3, 4, 4.5};
    QCOMPARE(layout.at(3).at(4).text, QStringLiteral("v"));
    cursor = OnScreenKeyboard::step(layout, cursor, 0, 1);
    QCOMPARE(layout.at(cursor.row).at(cursor.column).kind, OnScreenKeyboard::Kind::Space);
    cursor = OnScreenKeyboard::step(layout, cursor, 0, -1);
    QCOMPARE(layout.at(cursor.row).at(cursor.column).text, QStringLiteral("v"));
    // A horizontal step moves the column it aims for: b, then up.
    cursor = OnScreenKeyboard::step(layout, cursor, 1, 0);
    cursor = OnScreenKeyboard::step(layout, cursor, 0, -1);
    QCOMPARE(layout.at(cursor.row).at(cursor.column).text, QStringLiteral("h"));
}

void OnScreenKeyboardTest::padSelectOpensAndTypes()
{
    Scene scene;
    QVERIFY(scene.root);
    scene.focus("field");
    QSignalSpy opened(&scene.keyboard, &OnScreenKeyboard::openChanged);

    scene.pad(Qt::Key_Return);
    QVERIFY(scene.keyboard.isOpen());
    QCOMPARE(opened.count(), 1);
    QCOMPARE(scene.counter("accepts"), 0); // opening is not submitting
    QVERIFY(scene.seen().isEmpty());       // nor is its release seen

    // It starts on the first letter.
    scene.pad(Qt::Key_Return);
    scene.pad(Qt::Key_Down);
    scene.pad(Qt::Key_Return);
    QCOMPARE(scene.text("field"), QStringLiteral("qa"));
    QCOMPARE(scene.keyboard.text(), QStringLiteral("qa"));
    // Typed, as far as the field and the page can tell.
    QCOMPARE(scene.counter("edits"), 2);
    QVERIFY(scene.item("field")->hasActiveFocus());
    QVERIFY(scene.seen().isEmpty());
}

void OnScreenKeyboardTest::keyboardReturnStillSubmits()
{
    Scene scene;
    QVERIFY(scene.root);
    scene.focus("field");
    scene.type(Qt::Key_Return);
    QVERIFY(!scene.keyboard.isOpen());
    QCOMPARE(scene.counter("accepts"), 1);
}

void OnScreenKeyboardTest::backClosesAndKeepsTheText()
{
    Scene scene;
    QVERIFY(scene.root);
    scene.focus("field");
    scene.pad(Qt::Key_Return);
    scene.pad(Qt::Key_Return); // "q"
    scene.pad(Qt::Key_Escape);
    QVERIFY(!scene.keyboard.isOpen());
    QCOMPARE(scene.text("field"), QStringLiteral("q"));
    QVERIFY(scene.item("field")->hasActiveFocus());
    // Back closed the keyboard and went no further.
    QVERIFY(!scene.seen().contains(Qt::Key_Escape));
}

void OnScreenKeyboardTest::doneSubmitsOnceAndCloses()
{
    Scene scene;
    QVERIFY(scene.root);
    scene.focus("field");
    scene.pad(Qt::Key_Return);
    scene.pad(Qt::Key_Return); // "q"
    // q, a, shift, the symbols key on the bottom row; Done is the last key.
    scene.pad(Qt::Key_Down, 3);
    scene.pad(Qt::Key_Right, 5);
    QCOMPARE(scene.keyboard.rows().at(scene.keyboard.row()).toList()
                 .at(scene.keyboard.column()).toMap().value(QStringLiteral("kind")).toString(),
             QStringLiteral("done"));
    scene.pad(Qt::Key_Return);
    QVERIFY(!scene.keyboard.isOpen());
    QCOMPARE(scene.counter("accepts"), 1);
    QCOMPARE(scene.text("field"), QStringLiteral("q"));
    // The field declines Return after accepting, so the page sees it as it
    // would a keyboard's.
    QCOMPARE(scene.seen().count(Qt::Key_Return), 1);
}

void OnScreenKeyboardTest::shiftIsForOneLetter()
{
    Scene scene;
    QVERIFY(scene.root);
    scene.focus("field");
    scene.pad(Qt::Key_Return);
    scene.pad(Qt::Key_Down, 2); // shift
    scene.pad(Qt::Key_Return);
    QVERIFY(scene.keyboard.shifted());
    scene.pad(Qt::Key_Right); // Z
    scene.pad(Qt::Key_Return);
    QVERIFY(!scene.keyboard.shifted());
    scene.pad(Qt::Key_Right); // x
    scene.pad(Qt::Key_Return);
    QCOMPARE(scene.text("field"), QStringLiteral("Zx"));
}

void OnScreenKeyboardTest::symbolsLayerTypesSymbols()
{
    Scene scene;
    QVERIFY(scene.root);
    scene.focus("field");
    scene.pad(Qt::Key_Return);
    scene.pad(Qt::Key_Down, 3); // the symbols key
    scene.pad(Qt::Key_Return);
    QVERIFY(scene.keyboard.symbols());
    scene.pad(Qt::Key_Up, 3); // "!", where q was
    scene.pad(Qt::Key_Return);
    QCOMPARE(scene.text("field"), QStringLiteral("!"));
    // Opening again starts on the letters.
    scene.pad(Qt::Key_Escape);
    scene.pad(Qt::Key_Return);
    QVERIFY(!scene.keyboard.symbols());
}

void OnScreenKeyboardTest::editsThatTheFieldDeclinesStayInTheField()
{
    Scene scene;
    QVERIFY(scene.root);
    scene.focus("field");
    scene.pad(Qt::Key_Return);
    scene.pad(Qt::Key_Down, 3);
    scene.pad(Qt::Key_Right); // caret left, in an empty field
    scene.pad(Qt::Key_Return);
    scene.pad(Qt::Key_Right, 2); // caret right
    scene.pad(Qt::Key_Return);
    scene.pad(Qt::Key_Right); // backspace
    scene.pad(Qt::Key_Return);
    QVERIFY(scene.keyboard.isOpen());
    QVERIFY(scene.item("field")->hasActiveFocus());
    // An arrow the page saw could move the focus away, and Backspace is a
    // Back binding there.
    QVERIFY2(scene.seen().isEmpty(), qPrintable(QString::number(scene.seen().size())));
}

void OnScreenKeyboardTest::otherPadKeysDoNotReachThePage()
{
    Scene scene;
    QVERIFY(scene.root);
    scene.focus("field");
    scene.pad(Qt::Key_Return);
    scene.pad(Qt::Key_Menu); // a held A asks for the context menu
    QVERIFY(scene.keyboard.isOpen());
    QVERIFY(scene.seen().isEmpty());
}

void OnScreenKeyboardTest::realKeyboardTakesOver()
{
    Scene scene;
    QVERIFY(scene.root);
    scene.focus("field");
    scene.pad(Qt::Key_Return);
    scene.type('k');
    QVERIFY(!scene.keyboard.isOpen());
    QCOMPARE(scene.text("field"), QStringLiteral("k"));
}

void OnScreenKeyboardTest::focusLeavingTheFieldCloses()
{
    Scene scene;
    QVERIFY(scene.root);
    scene.focus("field");
    scene.pad(Qt::Key_Return);
    QVERIFY(scene.keyboard.isOpen());
    scene.focus("other");
    QVERIFY(!scene.keyboard.isOpen());
}

void OnScreenKeyboardTest::passwordIsShownAsBullets()
{
    Scene scene;
    QVERIFY(scene.root);
    scene.focus("password");
    scene.pad(Qt::Key_Return);
    scene.pad(Qt::Key_Return);
    scene.pad(Qt::Key_Right);
    scene.pad(Qt::Key_Return);
    QCOMPARE(scene.text("password"), QStringLiteral("qw"));
    QVERIFY(scene.keyboard.secret());
    QCOMPARE(scene.keyboard.text(), QString(2, QChar(0x2022)));
}

void OnScreenKeyboardTest::readOnlyAndOptedOutFieldsAreLeftAlone()
{
    Scene scene;
    QVERIFY(scene.root);
    scene.focus("readOnly");
    scene.pad(Qt::Key_Return);
    QVERIFY(!scene.keyboard.isOpen());

    scene.focus("optedOut");
    scene.pad(Qt::Key_Return);
    QVERIFY(!scene.keyboard.isOpen());
    // Its Return is the field's, as before.
    QCOMPARE(scene.counter("accepts"), 1);
}

void OnScreenKeyboardTest::pointerPressTypesTheKey()
{
    Scene scene;
    QVERIFY(scene.root);
    scene.focus("field");
    scene.keyboard.press(1, 1); // closed: nothing
    QCOMPARE(scene.text("field"), QString());
    scene.pad(Qt::Key_Return);
    scene.keyboard.press(1, 1);
    QCOMPARE(scene.text("field"), QStringLiteral("w"));
    QCOMPARE(scene.keyboard.row(), 1);
    QCOMPARE(scene.keyboard.column(), 1);
}

// The real panel, staged from the source tree with what it uses, over a field.
void OnScreenKeyboardTest::panelDrawsTheKeysAndAClickTypes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString modulePath = dir.filePath(QStringLiteral("StrmQt"));
    QVERIFY(QDir().mkpath(modulePath));
    const QList<std::pair<QString, QString>> files = {
        {QStringLiteral("/src/ui/"), QStringLiteral("Theme.qml")},
        {QStringLiteral("/src/ui/controls/"), QStringLiteral("FocusRing.qml")},
        {QStringLiteral("/src/ui/controls/"), QStringLiteral("StrmIcon.qml")},
        {QStringLiteral("/src/ui/shell/"), QStringLiteral("OnScreenKeyboard.qml")},
    };
    QByteArray qmldir = "module StrmQt\nsingleton Theme 1.0 Theme.qml\n";
    for (const auto &[from, name] : files) {
        QVERIFY(QFile::copy(QStringLiteral(STRMQT_SOURCE_DIR) + from + name,
                            modulePath + QLatin1Char('/') + name));
        if (name != QStringLiteral("Theme.qml"))
            qmldir += name.chopped(4).toUtf8() + " 1.0 " + name.toUtf8() + '\n';
    }
    const QByteArray shims = strmqt::test::stageShims(modulePath);
    QVERIFY(!shims.isEmpty());
    QFile qmldirFile(modulePath + QStringLiteral("/qmldir"));
    QVERIFY(qmldirFile.open(QIODevice::WriteOnly));
    qmldirFile.write(qmldir + shims);
    qmldirFile.close();

    QFile probe(dir.filePath(QStringLiteral("Probe.qml")));
    QVERIFY(probe.open(QIODevice::WriteOnly));
    // A window at the root, as Main.qml is: the panel finds the window's
    // overlay while it is being built.
    probe.write(R"(
import QtQuick
import StrmQt
Window {
    width: 1280; height: 720
    color: "#0C0B0A"
    TextInput { objectName: "field"; width: 300; height: 40 }
    OnScreenKeyboard { objectName: "panel" }
}
)");
    probe.close();

    OnScreenKeyboard keyboard;
    QQmlEngine engine;
    engine.addImportPath(dir.path());
    engine.rootContext()->setContextProperty(QStringLiteral("OnScreenKeyboardCtl"), &keyboard);
    QQmlComponent component(&engine, QUrl::fromLocalFile(probe.fileName()));
    std::unique_ptr<QQuickWindow> window(qobject_cast<QQuickWindow *>(component.create()));
    QVERIFY2(window, qPrintable(component.errorString()));
    keyboard.attach(window.get());
    window->show();
    window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(window.get()));
    QQuickWindow &view = *window;

    auto *field = view.findChild<QQuickItem *>(QStringLiteral("field"));
    QVERIFY(field);
    field->forceActiveFocus();
    // In the window's overlay, not where it was declared.
    auto *panel = view.findChild<QQuickItem *>(QStringLiteral("panel"));
    QVERIFY(panel);
    QVERIFY(panel->parentItem());
    QCOMPARE(panel->parentItem()->metaObject()->className(), "QQuickOverlay");
    QVERIFY(!panel->isVisible());

    strmqt::keydelivery::DeliveredKey select(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(&view, &select);
    QTRY_VERIFY(panel->isVisible());

    // Every key of the layout is drawn.
    int keys = 0;
    for (const QVariant &row : keyboard.rows())
        keys += int(row.toList().size());
    const QHash<QString, QQuickItem *> drawn = drawnKeys(panel);
    QCOMPARE(drawn.size(), keys);

    // A click on "w" types it, and the field keeps the focus.
    QQuickItem *w = drawn.value(QStringLiteral("key-1-1"));
    QVERIFY(w);
    const QPointF centre = w->mapToScene(QPointF(w->width() / 2, w->height() / 2));
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, centre.toPoint());
    QCOMPARE(field->property("text").toString(), QStringLiteral("w"));
    QVERIFY(field->hasActiveFocus());
    QCOMPARE(keyboard.row(), 1);
    QCOMPARE(keyboard.column(), 1);

    // For looking at it: the ring glides, so give it time to arrive.
    if (const QByteArray shot = qgetenv("STRMQT_TEST_KEYBOARD_SHOT"); !shot.isEmpty()) {
        QTest::qWait(500);
        view.grabWindow().save(QString::fromLocal8Bit(shot));
    }
}

QTEST_MAIN(OnScreenKeyboardTest)
#include "tst_on_screen_keyboard.moc"
