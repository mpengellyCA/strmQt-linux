#include <QDir>
#include <QFile>
#include <QImage>
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
    width: 520
    height: 320

    function setReducedMotion(on) { Theme.reducedMotion = on; }
    function change(url) { stage.changeAlbum(url); }
    function sleeveWidth() { return stage.sleeveRect(root).width; }

    RecordStage {
        id: stage
        objectName: "stage"
        sleeveSize: 300
    }
}
)QML";

bool copySource(const QString &from, const QString &modulePath, const QString &name)
{
    return QFile::copy(QStringLiteral(STRMQT_SOURCE_DIR) + from, modulePath + QLatin1Char('/') + name);
}

} // namespace

class TestRecordStage : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void spinsOnlyWhilePlayingAndLive();
    void animateOffOrReducedMotionStillsTheRecord();
    void albumChangeSlidesInSwapsAndSlidesOut();
    void changeWhileStoppedOrInFlightSwapsAtOnce();
    void sleeveRectMapsTheSleeve();
    void changeToBlankCoverHidesThePreviousCover();

private:
    QVariant call(const char *function, const QVariant &argument = QVariant());

    QTemporaryDir m_dir;
    QString m_coverA;
    QString m_coverB;
    QQuickView *m_view = nullptr;
    QObject *m_root = nullptr;
    QObject *m_stage = nullptr;
};

void TestRecordStage::initTestCase()
{
    QVERIFY(m_dir.isValid());
    const QString modulePath = m_dir.filePath(QStringLiteral("StrmQt"));
    QVERIFY(QDir().mkpath(modulePath));
    QVERIFY(copySource(QStringLiteral("/src/ui/Theme.qml"), modulePath, QStringLiteral("Theme.qml")));
    QVERIFY(copySource(QStringLiteral("/src/ui/controls/StrmImage.qml"), modulePath, QStringLiteral("StrmImage.qml")));
    QVERIFY(copySource(QStringLiteral("/src/ui/music/RecordStage.qml"), modulePath, QStringLiteral("RecordStage.qml")));

    QFile qmldir(modulePath + QStringLiteral("/qmldir"));
    QVERIFY(qmldir.open(QIODevice::WriteOnly));
    qmldir.write("module StrmQt\n"
                 "singleton Theme 1.0 Theme.qml\n"
                 "StrmImage 1.0 StrmImage.qml\n"
                 "RecordStage 1.0 RecordStage.qml\n");
    qmldir.close();

    QFile probe(m_dir.filePath(QStringLiteral("Probe.qml")));
    QVERIFY(probe.open(QIODevice::WriteOnly));
    probe.write(kProbe);
    probe.close();

    // Two real covers, so StrmImage reaches Ready instead of logging a dead URL.
    QImage a(8, 8, QImage::Format_RGB32);
    a.fill(Qt::darkRed);
    QVERIFY(a.save(m_dir.filePath(QStringLiteral("a.png"))));
    QImage b(8, 8, QImage::Format_RGB32);
    b.fill(Qt::darkBlue);
    QVERIFY(b.save(m_dir.filePath(QStringLiteral("b.png"))));
    m_coverA = QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("a.png"))).toString();
    m_coverB = QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("b.png"))).toString();
}

void TestRecordStage::init()
{
    m_view = new QQuickView;
    m_view->engine()->addImportPath(m_dir.path());
    m_view->setSource(QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("Probe.qml"))));
    QVERIFY2(m_view->status() == QQuickView::Ready,
             qPrintable(m_view->errors().isEmpty() ? QStringLiteral("no root object")
                                                   : m_view->errors().first().toString()));
    m_view->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_view));
    m_root = m_view->rootObject();
    m_stage = m_root->findChild<QQuickItem *>(QStringLiteral("stage"));
    QVERIFY(m_stage);
    m_stage->setProperty("coverUrl", m_coverA);
}

void TestRecordStage::cleanup()
{
    delete m_view;
    m_view = nullptr;
    m_root = nullptr;
    m_stage = nullptr;
}

QVariant TestRecordStage::call(const char *function, const QVariant &argument)
{
    QVariant result;
    if (argument.isValid())
        QMetaObject::invokeMethod(m_root, function, Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, argument));
    else
        QMetaObject::invokeMethod(m_root, function, Q_RETURN_ARG(QVariant, result));
    return result;
}

void TestRecordStage::spinsOnlyWhilePlayingAndLive()
{
    QCOMPARE(m_stage->property("recordState").toString(), QStringLiteral("stopped"));
    QVERIFY(!m_stage->property("turning").toBool());
    QCOMPARE(m_stage->property("slide").toReal(), 0.0);
    QCOMPARE(m_stage->property("shownCover").toString(), m_coverA);

    m_stage->setProperty("recordState", QStringLiteral("playing"));
    QVERIFY(m_stage->property("spinning").toBool());
    QTRY_VERIFY(m_stage->property("turning").toBool());
    QTRY_COMPARE(m_stage->property("slide").toReal(), 0.45);

    // Leaving the page stops the animator; it is not merely hidden.
    m_stage->setProperty("live", false);
    QVERIFY(!m_stage->property("spinning").toBool());
    QTRY_VERIFY(!m_stage->property("turning").toBool());
    m_stage->setProperty("live", true);
    QTRY_VERIFY(m_stage->property("turning").toBool());

    m_stage->setProperty("recordState", QStringLiteral("paused"));
    QTRY_VERIFY(!m_stage->property("turning").toBool());
    QVERIFY(m_stage->property("settling").toBool());
    QTRY_VERIFY(!m_stage->property("settling").toBool());
    QCOMPARE(m_stage->property("slide").toReal(), 0.45); // stays out

    m_stage->setProperty("recordState", QStringLiteral("buffering"));
    QVERIFY(!m_stage->property("turning").toBool());
    QVERIFY(!m_stage->property("settling").toBool());
    QCOMPARE(m_stage->property("slide").toReal(), 0.45); // holds

    m_stage->setProperty("recordState", QStringLiteral("stopped"));
    QTRY_COMPARE(m_stage->property("slide").toReal(), 0.0);
}

