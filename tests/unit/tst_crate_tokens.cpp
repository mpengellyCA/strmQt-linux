#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

#include "QmlShimStaging.h"
#include "core/Settings.h"

namespace {

const char *kProbe = R"QML(
import QtQuick
import StrmQt

QtObject {
    readonly property var axes: Theme.crateDisplayAxes
    readonly property int weight: Theme.crateDisplayWeight
    readonly property real tracking: Theme.crateDisplayTracking
    readonly property int heroHome: Theme.crateHeroHome
    readonly property int heroAlbum: Theme.crateHeroAlbum
    readonly property int heroArtist: Theme.crateHeroArtist
    readonly property int kicker: Theme.crateKickerSize
    readonly property real kickerTracking: Theme.crateKickerTracking
    readonly property int badge: Theme.crateBadgeSize
    readonly property int badgeRadius: Theme.crateBadgeRadius
    readonly property color badgeBorder: Theme.crateBadgeBorder
    readonly property color disabled: Theme.textDisabled
    readonly property color hiRes: Theme.crateBadgeHiRes
    readonly property color accent: Theme.accentColor
    readonly property int sleeveRadius: Theme.crateSleeveRadius
    readonly property var sleeveElevation: Theme.crateSleeveElevation
    readonly property var elevation4: Theme.elevation4
    readonly property int sleeve: Theme.crateSleeveSize
    function tvHero() { Theme.densityMode = "tv"; const v = Theme.crateHeroArtist; Theme.densityMode = "comfortable"; return v; }

    function durations(reduced) {
        Theme.reducedMotion = reduced;
        const v = [Theme.animInstant, Theme.animFastMs, Theme.animNormalMs, Theme.animSlow,
                   Theme.animAmbient];
        Theme.reducedMotion = false;
        return v.join(",");
    }

    function colourValues() { return Theme.subtitleColors.map(entry => entry.value).join(",") }
    function colourNames() { return Theme.subtitleColors.map(entry => entry.text).join(",") }
    function colourIndex(value) { return Theme.subtitleColorIndex(value) }
    function colourName(value) { return Theme.subtitleColorName(value) }
}
)QML";

// The tier's BoundLoader and BoundViewSlot (src/ui/shims/<tier>), asked the
// same questions in either build. On the full tier the answers are Loader's
// and ListView's own; on the compat tier they are the createObject() hosts',
// and the point of the test is that nobody can tell which.
const char *kShimProbe = R"QML(
import QtQuick
import StrmQt

Item {
    id: root
    width: 800
    height: 700

    property int w: 80
    property int boxW: 300
    property bool on: true
    property bool alt: false
    property int loads: 0

    Component { id: fixed; Item { width: 120; height: 40 } }
    Component { id: implicitOnly; Item { implicitWidth: 70; implicitHeight: 30 } }
    // Wider content than the width it declares: a card.
    Component { id: card; Item { implicitWidth: 200; implicitHeight: 60; width: 90; height: 20 } }
    Component { id: selfSized; Item { implicitWidth: 70; implicitHeight: 30; width: root.w } }
    Component { id: growing; Item { width: root.w; height: 10 } }

    BoundLoader { id: a; sourceComponent: fixed }
    BoundLoader { id: b; sourceComponent: implicitOnly }
    BoundLoader { id: c; sourceComponent: card }
    BoundLoader { id: d; width: root.boxW; height: 50; sourceComponent: implicitOnly }
    BoundLoader { id: e; width: root.boxW; height: 50; sourceComponent: selfSized }
    BoundLoader { id: f; width: root.boxW; sourceComponent: implicitOnly }
    BoundLoader { id: g; sourceComponent: growing }
    BoundLoader { id: h; active: root.on; sourceComponent: fixed }
    BoundLoader { id: i; sourceComponent: root.alt ? implicitOnly : fixed; onLoaded: root.loads++ }

    Item {
        id: frame
        width: 400
        height: 200

        BoundLoader { id: j; anchors.fill: parent; anchors.margins: 10; sourceComponent: implicitOnly }
        BoundLoader { id: k; anchors.centerIn: parent; sourceComponent: card }
    }

    Component { id: one; Item { objectName: "one"; width: view.width; height: 40 } }
    Component { id: two; Item { objectName: "two"; width: view.width / 2; height: 90 } }

    BoundViewSlot { id: slot; sourceComponent: root.alt ? two : one }

    ListView {
        id: view
        y: 220
        width: root.boxW
        height: 400
        model: 3
        delegate: Item { width: view.width; height: 50 }
        footer: slot.component
    }

    function size(item) { return item ? item.width + "x" + item.height : "none" }
    function loader(name) {
        const l = ({ a: a, b: b, c: c, d: d, e: e, f: f, g: g, h: h, i: i, j: j, k: k })[name];
        return size(l) + " " + size(l.item);
    }
    function centredAt() { return k.x }
    // The footer as the page wrote it: the view's footerItem on the full
    // tier, the one item inside the host on the compat tier.
    function footer() {
        const host = view.footerItem;
        if (!host)
            return "none";
        const item = host.objectName.length > 0 ? host : host.children[0];
        return (item ? item.objectName + " " + size(item) : "empty") + " in " + size(host)
               + " of " + view.contentHeight;
    }
    function widenFrame() { frame.width = 500 }
}
)QML";

