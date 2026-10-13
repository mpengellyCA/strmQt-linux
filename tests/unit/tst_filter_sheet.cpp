// The real filter sheet under the real navigator, against a stand-in for the
// controller: two lists that each hold the keyboard, crossed by Left/Right,
// and a tick that must not move the cursor it was made from.

#include "input/FocusNavigator.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QtTest>

#include "QmlShimStaging.h"

namespace {

const char *kProbe = R"QML(
import QtQuick
import StrmQt

Item {
    id: root
    width: 1200
    height: 900

    // What LibraryController publishes, with the lists held here.
    QtObject {
        id: ctl

        property var picks: ({})
        property int revision: 0
        property bool loading: false
        property var model: QtObject { property int totalRecordCount: 1384 }
        readonly property bool filtered: ctl.revision >= 0 && Object.keys(ctl.picks).length > 0
        readonly property var facets: {
            ctl.revision;
            const defs = [["genre", "Genre", true], ["studio", "Studio", true],
                          ["resolution", "Resolution", false]];
            return defs.map(d => ({ key: d[0], label: d[1], multi: d[2],
                                    count: (ctl.picks[d[0]] || []).length,
                                    summary: (ctl.picks[d[0]] || []).join(", ") }));
        }
        signal facetOptionsChanged(string key)
        signal scopeChanged

        function values(key) {
            if (key === "genre")
                return ["Action", "Comedy", "Horror"];
            if (key === "resolution")
                return ["4k", "1080", "720", "sd"];
            const out = [];
            for (let i = 0; i < 60; ++i)
                out.push("Studio " + (i < 10 ? "0" : "") + i);
            return out;
        }
        function facetOptions(key) {
            const picked = ctl.picks[key] || [];
            return ctl.values(key).map(v => ({ value: v, label: v,
                                               selected: picked.indexOf(v) >= 0 }));
        }
        function facetLoading(key) { return false }
        function toggleFacetValue(key, value) {
            const next = Object.assign({}, ctl.picks);
            let picked = (next[key] || []).slice();
            const at = picked.indexOf(value);
            if (at >= 0)
                picked.splice(at, 1);
            else if (key === "resolution")
                picked = [value];
            else
                picked.push(value);
            if (picked.length > 0)
                next[key] = picked;
            else
                delete next[key];
            ctl.picks = next;
            ctl.revision += 1;
            ctl.facetOptionsChanged(key);
        }
        function clearFacet(key) {
            const next = Object.assign({}, ctl.picks);
            delete next[key];
            ctl.picks = next;
            ctl.revision += 1;
            ctl.facetOptionsChanged(key);
        }
        function clearFilters() {
            ctl.picks = ({});
            ctl.revision += 1;
        }
    }

    Item { objectName: "behind"; x: 20; y: 400; width: 80; height: 80; activeFocusOnTab: true }

    FilterSheet {
        id: sheet
        objectName: "sheet"
        controller: ctl
    }

    function openSheet() { sheet.open() }
    function isOpen() { return sheet.opened }
    function activeKey() { return sheet.activeKey }
    function picked(key) { return (ctl.picks[key] || []).join(",") }
    function where() {
        for (let p = Window.activeFocusItem; p; p = p.parent) {
            if (p.objectName && p.objectName.length > 0)
                return p.objectName
        }
        return ""
    }
}
)QML";

