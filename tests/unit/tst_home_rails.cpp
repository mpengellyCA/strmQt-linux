#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "QmlShimStaging.h"
#include "app/controllers/HomeController.h"
#include "app/models/MediaItemModel.h"
#include "input/FocusNavigator.h"
#include "server/emby/EmbyClient.h"

using namespace strmqt;

namespace {

// Long enough for a key's focus move and the list's move animation
// (Theme.animNormalMs) to finish.
constexpr int kSettleMs = 400;

const char *kSource = R"QML(
import QtQuick

Item {
    id: root

    width: 640
    height: 480
    property int createdCount: 0
    property int destroyedCount: 0
    property int modelRoleCount: 0

    ListView {
        anchors.fill: parent
        model: HomeCtl.rails
        interactive: false

        delegate: Item {
            required property string railKey
            required property string title
            required property var railModel
            required property bool library
            required property bool wide
            required property string genreId
            width: ListView.view.width
            height: 80
            objectName: "rail-" + railKey

            Component.onCompleted: {
                root.createdCount = root.createdCount + 1
                if (railModel)
                    root.modelRoleCount = root.modelRoleCount + 1
            }
            Component.onDestruction: root.destroyedCount = root.destroyedCount + 1
        }
    }
}
)QML";

// The real HomePage, staged with the real control library and driven by the
// real FocusNavigator, so the stage's place in the D-pad graph is asserted
// rather than read. Main.qml's chrome is two plain stops in the places the
// navigation rail and the header occupy; Down out of the header is Main's
// TopBar.dismissed -> focusCurrentPage(), which is the page taking focus.
const char *kPageProbe = R"QML(
import QtQuick
import StrmQt

Item {
    id: root
    width: 1280
    height: 720

    Item {
        id: navRail
        objectName: "navRail"
        width: 72
        height: parent.height
        activeFocusOnTab: true
    }
    Item {
        id: header
        objectName: "header"
        x: 72
        width: parent.width - 72
        height: 56
        activeFocusOnTab: true
        Keys.onDownPressed: home.forceActiveFocus()
    }
    HomePage {
        id: home
        objectName: "home"
        x: 72
        y: 56
        width: parent.width - 72
        height: parent.height - 56
        focus: true
        // As the StackView names them in Main.qml.
        property Item navLeft: navRail
        property Item navUp: header
    }
}
)QML";

class StubActions : public QObject
{
    Q_OBJECT
public:
    QString last;
    Q_INVOKABLE QVariantMap itemCapabilities(const QVariant &value) const
    {
        const QVariantMap map = value.toMap();
        const bool valid = !map.value(QStringLiteral("itemId")).toString().isEmpty();
        return {{QStringLiteral("valid"), valid},
                {QStringLiteral("episode"),
                 map.value(QStringLiteral("type")).toString() == QLatin1String("Episode")},
                {QStringLiteral("play"), valid},
                {QStringLiteral("resume"), valid && map.value(QStringLiteral("resumable")).toBool()}};
    }
    Q_INVOKABLE void play(const QVariant &value)
    {
        last = QStringLiteral("play ") + value.toMap().value(QStringLiteral("itemId")).toString();
    }
    Q_INVOKABLE void openDetails(const QVariant &value)
    {
        last = QStringLiteral("details ") + value.toMap().value(QStringLiteral("itemId")).toString();
    }
    Q_INVOKABLE void togglePlayed(const QVariant &) {}
    Q_INVOKABLE void toggleFavorite(const QVariant &) {}
    Q_INVOKABLE void browseGenre(const QString &, const QString &) {}
};

class StubPrefs : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString densityMode MEMBER densityMode CONSTANT)
    Q_PROPERTY(QString themeAccent MEMBER themeAccent CONSTANT)
    Q_PROPERTY(bool reducedMotion MEMBER reducedMotion CONSTANT)
    Q_PROPERTY(bool backdropEnabled MEMBER backdropEnabled CONSTANT)
    Q_PROPERTY(int backdropOpacity MEMBER backdropOpacity CONSTANT)
    Q_PROPERTY(bool backdropKenBurns MEMBER backdropKenBurns CONSTANT)