// The capture sheet, with something behind it to catch whatever gets out.
const char *kSheetProbe = R"QML(
import QtQuick
import StrmQt

Item {
    id: root
    width: 900
    height: 700

    property bool pad: false
    property var log: []
    property int slashes: 0

    function note(entry) { const next = root.log.slice(); next.push(entry); root.log = next }
    function take() { const said = root.log.join(" "); root.log = []; return said }

    // What the shell binds to Search. Unclaimed, it fires instead of the key
    // being offered as a binding.
    Shortcut { sequence: "/"; onActivated: root.slashes++ }

    // The page behind the scrim.
    Keys.onPressed: event => { root.note("out:" + event.key); event.accepted = true }

    SettingsSections.KeyCaptureSheet {
        id: sheet
        anchors.fill: parent
        actionName: "Search"
        currentSequence: "/"
        padDriving: root.pad
        onKeyPressed: root.note("key")
        onCaptured: (key, modifiers) => root.note("captured:" + key + ":" + modifiers)
        onCleared: root.note("cleared")
        onDismissed: root.note("dismissed")
    }

    function open() { sheet.open() }
    function listening() { return sheet.listening }
    function modal() { return sheet.navModal === true && sheet.navEntry === Window.activeFocusItem }
    // Where the navigator sends Down from the well (ARCHITECTURE.md §5).
    function downFromTheWell() {
        const target = Window.activeFocusItem.navDown;
        target.forceActiveFocus();
        return target.text;
    }
    function backToTheWell() { sheet.navEntry.forceActiveFocus() }
}
)QML";

