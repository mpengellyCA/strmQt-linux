#include <QAccessible>
#include <QAccessibleActionInterface>
#include <QAccessibleValueInterface>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QTest>

namespace {

const char *kProbe = R"QML(
import QtQuick
import StrmQt

Item {
    id: root
    width: 320
    height: 120
    property int presses: 0
    property real committed: -1

    StrmButton {
        id: button
        objectName: "button"
        width: 120
        text: "Play"
        onClicked: root.presses += 1
    }

    StrmSlider {
        id: slider
        objectName: "slider"
        y: 60
        width: 240
        from: 0
        to: 100
        value: 40
        stepSize: 10
        accessibleName: "Playback position"
        onCommitted: value => root.committed = value
    }
}
)QML";

bool copyControl(const QString &modulePath, const QString &name)
{
    return QFile::copy(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/controls/") + name,
                       modulePath + QLatin1Char('/') + name);
}

} // namespace

class TestQmlAccessibility : public QObject
{
    Q_OBJECT

private slots:
    void controlsExposeSemanticActionsAndValues();
    void searchBackUsesMainNavigationTransaction();
};

void TestQmlAccessibility::searchBackUsesMainNavigationTransaction()
{
    QFile search(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/pages/SearchPage.qml"));
    QVERIFY(search.open(QIODevice::ReadOnly));
    const QByteArray searchSource = search.readAll();
    QVERIFY(searchSource.contains("signal backRequested()"));
    QVERIFY(searchSource.contains("page.backRequested()"));
    QVERIFY(!searchSource.contains("StackView.view.pop()"));

    QFile main(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/Main.qml"));
    QVERIFY(main.open(QIODevice::ReadOnly));
    const QByteArray mainSource = main.readAll();
    QVERIFY(mainSource.contains("function onBackRequested() { root.goBack(); }"));
}

void TestQmlAccessibility::controlsExposeSemanticActionsAndValues()
{
    QAccessible::setActive(true);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString modulePath = dir.filePath(QStringLiteral("StrmQt"));
    QVERIFY(QDir().mkpath(modulePath));
    QVERIFY(QFile::copy(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/Theme.qml"),
                        modulePath + QStringLiteral("/Theme.qml")));
    QVERIFY(copyControl(modulePath, QStringLiteral("FocusRing.qml")));
    QVERIFY(copyControl(modulePath, QStringLiteral("StrmIcon.qml")));
    QVERIFY(copyControl(modulePath, QStringLiteral("StrmButton.qml")));
    QVERIFY(copyControl(modulePath, QStringLiteral("StrmSlider.qml")));

    QFile qmldir(modulePath + QStringLiteral("/qmldir"));
    QVERIFY(qmldir.open(QIODevice::WriteOnly));
    qmldir.write("module StrmQt\n"
                 "singleton Theme 1.0 Theme.qml\n"
                 "FocusRing 1.0 FocusRing.qml\n"
                 "StrmIcon 1.0 StrmIcon.qml\n"
                 "StrmButton 1.0 StrmButton.qml\n"
                 "StrmSlider 1.0 StrmSlider.qml\n");
    qmldir.close();

    const QString probePath = dir.filePath(QStringLiteral("Probe.qml"));
    QFile probe(probePath);
    QVERIFY(probe.open(QIODevice::WriteOnly));
    probe.write(kProbe);
    probe.close();

    QQuickView view;
    view.engine()->addImportPath(dir.path());
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setSource(QUrl::fromLocalFile(probePath));
    QVERIFY2(view.status() == QQuickView::Ready,
             qPrintable(view.errors().isEmpty() ? QStringLiteral("no root object")
                                                : view.errors().first().toString()));
    view.resize(320, 120);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    QObject *root = view.rootObject();
    QVERIFY(root);
    auto *button = root->findChild<QQuickItem *>(QStringLiteral("button"));
    auto *slider = root->findChild<QQuickItem *>(QStringLiteral("slider"));
    QVERIFY(button);
    QVERIFY(slider);

    QAccessibleInterface *buttonInterface = QAccessible::queryAccessibleInterface(button);
    QVERIFY(buttonInterface);
    QCOMPARE(buttonInterface->role(), QAccessible::Button);
    QCOMPARE(buttonInterface->text(QAccessible::Name), QStringLiteral("Play"));
    QAccessibleActionInterface *buttonActions = buttonInterface->actionInterface();
    QVERIFY(buttonActions);
    QVERIFY(buttonActions->actionNames().contains(QAccessibleActionInterface::pressAction()));
    buttonActions->doAction(QAccessibleActionInterface::pressAction());
    QTRY_COMPARE(root->property("presses").toInt(), 1);

    QAccessibleInterface *sliderInterface = QAccessible::queryAccessibleInterface(slider);
    QVERIFY(sliderInterface);
    QCOMPARE(sliderInterface->role(), QAccessible::Slider);
    QCOMPARE(sliderInterface->text(QAccessible::Name), QStringLiteral("Playback position"));
    QAccessibleValueInterface *sliderValue = sliderInterface->valueInterface();
    QVERIFY(sliderValue);
    QCOMPARE(sliderValue->currentValue().toDouble(), 40.0);
    QCOMPARE(sliderValue->minimumValue().toDouble(), 0.0);
    QCOMPARE(sliderValue->maximumValue().toDouble(), 100.0);
    QCOMPARE(sliderValue->minimumStepSize().toDouble(), 10.0);

    QAccessibleActionInterface *sliderActions = sliderInterface->actionInterface();
    QVERIFY(sliderActions);
    QVERIFY(sliderActions->actionNames().contains(QAccessibleActionInterface::increaseAction()));
    QVERIFY(sliderActions->actionNames().contains(QAccessibleActionInterface::decreaseAction()));
    sliderActions->doAction(QAccessibleActionInterface::increaseAction());
    QTRY_COMPARE(root->property("committed").toDouble(), 50.0);
}

QTEST_MAIN(TestQmlAccessibility)

#include "tst_qml_accessibility.moc"