public:
    QString densityMode = QStringLiteral("tv");
    QString themeAccent = QStringLiteral("projection");
    // Motion on: the view's own move animation is what a range that changed
    // with the cursor lost to.
    bool reducedMotion = false;
    bool backdropEnabled = true;
    int backdropOpacity = 18;
    bool backdropKenBurns = false;
};

class StubPlaylists : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList playlists READ playlists CONSTANT)
    Q_PROPERTY(bool playlistsComplete READ complete CONSTANT)
public:
    QVariantList playlists() const { return {}; }
    bool complete() const { return true; }
    Q_INVOKABLE void ensureAllPlaylists() {}
signals:
    void actionSucceeded(const QString &message);
    void actionFailed(const QString &message);
};

class FlatImages : public QQuickImageProvider
{
public:
    FlatImages() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString &, QSize *size, const QSize &requested) override
    {
        QImage image(requested.width() > 0 ? requested.width() : 320,
                     requested.height() > 0 ? requested.height() : 180, QImage::Format_RGB32);
        image.fill(Qt::darkGray);
        if (size)
            *size = image.size();
        return image;
    }
};

QString stagePageModule(const QTemporaryDir &dir)
{
    const QString modulePath = dir.filePath(QStringLiteral("StrmQt"));
    if (!QDir().mkpath(modulePath))
        return {};
    QByteArray qmldir = "module StrmQt\n"
                        "singleton Theme 1.0 Theme.qml\n"
                        "singleton MediaKinds 1.0 MediaKinds.qml\n"
                        "singleton ModelUtils 1.0 ModelUtils.qml\n"
                        "singleton NavigationColumn 1.0 NavigationColumn.qml\n";
    const QString ui = QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/");
    for (const QString &name : {QStringLiteral("Theme.qml"), QStringLiteral("MediaKinds.qml"),
                                QStringLiteral("ModelUtils.qml")}) {
        if (!QFile::copy(ui + name, modulePath + QLatin1Char('/') + name))
            return {};
    }
    const QDir controls(ui + QStringLiteral("controls"));
    for (const QString &name : controls.entryList({QStringLiteral("*.qml")}, QDir::Files)) {
        if (!QFile::copy(controls.filePath(name), modulePath + QLatin1Char('/') + name))
            return {};
        if (name != QLatin1String("NavigationColumn.qml"))
            qmldir += name.chopped(4).toUtf8() + " 1.0 " + name.toUtf8() + '\n';
    }
    for (const QString &path : {QStringLiteral("shell/LoadingState.qml"),
                                QStringLiteral("shell/EmptyState.qml"),
                                QStringLiteral("pages/HomePage.qml")}) {
        const QString name = path.section(QLatin1Char('/'), -1);
        if (!QFile::copy(ui + path, modulePath + QLatin1Char('/') + name))
            return {};
        qmldir += name.chopped(4).toUtf8() + " 1.0 " + name.toUtf8() + '\n';
    }
    // CoverWash samples through CoverTint, which lives in the executable.
    QFile wash(modulePath + QStringLiteral("/CoverWash.qml"));
    if (!wash.open(QIODevice::WriteOnly))
        return {};
    wash.write("import QtQuick\nItem { property string source }\n");
    wash.close();
    qmldir += "CoverWash 1.0 CoverWash.qml\n";
    const QByteArray shims = strmqt::test::stageShims(modulePath);
    if (shims.isEmpty())
        return {};
    qmldir += shims;
    QFile file(modulePath + QStringLiteral("/qmldir"));
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write(qmldir);
    file.close();
    QFile probe(dir.filePath(QStringLiteral("HomePageProbe.qml")));
    if (!probe.open(QIODevice::WriteOnly))
        return {};
    probe.write(kPageProbe);
    probe.close();
    return probe.fileName();
}

