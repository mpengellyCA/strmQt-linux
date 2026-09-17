#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QTest>

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
}
)QML";

} // namespace

class TestCrateTokens : public QObject
{
    Q_OBJECT

private slots:
    void tokensMatchTheSpec();
};

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

QTEST_MAIN(TestCrateTokens)
#include "tst_crate_tokens.moc"
