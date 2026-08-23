// Can a pointer handler tell Ctrl+Click from Click?
//
// TrackRow's multi-select rests entirely on the answer (MUSIC.md §7): Ctrl
// toggles a row into the selection and a plain click plays it, and the row has
// to know which happened. There is no input-automation tool on this machine, so
// this probe is the strongest evidence available that the mechanism works — and
// it was written because the obvious spelling does NOT.
//
// What was measured, on Qt 6.11:
//
//  · `TapHandler.tapped(eventPoint, button)` carries a **QEventPoint**, and
//    QEventPoint has no modifier state of any kind. `eventPoint.modifiers` is
//    `undefined`, silently — qmllint says "Member \"modifiers\" not found on
//    type \"QEventPoint\"" and the app then treats every click as unmodified.
//  · The handler's own `point` property is a **handlerPoint**
//    (QQuickHandlerPoint), which does carry `modifiers`, and it is up to date
//    by the time `tapped` is emitted.
//
// So the test asserts both halves: the handler's point reports the modifier,
// and the signal's own argument does not. The second assertion is the one that
// stops the "obvious" spelling from being reintroduced.

#include <QFile>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QTest>

namespace {

const char *kSource = R"QML(
import QtQuick

Rectangle {
    id: root

    width: 200
    height: 100
    color: "black"

    property int lastFromHandlerPoint: -1
    // Whether the signal's own eventPoint even HAS the property. Recorded as an
    // int so the test can tell "undefined" from "no modifiers".
    property bool signalPointHasModifiers: true
    property int taps: 0

    TapHandler {
        id: tap

        acceptedButtons: Qt.LeftButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: eventPoint => {
            root.lastFromHandlerPoint = tap.point.modifiers
            root.signalPointHasModifiers = eventPoint.modifiers !== undefined
            root.taps = root.taps + 1
        }
    }
}
)QML";

} // namespace

class TestTapModifiers : public QObject
{
    Q_OBJECT

private slots:
    void handlerPointCarriesModifiers_data();
    void handlerPointCarriesModifiers();
    void signalEventPointDoesNot();

private:
    void clickWith(Qt::KeyboardModifiers modifiers);
    QQuickView m_view;
    QQuickItem *m_root = nullptr;
};

void TestTapModifiers::clickWith(Qt::KeyboardModifiers modifiers)
{
    if (!m_root) {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("Probe.qml"));
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
        m_view.resize(200, 100);
        m_view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&m_view));
    }
    const QPoint centre(100, 50);
    QTest::mousePress(&m_view, Qt::LeftButton, modifiers, centre);
    QTest::mouseRelease(&m_view, Qt::LeftButton, modifiers, centre);
    QTRY_VERIFY(m_root->property("taps").toInt() > 0);
}

void TestTapModifiers::handlerPointCarriesModifiers_data()
{
    QTest::addColumn<int>("modifiers");
    QTest::newRow("none") << int(Qt::NoModifier);
    QTest::newRow("ctrl") << int(Qt::ControlModifier);
    QTest::newRow("shift") << int(Qt::ShiftModifier);
}

void TestTapModifiers::handlerPointCarriesModifiers()
{
    QFETCH(int, modifiers);
    clickWith(Qt::KeyboardModifiers(modifiers));
    // Only the modifier bits the row cares about: a platform is free to report
    // keypad/group flags nobody asked about.
    const int seen = m_root->property("lastFromHandlerPoint").toInt();
    QCOMPARE(seen & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier), modifiers);
    m_root->setProperty("taps", 0);
}

void TestTapModifiers::signalEventPointDoesNot()
{
    clickWith(Qt::ControlModifier);
    // The reason TrackRow reads tap.point and not the signal's argument. If
    // this ever starts failing, Qt has grown the property and the comment in
    // TrackRow.qml is out of date — the code still works either way.
    QVERIFY(!m_root->property("signalPointHasModifiers").toBool());
}

QTEST_MAIN(TestTapModifiers)
#include "tst_tap_modifiers.moc"