QList<MediaItem> episodes(const QString &prefix, int count)
{
    QList<MediaItem> result;
    for (int i = 0; i < count; ++i) {
        MediaItem episode;
        episode.id = prefix + QString::number(i);
        episode.name = QStringLiteral("Episode %1").arg(i + 1);
        episode.type = QStringLiteral("Episode");
        episode.seriesName = QStringLiteral("Fixture Series");
        episode.parentIndexNumber = 1;
        episode.indexNumber = i + 1;
        episode.primaryImageTag = QStringLiteral("still");
        episode.runtimeTicks = 50LL * 60 * kTicksPerSecond;
        result << episode;
    }
    return result;
}

MediaItem item(const QString &id)
{
    MediaItem result;
    result.id = id;
    result.name = id;
    result.type = QStringLiteral("Movie");
    return result;
}

} // namespace

class HomeRailsTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void childContentUpdatesKeepRailDelegates();
    void featuredIsResumeThenNextUpThenNothing();
    void featuredArtIsWideOrNone();
    void stageSitsInTheFocusPathAndKeepsTheFirstRailOnScreen();
};

void HomeRailsTest::initTestCase()
{
    setEmbyImageSourceNamespace(QStringLiteral("test-session"));
}

void HomeRailsTest::childContentUpdatesKeepRailDelegates()
{
    emby::EmbyClient client;
    HomeController home(&client);
    home.resume()->setItems({item(QStringLiteral("resume-a"))});
    home.nextUp()->setItems({item(QStringLiteral("next-a"))});
    QCOMPARE(home.rails()->rowCount(), 2);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("HomeRailsProbe.qml"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(kSource);
    file.close();

    QQuickView view;
    view.rootContext()->setContextProperty(QStringLiteral("HomeCtl"), &home);
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setSource(QUrl::fromLocalFile(path));
    QVERIFY2(view.status() == QQuickView::Ready,
             qPrintable(view.errors().isEmpty() ? QStringLiteral("no root object")
                                                : view.errors().first().toString()));
    QQuickItem *root = view.rootObject();
    QVERIFY(root);
    view.resize(640, 480);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTRY_COMPARE(root->property("createdCount").toInt(), 2);
    QCOMPARE(root->property("modelRoleCount").toInt(), 2);
    QCOMPARE(root->property("destroyedCount").toInt(), 0);

    QSignalSpy resetSpy(home.rails(), &QAbstractItemModel::modelReset);
    QSignalSpy insertedSpy(home.rails(), &QAbstractItemModel::rowsInserted);
    QSignalSpy removedSpy(home.rails(), &QAbstractItemModel::rowsRemoved);
    QSignalSpy movedSpy(home.rails(), &QAbstractItemModel::rowsMoved);
    QSignalSpy changedSpy(home.rails(), &QAbstractItemModel::dataChanged);

    // The child model replaces every card at the same cardinality. Its own
    // modelReset updates that rail, but the vertical descriptor model is quiet.
    home.resume()->setItems({item(QStringLiteral("resume-b"))});
    QCoreApplication::processEvents();
    QCOMPARE(root->property("createdCount").toInt(), 2);
    QCOMPARE(root->property("destroyedCount").toInt(), 0);

    // Even a real 1 -> 2 child count change leaves the set of rails unchanged.
    // HomeController does run its descriptor sync, which must remain a no-op.
    home.resume()->setItems(
        {item(QStringLiteral("resume-c")), item(QStringLiteral("resume-d"))});
    QCoreApplication::processEvents();
    QCOMPARE(home.rails()->rowCount(), 2);
    QCOMPARE(root->property("createdCount").toInt(), 2);
    QCOMPARE(root->property("destroyedCount").toInt(), 0);
    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(insertedSpy.count(), 0);
    QCOMPARE(removedSpy.count(), 0);
    QCOMPARE(movedSpy.count(), 0);
    QCOMPARE(changedSpy.count(), 0);

    // A genuine new rail is one row insertion; both existing delegates survive.
    home.favorites()->setItems({item(QStringLiteral("favorite-a"))});
    QTRY_COMPARE(home.rails()->rowCount(), 3);
    QTRY_COMPARE(root->property("createdCount").toInt(), 3);
    QCOMPARE(root->property("destroyedCount").toInt(), 0);
    QCOMPARE(insertedSpy.count(), 1);
    QCOMPARE(resetSpy.count(), 0);
}

// The Home stage features one item, chosen in C++ from rails Home already
// holds: the first Continue Watching row, else the first Next Up row, else the
// newest arrival (that last branch needs the latest rails a refresh builds, so
// tst_controllers covers it against the mock server). Nothing to feature is an
// empty map, which the page reads as "no stage" rather than an empty band.
void HomeRailsTest::featuredIsResumeThenNextUpThenNothing()
{
    emby::EmbyClient client;
    HomeController home(&client);
    QVERIFY(home.featured().isEmpty());

    QSignalSpy featuredSpy(&home, &HomeController::featuredChanged);
    home.nextUp()->setItems({item(QStringLiteral("next-a")), item(QStringLiteral("next-b"))});
    QCOMPARE(featuredSpy.count(), 1);
    QCOMPARE(home.featured().value(QStringLiteral("itemId")).toString(), QStringLiteral("next-a"));
    QCOMPARE(home.featured().value(QStringLiteral("featuredKind")).toString(),
             QStringLiteral("nextUp"));
    QVERIFY(!home.featured().value(QStringLiteral("featuredLabel")).toString().isEmpty());

    // Something in progress outranks what comes next.
    MediaItem started = item(QStringLiteral("resume-a"));
    started.runtimeTicks = 6000LL * 10'000'000;
    started.playbackPositionTicks = 600LL * 10'000'000;
    home.resume()->setItems({started});
    QCOMPARE(home.featured().value(QStringLiteral("itemId")).toString(),
             QStringLiteral("resume-a"));
    QCOMPARE(home.featured().value(QStringLiteral("featuredKind")).toString(),
             QStringLiteral("resume"));
    QCOMPARE(featuredSpy.count(), 2);

    // A change elsewhere that leaves the choice alone is not a change.
    home.nextUp()->setItems({item(QStringLiteral("next-c"))});
    home.favorites()->setItems({item(QStringLiteral("favorite-a"))});
    QCOMPARE(featuredSpy.count(), 2);

    // A live record moves the stage's progress with the rail's.
    home.resume()->updateUserData(QStringLiteral("resume-a"), false, false,
                                  1200LL * 10'000'000, 0);
    QCOMPARE(featuredSpy.count(), 3);
    QCOMPARE(home.featured().value(QStringLiteral("positionMs")).toLongLong(), 1'200'000LL);

    // The resumed item finishes and leaves the rail: Next Up takes the stage.
    home.resume()->setItems({});
    QCOMPARE(home.featured().value(QStringLiteral("itemId")).toString(), QStringLiteral("next-c"));
    home.nextUp()->setItems({});
    QVERIFY(home.featured().isEmpty());
    QCOMPARE(featuredSpy.count(), 5);
}

// The stage is full-bleed and wide: an episode borrows its series' fanart, and
// a film with only a poster gets no art at all rather than a stretched poster.
void HomeRailsTest::featuredArtIsWideOrNone()
{
    emby::EmbyClient client;
    HomeController home(&client);

    MediaItem episode;
    episode.id = QStringLiteral("ep-1");
    episode.type = QStringLiteral("Episode");
    episode.seriesName = QStringLiteral("Fixture Series");
    episode.primaryImageTag = QStringLiteral("still");
    episode.parentBackdropItemId = QStringLiteral("400001");
    episode.parentBackdropImageTag = QStringLiteral("fanart");
    episode.parentLogoItemId = QStringLiteral("400001");
    episode.parentLogoImageTag = QStringLiteral("logo");
    home.nextUp()->setItems({episode});
    QCOMPARE(home.featured().value(QStringLiteral("artUrl")).toString(),
             QStringLiteral("image://emby/test-session/400001/Backdrop/fanart"));
    QCOMPARE(home.featured().value(QStringLiteral("logoUrl")).toString(),
             QStringLiteral("image://emby/test-session/400001/Logo/logo"));

    MediaItem posterOnly = item(QStringLiteral("film-1"));
    posterOnly.primaryImageTag = QStringLiteral("poster");
    home.resume()->setItems({posterOnly});
    QCOMPARE(home.featured().value(QStringLiteral("itemId")).toString(), QStringLiteral("film-1"));
    QCOMPARE(home.featured().value(QStringLiteral("artUrl")).toString(), QString());

    MediaItem withFanart = posterOnly;
    withFanart.backdropImageTags = {QStringLiteral("own-fanart")};
    home.resume()->setItems({withFanart});
    QCOMPARE(home.featured().value(QStringLiteral("artUrl")).toString(),
             QStringLiteral("image://emby/test-session/film-1/Backdrop/own-fanart"));
}

void HomeRailsTest::stageSitsInTheFocusPathAndKeepsTheFirstRailOnScreen()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString probePath = stagePageModule(dir);
    QVERIFY(!probePath.isEmpty());

    emby::EmbyClient client;
    HomeController home(&client);
    QList<MediaItem> resume = episodes(QStringLiteral("r"), 3);
    resume[0].playbackPositionTicks = 20LL * 60 * kTicksPerSecond;
    home.resume()->setItems(resume);
    home.nextUp()->setItems(episodes(QStringLiteral("n"), 6));
    home.favorites()->setItems({item(QStringLiteral("f0")), item(QStringLiteral("f1"))});

    StubActions actions;
    StubPrefs prefs;
    StubPlaylists playlists;
    QQuickView view;
    view.engine()->addImportPath(dir.path());
    view.engine()->addImageProvider(QStringLiteral("emby"), new FlatImages);
    view.rootContext()->setContextProperty(QStringLiteral("HomeCtl"), &home);
    view.rootContext()->setContextProperty(QStringLiteral("Actions"), &actions);
    view.rootContext()->setContextProperty(QStringLiteral("Prefs"), &prefs);
    view.rootContext()->setContextProperty(QStringLiteral("PlaylistCtl"), &playlists);
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setSource(QUrl::fromLocalFile(probePath));
    QVERIFY2(view.status() == QQuickView::Ready,
             qPrintable(view.errors().isEmpty() ? QStringLiteral("no root object")
                                                : view.errors().first().toString()));
    // 1280x720 in TV density: the tightest screen the stage is sized for.
    view.resize(1280, 720);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    new FocusNavigator(&view, &view);

    QQuickItem *root = view.rootObject();
    QQuickItem *page = root->findChild<QQuickItem *>(QStringLiteral("home"));
    QQuickItem *header = root->findChild<QQuickItem *>(QStringLiteral("header"));
    QQuickItem *navRail = root->findChild<QQuickItem *>(QStringLiteral("navRail"));
    QVERIFY(page && header && navRail);
    QQuickItem *list = nullptr;
    QQuickItem *stage = nullptr;
    for (QQuickItem *child : page->findChildren<QQuickItem *>()) {
        if (child->property("model").value<QObject *>() == home.rails())
            list = child;
        if (child->property("navigationFocusKey").toString() == QLatin1String("home-hero"))
            stage = child;
    }
    QVERIFY(list && stage);

    const auto focused = [&view] { return view.activeFocusItem(); };
    const auto inside = [](const QQuickItem *ancestor, const QQuickItem *item) {
        for (const QQuickItem *p = item; p; p = p->parentItem())
            if (p == ancestor)
                return true;
        return false;
    };
    const auto railBounds = [&] {
        auto *current = list->property("currentItem").value<QQuickItem *>();
        if (!current)
            return QRectF();
        return QRectF(current->mapToItem(page, QPointF(0, 0)), current->size());
    };
    const auto press = [&view](Qt::Key key) {
        QTest::keyClick(&view, key);
        QTest::qWait(kSettleMs);
    };

    // Opens on the first rail, with the stage whole above it and that rail
    // whole on screen under it.
    QTRY_VERIFY(page->property("heroShown").toBool());
    QTRY_VERIFY(inside(list, focused()));
    QTRY_COMPARE(page->property("stageY").toReal(), 0.0);
    QCOMPARE(list->property("currentIndex").toInt(), 0);
    QVERIFY2(railBounds().bottom() <= page->height(),
             qPrintable(QStringLiteral("first rail ends at %1 of %2")
                            .arg(railBounds().bottom()).arg(page->height())));

    // Up from the first rail is the stage; Up again is the header, and Down
    // from the header comes back to the stage's button.
    press(Qt::Key_Up);
    QVERIFY(inside(stage, focused()));
    QQuickItem *stageButton = focused();
    press(Qt::Key_Up);
    QCOMPARE(focused(), header);
    press(Qt::Key_Down);
    QCOMPARE(focused(), stageButton);
    // Left from the stage is the navigation rail.
    for (int i = 0; i < 3 && focused() != navRail; ++i)
        press(Qt::Key_Left);
    QCOMPARE(focused(), navRail);
    press(Qt::Key_Right);
    if (!inside(stage, focused()))
        press(Qt::Key_Down); // the header lies to the right of the rail's top
    QVERIFY(inside(stage, focused()));

    // Down from the stage is the first rail, the stage still in place.
    press(Qt::Key_Down);
    QVERIFY(inside(list, focused()));
    QCOMPARE(list->property("currentIndex").toInt(), 0);
    QCOMPARE(page->property("stageY").toReal(), 0.0);

    // Down to the second rail brings that rail on screen (the view's own move
    // animation once beat a cursor-dependent range here and left it below
    // the window), and Up back to the first brings the stage back whole.
    press(Qt::Key_Down);
    QCOMPARE(list->property("currentIndex").toInt(), 1);
    QTRY_VERIFY(railBounds().top() >= 0 && railBounds().bottom() <= page->height());
    press(Qt::Key_Up);
    QCOMPARE(list->property("currentIndex").toInt(), 0);
    QTRY_COMPARE(page->property("stageY").toReal(), 0.0);

    // Focus memory: the stage is a semantic owner, and it restores the button
    // the user left from.
    press(Qt::Key_Up);
    QVERIFY(inside(stage, focused()));
    QQuickItem *firstButton = focused();
    press(Qt::Key_Right);
    QVERIFY(inside(stage, focused()));
    QVERIFY(focused() != firstButton);
    QQuickItem *moreInfo = focused();
    QVariant snapshot;
    QVERIFY(QMetaObject::invokeMethod(stage, "navigationFocusSnapshot",
                                      Q_RETURN_ARG(QVariant, snapshot)));
    QVERIFY(snapshot.toMap().value(QStringLiteral("valid")).toBool());
    QCOMPARE(snapshot.toMap().value(QStringLiteral("identity")).toString(), QStringLiteral("r0"));
    press(Qt::Key_Down);
    QVERIFY(inside(list, focused()));
    bool restored = false;
    QVERIFY(QMetaObject::invokeMethod(stage, "restoreNavigationFocus", Q_RETURN_ARG(bool, restored),
                                      Q_ARG(QVariant, snapshot.toMap().value(QStringLiteral("identity"))),
                                      Q_ARG(QVariant, snapshot.toMap().value(QStringLiteral("index")))));
    QVERIFY(restored);
    QCOMPARE(focused(), moreInfo);
    press(Qt::Key_Return);
    QCOMPARE(actions.last, QStringLiteral("details r0"));

    // Nothing left to feature: no stage, no band where it was, and the
    // keyboard it held goes to the rails rather than to nothing.
    home.resume()->setItems({});
    home.nextUp()->setItems({});
    QTRY_VERIFY(!page->property("heroShown").toBool());
    QTRY_VERIFY(inside(list, focused()));
    QVERIFY(!stage->isVisible());
    QTRY_VERIFY(railBounds().top() <= 60);
}

QTEST_MAIN(HomeRailsTest)
#include "tst_home_rails.moc"