void TestRecordStage::animateOffOrReducedMotionStillsTheRecord()
{
    m_stage->setProperty("animate", false);
    m_stage->setProperty("recordState", QStringLiteral("playing"));
    QVERIFY(!m_stage->property("spinning").toBool());
    QVERIFY(!m_stage->property("turning").toBool());
    QCOMPARE(m_stage->property("slide").toReal(), 0.45); // out at once, static

    m_stage->setProperty("animate", true);
    QTRY_VERIFY(m_stage->property("turning").toBool());

    call("setReducedMotion", true);
    QVERIFY(!m_stage->property("motion").toBool());
    QTRY_VERIFY(!m_stage->property("turning").toBool());
    QVERIFY(!m_stage->property("settling").toBool());
    m_stage->setProperty("recordState", QStringLiteral("stopped"));
    QCOMPARE(m_stage->property("slide").toReal(), 0.0);
    call("setReducedMotion", false);
}

void TestRecordStage::albumChangeSlidesInSwapsAndSlidesOut()
{
    m_stage->setProperty("recordState", QStringLiteral("playing"));
    QTRY_COMPARE(m_stage->property("slide").toReal(), 0.45);

    // NowPlayingMusicController emits albumChanging before the cover binding moves.
    call("change", m_coverB);
    m_stage->setProperty("coverUrl", m_coverB);
    QVERIFY(m_stage->property("changing").toBool());
    QCOMPARE(m_stage->property("phase").toString(), QStringLiteral("in"));
    QCOMPARE(m_stage->property("shownCover").toString(), m_coverA); // not before it is in
    QVERIFY(!m_stage->property("spinning").toBool());

    QTRY_COMPARE(m_stage->property("slide").toReal(), 0.0);
    QTRY_COMPARE(m_stage->property("shownCover").toString(), m_coverB);
    QTRY_COMPARE(m_stage->property("phase").toString(), QString());
    QTRY_VERIFY(!m_stage->property("changing").toBool());
    QTRY_COMPARE(m_stage->property("slide").toReal(), 0.45);
    QTRY_VERIFY(m_stage->property("turning").toBool());
}

void TestRecordStage::changeWhileStoppedOrInFlightSwapsAtOnce()
{
    call("change", m_coverB);
    QVERIFY(!m_stage->property("changing").toBool());
    QCOMPARE(m_stage->property("shownCover").toString(), m_coverB);

    m_stage->setProperty("recordState", QStringLiteral("playing"));
    m_stage->setProperty("holdIn", true);
    QCOMPARE(m_stage->property("slide").toReal(), 0.0);
    QVERIFY(!m_stage->property("spinning").toBool());
    call("change", m_coverA);
    QVERIFY(!m_stage->property("changing").toBool());
    QCOMPARE(m_stage->property("shownCover").toString(), m_coverA);

    // The flight has landed: out it comes.
    m_stage->setProperty("holdIn", false);
    QTRY_COMPARE(m_stage->property("slide").toReal(), 0.45);
}

void TestRecordStage::sleeveRectMapsTheSleeve()
{
    QCOMPARE(call("sleeveWidth").toReal(), 300.0);
    QCOMPARE(m_stage->property("implicitWidth").toReal(), 435.0);
    QCOMPARE(m_stage->property("implicitHeight").toReal(), 300.0);
}

void TestRecordStage::changeToBlankCoverHidesThePreviousCover()
{
    m_stage->setProperty("recordState", QStringLiteral("stopped"));

    QQuickItem *previousImage = m_root->findChild<QQuickItem *>(QStringLiteral("previousCoverImage"));
    QVERIFY(previousImage);

    // Move to a second cover while stopped (an immediate swap), so the
    // outgoing cover (coverA) becomes the underlay and has time to reach
    // Ready underneath it.
    call("change", m_coverB);
    QCOMPARE(m_stage->property("shownCover").toString(), m_coverB);
    QTRY_COMPARE(previousImage->property("source").toString(), m_coverA);
    QTRY_COMPARE(previousImage->property("opacity").toReal(), 1.0);

    // A later album has no art at all: nothing to fade to, so the old cover
    // must not linger under a blank stage.
    call("change", QString());
    QCOMPARE(m_stage->property("shownCover").toString(), QString());
    QVERIFY(previousImage->property("source").toString().isEmpty());
    QTRY_COMPARE(previousImage->property("opacity").toReal(), 0.0);
}

QTEST_MAIN(TestRecordStage)
#include "tst_record_stage.moc"
