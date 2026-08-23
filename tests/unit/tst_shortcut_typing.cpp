// Where does a window-scoped `Shortcut` sit relative to the item that has focus?
//
// Two things in this tree rest on the answer and they rest on different halves
// of it (ARCHITECTURE.md §5), so both halves are measured here. There is no
// input automation on this machine, so an offscreen probe is the strongest
// evidence available — the same reason tst_tap_modifiers exists.
//
// What was measured, on Qt 6.11.2:
//
//  1. A chord reaches its Shortcut past a focused, editable text field and the
//     field gets nothing. That is why MappedShortcut's chord half may stay live
//     inside a search box, which is the whole point of Ctrl+K.
//
//  2. A focused editable TextInput / TextField / TextArea **claims an
//     unmodified typable key for itself** — QQuickTextInput::event() accepts
//     the ShortcutOverride for any no-modifier-or-Shift key below
//     Qt::Key_Escape, plus Backspace/Delete/Home/End/Left/Right. So Space, "S"
//     and "/" land in the field and the Shortcut does not fire, whichever of
//     MappedShortcut's two halves the binding was sorted into. A review finding
//     claimed the opposite — that `music.playPause` on Space ate every space
//     typed into a playlist-name field — and it does not reproduce, on any of
//     the three text types. Read-only is the exception: QQuickTextInput ignores
//     ShortcutOverride when it cannot be typed into, which is exactly why
//     MappedShortcut keeps its own `_editingText` gate rather than trusting
//     this.
//
//  3. A focus item that is **not** a text item gets no such protection: the
//     Shortcut fires and the key never reaches the item's own Keys handler.
//     This is real and it is what killed StrmSelect's Space on a music page
//     with a queue loaded — the sort and genre dropdowns could not be opened
//     from the keyboard at all.
//
//  4. Accepting the ShortcutOverride event gets the key back, for that one
//     press only. That is StrmSelect's fix and the mechanism TrackTable's
//     type-to-jump has always used.

#include <QFile>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QTest>

namespace {

const char *kSource = R"QML(
import QtQuick
import QtQuick.Controls.Basic

Item {
    id: root

    width: 400
    height: 320

    property int shortcutFired: 0
    property int chordFired: 0
    property int plainItemSpaces: 0
    property int claimerSpaces: 0

    readonly property string inputText: input.text
    readonly property string fieldText: field.text
    readonly property string areaText: area.text

    function focusInput() { input.forceActiveFocus() }
    function focusField() { field.forceActiveFocus() }
    function focusArea() { area.forceActiveFocus() }
    function focusPlainItem() { plainItem.forceActiveFocus() }
    function focusClaimer() { claimer.forceActiveFocus() }
    function reset() {
        input.text = ""
        field.text = ""
        area.text = ""
        root.shortcutFired = 0
        root.chordFired = 0
        root.plainItemSpaces = 0
        root.claimerSpaces = 0
    }

    TextInput { id: input; y: 0;   width: parent.width; height: 40 }
    TextField { id: field; y: 50;  width: parent.width; height: 40 }
    TextArea  { id: area;  y: 100; width: parent.width; height: 60 }

    // A focusable item with no ShortcutOverride claim — every grid and card in
    // the app.
    Item {
        id: plainItem

        y: 180
        width: parent.width
        height: 40
        Keys.onSpacePressed: root.plainItemSpaces = root.plainItemSpaces + 1
    }

    // …and the same item once it claims the key, which is what StrmSelect does.
    Item {
        id: claimer

        y: 230
        width: parent.width
        height: 40
        Keys.onShortcutOverride: event => {
            if (event.key === Qt.Key_Space)
                event.accepted = true
        }
        Keys.onSpacePressed: root.claimerSpaces = root.claimerSpaces + 1
    }

    Shortcut {
        sequence: "Space"
        onActivated: root.shortcutFired = root.shortcutFired + 1
    }

    Shortcut {
        sequence: "Ctrl+K"
        onActivated: root.chordFired = root.chordFired + 1
    }
}
)QML";

} // namespace