QQuickItem *createProbe(QTemporaryDir &dir, QQuickView &view)
{
    const QString modulePath = dir.filePath(QStringLiteral("StrmQt"));
    if (!QDir().mkpath(modulePath))
        return nullptr;
    const QStringList controls = {
        QStringLiteral("FocusRing.qml"),      QStringLiteral("StrmIcon.qml"),
        QStringLiteral("StrmTooltip.qml"),    QStringLiteral("StrmIconButton.qml"),
        QStringLiteral("StrmButton.qml"),     QStringLiteral("StrmScrollBar.qml"),
        QStringLiteral("StrmSearchField.qml"),
    };
    QByteArray lines = "module StrmQt\nsingleton Theme 1.0 Theme.qml\n";
    const auto stage = [&](const QString &from, const QString &name, bool listed) {
        if (!QFile::copy(from + name, modulePath + QLatin1Char('/') + name))
            return false;
        if (listed)
            lines += name.chopped(4).toUtf8() + " 1.0 " + name.toUtf8() + '\n';
        return true;
    };
    if (!stage(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/"), QStringLiteral("Theme.qml"), false))
        return nullptr;
    for (const QString &name : controls) {
        if (!stage(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/controls/"), name, true))
            return nullptr;
    }
    if (!stage(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/shell/"),
               QStringLiteral("FilterSheet.qml"), true))
        return nullptr;
    const QByteArray shimLines = strmqt::test::stageShims(modulePath);
    if (shimLines.isEmpty())
        return nullptr;
    QFile qmldir(modulePath + QStringLiteral("/qmldir"));
    if (!qmldir.open(QIODevice::WriteOnly))
        return nullptr;
    qmldir.write(lines);
    qmldir.write(shimLines);
    qmldir.close();

    QFile probe(dir.filePath(QStringLiteral("Probe.qml")));
    if (!probe.open(QIODevice::WriteOnly))
        return nullptr;
    probe.write(kProbe);
    probe.close();

    view.engine()->addImportPath(dir.path());
    view.setSource(QUrl::fromLocalFile(probe.fileName()));
    if (view.status() != QQuickView::Ready)
        return nullptr;
    view.resize(1200, 900);
    view.show();
    if (!QTest::qWaitForWindowExposed(&view))
        return nullptr;
    return view.rootObject();
}

} // namespace

class FilterSheetTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void opensOnTheFirstAxis();
    void selectEntersTheValuesAndTicks();
    void aTickKeepsTheCursor();
    void singleChoiceReplaces();
    void backClosesAndNothingBehindIsReachable();

private:
    QVariant call(const char *name)
    {
        QVariant result;
        QMetaObject::invokeMethod(m_root, name, Q_RETURN_ARG(QVariant, result));
        return result;
    }
    QString picked(const char *key)
    {
        QVariant result;
        QMetaObject::invokeMethod(m_root, "picked", Q_RETURN_ARG(QVariant, result),
                                  Q_ARG(QVariant, QString::fromLatin1(key)));
        return result.toString();
    }
    QString where() { return call("where").toString(); }
    void press(Qt::Key key) { QTest::keyClick(&m_view, key); }

    QTemporaryDir m_dir;
    QQuickView m_view;
    QQuickItem *m_root = nullptr;
};

void FilterSheetTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_root = createProbe(m_dir, m_view);
    QVERIFY(m_root);
    new strmqt::FocusNavigator(&m_view, &m_view);
}

void FilterSheetTest::opensOnTheFirstAxis()
{
    QMetaObject::invokeMethod(m_root, "openSheet");
    QVERIFY(call("isOpen").toBool());
    QCOMPARE(call("activeKey").toString(), QStringLiteral("genre"));
    QCOMPARE(where(), QStringLiteral("filterSheetFacets"));

    // Walking the axes changes what the right-hand pane lists.
    press(Qt::Key_Down);
    QCOMPARE(call("activeKey").toString(), QStringLiteral("studio"));
    press(Qt::Key_Up);
    QCOMPARE(call("activeKey").toString(), QStringLiteral("genre"));
}

void FilterSheetTest::selectEntersTheValuesAndTicks()
{
    press(Qt::Key_Return);
    QCOMPARE(where(), QStringLiteral("filterSheetOptions"));
    press(Qt::Key_Down);
    press(Qt::Key_Space);
    QCOMPARE(picked("genre"), QStringLiteral("Comedy"));
    press(Qt::Key_Down);
    press(Qt::Key_Return);
    QCOMPARE(picked("genre"), QStringLiteral("Comedy,Horror"));
    // Ticking again unticks.
    press(Qt::Key_Return);
    QCOMPARE(picked("genre"), QStringLiteral("Comedy"));

    // Left is the way back to the axes, found by the navigator.
    press(Qt::Key_Left);
    QCOMPARE(where(), QStringLiteral("filterSheetFacets"));
    QCOMPARE(call("activeKey").toString(), QStringLiteral("genre"));
}

void FilterSheetTest::aTickKeepsTheCursor()
{
    // Sixty studios: the list scrolls, and a tick rebuilds the controller's
    // answer. The cursor and the scroll position must survive that.
    press(Qt::Key_Down);
    QCOMPARE(call("activeKey").toString(), QStringLiteral("studio"));
    press(Qt::Key_Right);
    QCOMPARE(where(), QStringLiteral("filterSheetOptions"));
    for (int i = 0; i < 30; ++i)
        press(Qt::Key_Down);
    press(Qt::Key_Space);
    QCOMPARE(picked("studio"), QStringLiteral("Studio 30"));
    press(Qt::Key_Down);
    press(Qt::Key_Space);
    QCOMPARE(picked("studio"), QStringLiteral("Studio 30,Studio 31"));
    QCOMPARE(where(), QStringLiteral("filterSheetOptions"));
}

void FilterSheetTest::singleChoiceReplaces()
{
    // From thirty rows down a list that has not been drawn since: Left is
    // still the axes (FocusNavigator::cursorRect).
    press(Qt::Key_Left);
    QCOMPARE(where(), QStringLiteral("filterSheetFacets"));
    press(Qt::Key_Down);
    QCOMPARE(call("activeKey").toString(), QStringLiteral("resolution"));
    press(Qt::Key_Right);
    press(Qt::Key_Space);
    QCOMPARE(picked("resolution"), QStringLiteral("4k"));
    press(Qt::Key_Down);
    press(Qt::Key_Space);
    QCOMPARE(picked("resolution"), QStringLiteral("1080"));
}

void FilterSheetTest::backClosesAndNothingBehindIsReachable()
{
    // The page under the scrim is not somewhere an arrow can go.
    for (int i = 0; i < 6; ++i)
        press(Qt::Key_Left);
    QVERIFY(where() != QStringLiteral("behind"));
    QVERIFY(call("isOpen").toBool());

    press(Qt::Key_Escape);
    QVERIFY(!call("isOpen").toBool());
}

QTEST_MAIN(FilterSheetTest)
#include "tst_filter_sheet.moc"