bool stageModule(const QTemporaryDir &dir, const QStringList &controls, bool withSections)
{
    const QString modulePath = dir.filePath(QStringLiteral("StrmQt"));
    if (!QDir().mkpath(modulePath))
        return false;
    QByteArray lines = "module StrmQt\nsingleton Theme 1.0 Theme.qml\n";
    const auto stage = [&](const QString &from, const QString &name, bool listed) {
        if (!QFile::copy(from + name, modulePath + QLatin1Char('/') + name))
            return false;
        if (listed)
            lines += name.chopped(4).toUtf8() + " 1.0 " + name.toUtf8() + '\n';
        return true;
    };
    if (!stage(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/"), QStringLiteral("Theme.qml"), false))
        return false;
    for (const QString &name : controls) {
        if (!stage(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/controls/"), name, true))
            return false;
    }
    if (withSections
        && !stage(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/settings/"),
                  QStringLiteral("SettingsSections.qml"), true))
        return false;
    const QByteArray shimLines = strmqt::test::stageShims(modulePath);
    if (shimLines.isEmpty())
        return false;
    QFile qmldir(modulePath + QStringLiteral("/qmldir"));
    if (!qmldir.open(QIODevice::WriteOnly))
        return false;
    qmldir.write(lines);
    qmldir.write(shimLines);
    return true;
}

QQuickItem *showProbe(const QTemporaryDir &dir, QQuickView &view, const char *source)
{
    QFile probe(dir.filePath(QStringLiteral("Probe.qml")));
    if (!probe.open(QIODevice::WriteOnly))
        return nullptr;
    probe.write(source);
    probe.close();
    view.engine()->addImportPath(dir.path());
    view.setSource(QUrl::fromLocalFile(probe.fileName()));
    if (view.status() != QQuickView::Ready)
        return nullptr;
    view.show();
    if (!QTest::qWaitForWindowExposed(&view))
        return nullptr;
    return view.rootObject();
}

QVariant call(QObject *object, const char *name)
{
    QVariant result;
    QMetaObject::invokeMethod(object, name, Q_RETURN_ARG(QVariant, result));
    return result;
}

QVariant call(QObject *object, const char *name, const QVariant &argument)
{
    QVariant result;
    QMetaObject::invokeMethod(object, name, Q_RETURN_ARG(QVariant, result),
                              Q_ARG(QVariant, argument));
    return result;
}

QString readSource(const QString &relative)
{
    QFile file(QStringLiteral(STRMQT_SOURCE_DIR "/") + relative);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

} // namespace

class TestCrateTokens : public QObject
{
    Q_OBJECT

private slots:
    void tokensMatchTheSpec();
    void reducedMotionStillsEveryDuration();
    void whateverLoopsOnAmbientIsGated();
    void subtitleColoursAreOneList();
    void boundShimsSizeAsTheViewsDo();
    void captureSheetListensWhileTheWellHasFocus();

private:
    std::unique_ptr<QObject> themeProbe(QQmlEngine &engine, const QTemporaryDir &dir);
};

std::unique_ptr<QObject> TestCrateTokens::themeProbe(QQmlEngine &engine, const QTemporaryDir &dir)
{
    const QString modulePath = dir.filePath(QStringLiteral("StrmQt"));
    if (!QDir().mkpath(modulePath)
        || !QFile::copy(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/Theme.qml"),
                        modulePath + QStringLiteral("/Theme.qml")))
        return nullptr;
    QFile qmldir(modulePath + QStringLiteral("/qmldir"));
    if (!qmldir.open(QIODevice::WriteOnly))
        return nullptr;
    qmldir.write("module StrmQt\nsingleton Theme 1.0 Theme.qml\n");
    qmldir.close();

    engine.addImportPath(dir.path());
    QQmlComponent component(&engine);
    component.setData(kProbe, QUrl::fromLocalFile(dir.filePath(QStringLiteral("Probe.qml"))));
    std::unique_ptr<QObject> probe(component.create());
    if (!probe)
        qWarning("%s", qPrintable(component.errorString()));
    return probe;
}

void TestCrateTokens::tokensMatchTheSpec()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString modulePath = dir.filePath(QStringLiteral("StrmQt"));
    QVERIFY(QDir().mkpath(modulePath));
    QVERIFY(QFile::copy(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/Theme.qml"),
                        modulePath + QStringLiteral("/Theme.qml")));
    QFile qmldir(modulePath + QStringLiteral("/qmldir"));
    QVERIFY(qmldir.open(QIODevice::WriteOnly));
    qmldir.write("module StrmQt\nsingleton Theme 1.0 Theme.qml\n");
    qmldir.close();

    QQmlEngine engine;
    engine.addImportPath(dir.path());
    QQmlComponent component(&engine);
    component.setData(kProbe, QUrl::fromLocalFile(dir.filePath(QStringLiteral("Probe.qml"))));
    std::unique_ptr<QObject> probe(component.create());
    QVERIFY2(probe, qPrintable(component.errorString()));

    const QVariantMap axes = probe->property("axes").toMap();
    QCOMPARE(axes.value("wdth").toInt(), 120);
    QCOMPARE(axes.value("wght").toInt(), 820);
    QCOMPARE(probe->property("weight").toInt(), 820);
    QCOMPARE(probe->property("tracking").toReal(), -0.02);
    QCOMPARE(probe->property("heroHome").toInt(), 52);
    QCOMPARE(probe->property("heroAlbum").toInt(), 58);
    QCOMPARE(probe->property("heroArtist").toInt(), 84);
    QCOMPARE(probe->property("kicker").toInt(), 11);
    QCOMPARE(probe->property("kickerTracking").toReal(), 0.17);
    QCOMPARE(probe->property("badge").toInt(), 11);
    QCOMPARE(probe->property("badgeRadius").toInt(), 3);
    QCOMPARE(probe->property("badgeBorder"), probe->property("disabled"));
    QCOMPARE(probe->property("hiRes"), probe->property("accent"));
    QCOMPARE(probe->property("sleeveRadius").toInt(), 3);
    QCOMPARE(probe->property("sleeveElevation").toMap(), probe->property("elevation4").toMap());
    QCOMPARE(probe->property("sleeve").toInt(), 178);

    QVariant tvHero;
    QVERIFY(QMetaObject::invokeMethod(probe.get(), "tvHero", Q_RETURN_ARG(QVariant, tvHero)));
    QCOMPARE(tvHero.toInt(), 97); // round(84 × 1.15): hero sizes follow density
}

void TestCrateTokens::reducedMotionStillsEveryDuration()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QQmlEngine engine;
    const std::unique_ptr<QObject> probe = themeProbe(engine, dir);
    QVERIFY(probe);

    // Ambient was the one duration the preference did not reach.
    QCOMPARE(call(probe.get(), "durations", false).toString(),
             QStringLiteral("90,160,240,420,1200"));
    QCOMPARE(call(probe.get(), "durations", true).toString(), QStringLiteral("0,0,0,0,0"));
}

void TestCrateTokens::whateverLoopsOnAmbientIsGated()
{
    // A loop with no duration does not stand still: it runs to its end at once
    // and parks its target there — a shimmer off the edge of its skeleton, a
    // spinner at whatever angle. So zero is not enough, and every file that
    // loops on the token also has to ask the preference whether to run.
    static const QRegularExpression asksThePreference(QStringLiteral("\\breduce[d]?Motion\\b"));
    QStringList users;
    QDirIterator files(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui"), {QStringLiteral("*.qml")},
                       QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        const QString path = files.next();
        if (path.endsWith(QStringLiteral("/Theme.qml")))
            continue;
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(file.readAll());
        if (!text.contains(QStringLiteral("Theme.animAmbient")))
            continue;
        users.append(path);
        QVERIFY2(asksThePreference.match(text).hasMatch(), qPrintable(path));
    }
    // The skeleton's shimmer at the least: a scan that finds nothing proves
    // nothing.
    QVERIFY(!users.filter(QStringLiteral("StrmSkeleton.qml")).isEmpty());

    // And the skeletons a page shows while it loads follow the preference
    // instead of overriding it with `false`.
    const QString loading = readSource(QStringLiteral("src/ui/shell/LoadingState.qml"));
    QVERIFY(loading.contains(QStringLiteral("property bool reduceMotion: Theme.reducedMotion")));
}

void TestCrateTokens::subtitleColoursAreOneList()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QQmlEngine engine;
    // Theme names an older shade through Settings (subtitleColorSwatch), as
    // the app exposes it.
    strmqt::Settings prefs(dir.filePath(QStringLiteral("settings.ini")));
    engine.rootContext()->setContextProperty(QStringLiteral("Prefs"), &prefs);
    const std::unique_ptr<QObject> probe = themeProbe(engine, dir);
    QVERIFY(probe);

    const QStringList values =
        call(probe.get(), "colourValues").toString().split(QLatin1Char(','));
    QCOMPARE(values, (QStringList{QStringLiteral("#FFFFFF"), QStringLiteral("#E8E2D6"),
                                  QStringLiteral("#F0A02A"), QStringLiteral("#F2E14C"),
                                  QStringLiteral("#7FD8E8"), QStringLiteral("#8CE08C"),
                                  QStringLiteral("#A0A0A0"), QStringLiteral("#000000")}));
    const QStringList names =
        call(probe.get(), "colourNames").toString().split(QLatin1Char(','));
    QCOMPARE(names.size(), values.size());
    QCOMPARE(QSet<QString>(names.cbegin(), names.cend()).size(), names.size());

    // Settings hands a colour back in capitals; a file edited by hand may not.
    for (int i = 0; i < values.size(); ++i) {
        QCOMPARE(call(probe.get(), "colourIndex", values.at(i)).toInt(), i);
        QCOMPARE(call(probe.get(), "colourIndex", values.at(i).toLower()).toInt(), i);
        QCOMPARE(call(probe.get(), "colourName", values.at(i)).toString(), names.at(i));
    }

    // The shades the player's sheet used to write. A stored one is shown
    // under the name that replaced it; nothing rewrites it.
    QCOMPARE(call(probe.get(), "colourName", QStringLiteral("#E8E4DC")).toString(),
             QStringLiteral("Soft white"));
    QCOMPARE(call(probe.get(), "colourName", QStringLiteral("#FFE066")).toString(),
             QStringLiteral("Yellow"));
    QCOMPARE(call(probe.get(), "colourName", QStringLiteral("#8ae0ff")).toString(),
             QStringLiteral("Cyan"));
    QCOMPARE(call(probe.get(), "colourName", QStringLiteral("#BFBFBF")).toString(),
             QStringLiteral("Grey"));

    // One from nowhere is said to be that, with its code.
    QCOMPARE(call(probe.get(), "colourIndex", QStringLiteral("#123456")).toInt(), -1);
    QCOMPARE(call(probe.get(), "colourName", QStringLiteral("#12ab56")).toString(),
             QStringLiteral("Custom #12AB56"));

    // The web remote keeps a list of its own (it is a web page, and cannot
    // read Theme). Whatever it can write has to have a name here.
    const QString remote = readSource(QStringLiteral("src/remote/web/player.js"));
    const qsizetype from = remote.indexOf(QStringLiteral("SUB_COLOURS"));
    QVERIFY(from >= 0);
    static const QRegularExpression hex(QStringLiteral("#[0-9A-Fa-f]{6}"));
    const QString list = remote.mid(from, remote.indexOf(QStringLiteral("];"), from) - from);
    QRegularExpressionMatchIterator written = hex.globalMatch(list);
    int remoteColours = 0;
    while (written.hasNext()) {
        const QString value = written.next().captured();
        QVERIFY2(call(probe.get(), "colourIndex", value).toInt() >= 0, qPrintable(value));
        ++remoteColours;
    }
    QVERIFY(remoteColours > 0);

    // And the two pages read the list rather than keeping one each, which is
    // how they came to disagree.
    for (const QString &page : {QStringLiteral("src/ui/pages/SettingsPage.qml"),
                                QStringLiteral("src/ui/player/PlaybackSettingsPanel.qml")}) {
        const QString text = readSource(page);
        QVERIFY2(text.contains(QStringLiteral("Theme.subtitleColors")), qPrintable(page));
        for (const QString &value : values) {
            if (value == QStringLiteral("#FFFFFF") || value == QStringLiteral("#000000"))
                continue; // the reset to white, and inks that are not subtitles
            QVERIFY2(!text.contains(value, Qt::CaseInsensitive),
                     qPrintable(page + QStringLiteral(" spells out ") + value));
        }
    }
}

void TestCrateTokens::boundShimsSizeAsTheViewsDo()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(stageModule(dir, {}, false));
    QQuickView view;
    QQuickItem *root = showProbe(dir, view, kShimProbe);
    QVERIFY(root);
    const auto loader = [&](const char *name) {
        return call(root, "loader", QString::fromLatin1(name)).toString();
    };

    // Not sized from outside: the loader takes the item's size — its width
    // where it declares one, whatever its content would like to be.
    QCOMPARE(loader("a"), QStringLiteral("120x40 120x40"));
    QCOMPARE(loader("b"), QStringLiteral("70x30 70x30"));
    QCOMPARE(loader("c"), QStringLiteral("90x20 90x20"));
    QCOMPARE(call(root, "centredAt").toInt(), 155);
    // Sized: the item takes the loader's size, in the dimension that was
    // given and not in the other.
    QCOMPARE(loader("d"), QStringLiteral("300x50 300x50"));
    QCOMPARE(loader("f"), QStringLiteral("300x30 300x30"));
    QCOMPARE(loader("j"), QStringLiteral("380x180 380x180"));
    // ...and keeps it, a width binding of its own notwithstanding.
    QCOMPARE(loader("e"), QStringLiteral("300x50 300x50"));

    root->setProperty("w", 95);
    QCOMPARE(loader("g"), QStringLiteral("95x10 95x10"));
    QCOMPARE(loader("e"), QStringLiteral("300x50 300x50"));

    root->setProperty("boxW", 260);
    QCOMPARE(loader("d"), QStringLiteral("260x50 260x50"));
    QCOMPARE(loader("e"), QStringLiteral("260x50 260x50"));
    QCOMPARE(loader("f"), QStringLiteral("260x30 260x30"));
    QMetaObject::invokeMethod(root, "widenFrame");
    QCOMPARE(loader("j"), QStringLiteral("480x180 480x180"));
    QCOMPARE(call(root, "centredAt").toInt(), 205);

    QCOMPARE(root->property("loads").toInt(), 1);
    root->setProperty("on", false);
    QVERIFY(loader("h").endsWith(QStringLiteral(" none")));
    root->setProperty("on", true);
    QCOMPARE(loader("h"), QStringLiteral("120x40 120x40"));

    // A footer is sized by nobody but itself, and a different component is a
    // different footer.
    QTRY_COMPARE(call(root, "footer").toString(), QStringLiteral("one 260x40 in 260x40 of 190"));
    root->setProperty("alt", true);
    QCOMPARE(loader("i"), QStringLiteral("70x30 70x30"));
    QCOMPARE(root->property("loads").toInt(), 2);
    QTRY_COMPARE(call(root, "footer").toString(), QStringLiteral("two 130x90 in 130x90 of 240"));
    root->setProperty("boxW", 300);
    QTRY_COMPARE(call(root, "footer").toString(), QStringLiteral("two 150x90 in 150x90 of 240"));
    root->setProperty("alt", false);
    QTRY_COMPARE(call(root, "footer").toString(), QStringLiteral("one 300x40 in 300x40 of 190"));
}