class TestShortcutTyping : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void chordReachesItsShortcutPastAFocusedField();
    void aFocusedTextItemKeepsATypableKey_data();
    void aFocusedTextItemKeepsATypableKey();
    void aNonTextFocusItemLosesItToTheShortcut();
    void shortcutOverrideGetsTheKeyBack();

private:
    QTemporaryDir m_dir;
    QQuickView m_view;
    QQuickItem *m_root = nullptr;
};

void TestShortcutTyping::initTestCase()
{
    QVERIFY(m_dir.isValid());
    const QString path = m_dir.filePath(QStringLiteral("Probe.qml"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(kSource);
    file.close();

    m_view.setResizeMode(QQuickView::SizeRootObjectToView);
    m_view.setSource(QUrl::fromLocalFile(path));
    QVERIFY2(m_view.status() == QQuickView::Ready,
             qPrintable(m_view.errors().isEmpty() ? QStringLiteral("no root object")
                                                  : m_view.errors().first().toString()));
    m_root = m_view.rootObject();
    QVERIFY(m_root);
    m_view.resize(400, 320);
    m_view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&m_view));
}

void TestShortcutTyping::init()
{
    QMetaObject::invokeMethod(m_root, "reset");
}

void TestShortcutTyping::chordReachesItsShortcutPastAFocusedField()
{
    QMetaObject::invokeMethod(m_root, "focusField");
    QTest::keyClick(&m_view, Qt::Key_K, Qt::ControlModifier);
    QTRY_COMPARE(m_root->property("chordFired").toInt(), 1);
    QVERIFY(m_root->property("fieldText").toString().isEmpty());
}

void TestShortcutTyping::aFocusedTextItemKeepsATypableKey_data()
{
    QTest::addColumn<QString>("focusFn");
    QTest::addColumn<QString>("textProperty");
    QTest::newRow("TextInput") << QStringLiteral("focusInput") << QStringLiteral("inputText");
    QTest::newRow("TextField") << QStringLiteral("focusField") << QStringLiteral("fieldText");
    QTest::newRow("TextArea") << QStringLiteral("focusArea") << QStringLiteral("areaText");
}

void TestShortcutTyping::aFocusedTextItemKeepsATypableKey()
{
    QFETCH(QString, focusFn);
    QFETCH(QString, textProperty);

    QMetaObject::invokeMethod(m_root, focusFn.toUtf8().constData());
    QTest::keyClick(&m_view, Qt::Key_Space);
    // The space is typed, and the window Shortcut on "Space" never fires.
    QTRY_COMPARE(m_root->property(textProperty.toUtf8().constData()).toString(),
                 QStringLiteral(" "));
    QCOMPARE(m_root->property("shortcutFired").toInt(), 0);
}

void TestShortcutTyping::aNonTextFocusItemLosesItToTheShortcut()
{
    QMetaObject::invokeMethod(m_root, "focusPlainItem");
    QTest::keyClick(&m_view, Qt::Key_Space);
    QTRY_COMPARE(m_root->property("shortcutFired").toInt(), 1);
    // Keys.onSpacePressed never ran: the shortcut map consumed the press before
    // it was delivered anywhere. StrmSelect's dropdown was in exactly here.
    QCOMPARE(m_root->property("plainItemSpaces").toInt(), 0);
}

void TestShortcutTyping::shortcutOverrideGetsTheKeyBack()
{
    QMetaObject::invokeMethod(m_root, "focusClaimer");
    QTest::keyClick(&m_view, Qt::Key_Space);
    QTRY_COMPARE(m_root->property("claimerSpaces").toInt(), 1);
    QCOMPARE(m_root->property("shortcutFired").toInt(), 0);
}

QTEST_MAIN(TestShortcutTyping)
#include "tst_shortcut_typing.moc"