void TestCrateTokens::captureSheetListensWhileTheWellHasFocus()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(stageModule(dir,
                        {QStringLiteral("FocusRing.qml"), QStringLiteral("StrmIcon.qml"),
                         QStringLiteral("StrmTooltip.qml"), QStringLiteral("StrmIconButton.qml"),
                         QStringLiteral("StrmButton.qml"), QStringLiteral("StrmPanel.qml")},
                        true));
    QQuickView view;
    QQuickItem *root = showProbe(dir, view, kSheetProbe);
    QVERIFY(root);
    const auto take = [&] { return call(root, "take").toString(); };
    const auto press = [&](Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QTest::keyClick(&view, key, modifiers);
        return take();
    };
    const QString down = QString::number(Qt::Key_Down);
    const QString up = QString::number(Qt::Key_Up);

    QMetaObject::invokeMethod(root, "open");
    QVERIFY(call(root, "listening").toBool());
    QVERIFY(call(root, "modal").toBool());

    // Typed: every key is a candidate, the ones the navigator would otherwise
    // have and the one a window shortcut is bound to included.
    // (Two keys, because QTest presses Ctrl first — the half of a chord that
    // is not one yet.)
    QCOMPARE(press(Qt::Key_K, Qt::ControlModifier),
             QStringLiteral("key key captured:%1:%2").arg(Qt::Key_K).arg(int(Qt::ControlModifier)));
    QCOMPARE(press(Qt::Key_Down), QStringLiteral("key captured:%1:0").arg(down));
    QCOMPARE(press(Qt::Key_Tab), QStringLiteral("key captured:%1:0").arg(Qt::Key_Tab));
    QCOMPARE(press(Qt::Key_Return), QStringLiteral("key captured:%1:0").arg(Qt::Key_Return));
    QCOMPARE(press(Qt::Key_Slash), QStringLiteral("key captured:%1:0").arg(Qt::Key_Slash));
    QCOMPARE(root->property("slashes").toInt(), 0);
    QCOMPARE(press(Qt::Key_Control), QStringLiteral("key"));
    // The two buttons, from the keyboard.
    QCOMPARE(press(Qt::Key_Escape), QStringLiteral("key dismissed"));
    QCOMPARE(press(Qt::Key_Backspace), QStringLiteral("key cleared"));
    QCOMPARE(press(Qt::Key_Delete), QStringLiteral("key cleared"));

    // Delivered for a pad: nothing is a binding. An arrow is declined all the
    // way out (to the navigator, in the app), Back cancels, Select is spent.
    root->setProperty("pad", true);
    QCOMPARE(press(Qt::Key_Down), QStringLiteral("key key out:%1").arg(down));
    QCOMPARE(press(Qt::Key_Return), QStringLiteral("key"));
    QCOMPARE(press(Qt::Key_Backspace), QStringLiteral("key"));
    QCOMPARE(press(Qt::Key_Escape), QStringLiteral("key dismissed"));

    // Down is named, and it is the button that changes nothing.
    QCOMPARE(call(root, "downFromTheWell").toString(), QStringLiteral("Cancel"));
    QVERIFY(!call(root, "listening").toBool());
    QCOMPARE(press(Qt::Key_Return), QStringLiteral("dismissed"));
    QCOMPARE(press(Qt::Key_Escape), QStringLiteral("key dismissed"));
    QCOMPARE(press(Qt::Key_Up), QStringLiteral("key out:%1").arg(up));
    // Off the well a key is not a binding from a keyboard either, and still
    // does not reach the page.
    root->setProperty("pad", false);
    QCOMPARE(press(Qt::Key_K), QStringLiteral("key"));
    QCOMPARE(press(Qt::Key_Backspace), QStringLiteral("key"));

    // Back on the well it listens again.
    QMetaObject::invokeMethod(root, "backToTheWell");
    QVERIFY(call(root, "listening").toBool());
    QCOMPARE(press(Qt::Key_K), QStringLiteral("key captured:%1:0").arg(Qt::Key_K));
}

QTEST_MAIN(TestCrateTokens)
#include "tst_crate_tokens.moc"
