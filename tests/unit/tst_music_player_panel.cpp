// MusicPlayerPanel's Lyrics tab is the one tab that can vanish with no click
// or keypress of the user's own: NowPlayingMusicController::clearLyrics()
// runs as soon as the queue moves to a new track, and the new track's lyrics
// (if any) arrive back over the network afterwards. This measures the fix for
// the bug that shipped in review: a straight `currentTab` write on every
// `tabKeys` change threw the user back to Up next at *every* track boundary,
// even the ones where the next track has lyrics too, and — worse — left
// active focus on the now-hidden lyrics list, with no ring drawn and no
// longer even the tab shown. Measured on a real window (see the assertions
// below): Qt does NOT move focus anywhere on its own when the item holding it
// is hidden — it has to be moved explicitly, which is what the fix does.
//
// A stub NowPlayingMusicCtl/PlayerCtl/Input stand in for the context
// properties the real app wires up in main.cpp (ARCHITECTURE.md), following
// the same staged-module pattern as tst_record_stage.cpp and
// tst_crate_controls.cpp: real .qml sources copied into a temporary "StrmQt"
// module rather than a hand-rewritten substitute, so this measures the actual
// file that ships.
#include <QDir>
#include <QFile>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantList>

namespace {

const char *kProbe = R"QML(
import QtQuick
import StrmQt

Item {
    id: root
    width: 900
    height: 700
    focus: true

    MusicPlayerPanel {
        id: panel
        objectName: "panel"
        anchors.fill: parent
    }
}
)QML";

// Stands in for src/input/InputMap.h's Q_INVOKABLE noteInput(), the only
// member MusicPlayerPanel calls on it.
class StubInput : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE void noteInput(const QString &device) { Q_UNUSED(device); }
};

// Stands in for PlayerController's one property MusicPlayerPanel reads
// (src/app/controllers/PlayerController.h). null queue is deliberate: the
// Lyrics-tab fix under test does not touch the Up next tab, and TrackTable
// renders an empty "Nothing queued" list against a null model without error.
class StubPlayerCtl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariant queue READ queue CONSTANT)
public:
    using QObject::QObject;
    QVariant queue() const { return QVariant(); }
};

// Stands in for NowPlayingMusicController (src/app/controllers/music/
// NowPlayingMusicController.h), exposing exactly the members
// MusicPlayerPanel.qml reads, with lyricsAvailable/lyrics driveable from the
// test the way a real track change drives them.
class StubNowPlayingMusicCtl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString sourceKicker READ sourceKicker NOTIFY changed)
    Q_PROPERTY(QString album READ album NOTIFY changed)
    Q_PROPERTY(QString albumSummary READ albumSummary NOTIFY changed)
    Q_PROPERTY(QString coverUrl READ coverUrl NOTIFY changed)
    Q_PROPERTY(QObject *albumTracks READ albumTracks CONSTANT)
    Q_PROPERTY(int currentAlbumRow READ currentAlbumRow NOTIFY changed)
    Q_PROPERTY(bool lyricsAvailable READ lyricsAvailable NOTIFY lyricsChanged)
    Q_PROPERTY(QVariantList lyrics READ lyrics NOTIFY lyricsChanged)
    Q_PROPERTY(bool lyricsTimed READ lyricsTimed NOTIFY lyricsChanged)
    Q_PROPERTY(int currentLyricRow READ currentLyricRow NOTIFY changed)

public:
    using QObject::QObject;

    QString sourceKicker() const { return QStringLiteral("Playing from · Test Album"); }
    QString album() const { return QStringLiteral("Test Album"); }
    QString albumSummary() const { return QStringLiteral("1 track · 1 min"); }
    QString coverUrl() const { return QString(); }
    QObject *albumTracks() const { return nullptr; }
    int currentAlbumRow() const { return -1; }
    bool lyricsAvailable() const { return m_lyricsAvailable; }
    QVariantList lyrics() const { return m_lyrics; }
    bool lyricsTimed() const { return false; }
    int currentLyricRow() const { return -1; }

    Q_INVOKABLE void playAlbumFrom(int row) { Q_UNUSED(row); }

    // The two halves of a real track change, driveable independently: a
    // track move clears lyrics immediately (clearLyrics()); replacement
    // lyrics, if any, land later as a separate step once the request
    // resolves.
    void setLyricsAvailable(bool available)
    {
        if (m_lyricsAvailable == available)
            return;
        m_lyricsAvailable = available;
        m_lyrics = available ? QVariantList{QVariantMap{{QStringLiteral("timeMs"), -1},
                                                        {QStringLiteral("text"), QStringLiteral("La la la")}}}
                             : QVariantList{};
        emit lyricsChanged();
    }

signals:
    void changed();
    void lyricsChanged();

private:
    bool m_lyricsAvailable = false;
    QVariantList m_lyrics;
};

} // namespace

class TestMusicPlayerPanel : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void lyricsHiddenMidReadReclaimsFocusAndRestoresWhenAvailableAgain();
    void userChoiceMadeWhileHiddenIsNotOverriddenByLyricsReturning();

private:
    bool stage(const QString &sourceDir, const QString &file, const QString &modulePath, QByteArray &qmldir);

    QTemporaryDir m_dir;
    QQuickView *m_view = nullptr;
    QObject *m_root = nullptr;
    QQuickItem *m_panel = nullptr;
    QQuickItem *m_tabStrip = nullptr;
    QQuickItem *m_queueList = nullptr;
    QQuickItem *m_albumList = nullptr;
    QQuickItem *m_lyricList = nullptr;
    StubInput *m_input = nullptr;
    StubPlayerCtl *m_playerCtl = nullptr;
    StubNowPlayingMusicCtl *m_nowPlaying = nullptr;
};

bool TestMusicPlayerPanel::stage(const QString &sourceDir, const QString &file, const QString &modulePath,
                                 QByteArray &qmldir)
{
    if (!QFile::copy(sourceDir + file, modulePath + QLatin1Char('/') + file))
        return false;
    const QString type = QString(file).remove(QStringLiteral(".qml"));
    qmldir += type.toUtf8() + " 1.0 " + file.toUtf8() + '\n';
    return true;
}

void TestMusicPlayerPanel::initTestCase()
{
    QVERIFY(m_dir.isValid());
    const QString modulePath = m_dir.filePath(QStringLiteral("StrmQt"));
    QVERIFY(QDir().mkpath(modulePath));

    QByteArray qmldir = "module StrmQt\nsingleton Theme 1.0 Theme.qml\n";
    QVERIFY(QFile::copy(QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/Theme.qml"),
                        modulePath + QStringLiteral("/Theme.qml")));

    const QString controlsDir = QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/controls/");
    const QStringList controls = {
        QStringLiteral("FocusRing.qml"),       QStringLiteral("StrmIcon.qml"),
        QStringLiteral("StrmTooltip.qml"),     QStringLiteral("StrmIconButton.qml"),
        QStringLiteral("StrmImage.qml"),       QStringLiteral("StrmScrollBar.qml"),
        QStringLiteral("NavigationFocusRestorer.qml"), QStringLiteral("TrackRow.qml"),
        QStringLiteral("TrackTable.qml"),
    };
    for (const QString &file : controls)
        QVERIFY2(stage(controlsDir, file, modulePath, qmldir), qPrintable(file));

    const QString musicDir = QStringLiteral(STRMQT_SOURCE_DIR "/src/ui/music/");
    const QStringList music = {
        QStringLiteral("CrateKicker.qml"),
        QStringLiteral("MusicPlayerPanel.qml"),
    };
    for (const QString &file : music)
        QVERIFY2(stage(musicDir, file, modulePath, qmldir), qPrintable(file));

    QFile qmldirFile(modulePath + QStringLiteral("/qmldir"));
    QVERIFY(qmldirFile.open(QIODevice::WriteOnly));
    qmldirFile.write(qmldir);
    qmldirFile.close();

    QFile probe(m_dir.filePath(QStringLiteral("Probe.qml")));
    QVERIFY(probe.open(QIODevice::WriteOnly));
    probe.write(kProbe);
    probe.close();
}

void TestMusicPlayerPanel::init()
{
    m_input = new StubInput;
    m_playerCtl = new StubPlayerCtl;
    m_nowPlaying = new StubNowPlayingMusicCtl;

    m_view = new QQuickView;
    m_view->engine()->addImportPath(m_dir.path());
    m_view->engine()->rootContext()->setContextProperty(QStringLiteral("Input"), m_input);
    m_view->engine()->rootContext()->setContextProperty(QStringLiteral("PlayerCtl"), m_playerCtl);
    m_view->engine()->rootContext()->setContextProperty(QStringLiteral("NowPlayingMusicCtl"), m_nowPlaying);
    m_view->setSource(QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("Probe.qml"))));
    QVERIFY2(m_view->status() == QQuickView::Ready,
             qPrintable(m_view->errors().isEmpty() ? QStringLiteral("no root object")
                                                   : m_view->errors().first().toString()));
    m_view->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_view));

    m_root = m_view->rootObject();
    m_panel = m_root->findChild<QQuickItem *>(QStringLiteral("panel"));
    QVERIFY(m_panel);
    m_tabStrip = m_root->findChild<QQuickItem *>(QStringLiteral("tabStrip"));
    m_queueList = m_root->findChild<QQuickItem *>(QStringLiteral("queueList"));
    m_albumList = m_root->findChild<QQuickItem *>(QStringLiteral("albumList"));
    m_lyricList = m_root->findChild<QQuickItem *>(QStringLiteral("lyricList"));
    QVERIFY(m_tabStrip);
    QVERIFY(m_queueList);
    QVERIFY(m_albumList);
    QVERIFY(m_lyricList);
}

void TestMusicPlayerPanel::cleanup()
{
    delete m_view;
    m_view = nullptr;
    m_root = nullptr;
    m_panel = nullptr;
    m_tabStrip = nullptr;
    m_queueList = nullptr;
    m_albumList = nullptr;
    m_lyricList = nullptr;
    // Owned by nothing but this test: the context properties outlive the
    // QQuickView that read them.
    delete m_input;
    delete m_playerCtl;
    delete m_nowPlaying;
    m_input = nullptr;
    m_playerCtl = nullptr;
    m_nowPlaying = nullptr;
}

void TestMusicPlayerPanel::lyricsHiddenMidReadReclaimsFocusAndRestoresWhenAvailableAgain()
{
    m_nowPlaying->setLyricsAvailable(true);
    QCOMPARE(m_panel->property("tabKeys").toList().size(), 3);

    m_panel->setProperty("_desiredTab", QStringLiteral("lyrics"));
    QCOMPARE(m_panel->property("currentTab").toString(), QStringLiteral("lyrics"));
    QMetaObject::invokeMethod(m_panel, "focusContent");
    QVERIFY(m_lyricList->hasActiveFocus());

    // The queue moves to a track with no lyrics yet known: clearLyrics().
    m_nowPlaying->setLyricsAvailable(false);
    QCOMPARE(m_panel->property("currentTab").toString(), QStringLiteral("upNext"));
    // Never left stranded on the now-hidden (but still technically focused,
    // measured: Qt does not move focus on its own) lyrics list.
    QVERIFY(m_tabStrip->hasActiveFocus());
    QVERIFY(!m_lyricList->hasActiveFocus());

    // The new track's lyrics arrive: the user's pick — still "lyrics" — is
    // shown again on its own, with no further action from the user.
    m_nowPlaying->setLyricsAvailable(true);
    QCOMPARE(m_panel->property("currentTab").toString(), QStringLiteral("lyrics"));
}

void TestMusicPlayerPanel::userChoiceMadeWhileHiddenIsNotOverriddenByLyricsReturning()
{
    m_nowPlaying->setLyricsAvailable(true);
    m_panel->setProperty("_desiredTab", QStringLiteral("lyrics"));
    m_nowPlaying->setLyricsAvailable(false);
    QCOMPARE(m_panel->property("currentTab").toString(), QStringLiteral("upNext"));

    // The user does not just wait: they pick Album explicitly while Lyrics is
    // off the strip.
    m_panel->setProperty("_desiredTab", QStringLiteral("album"));
    QCOMPARE(m_panel->property("currentTab").toString(), QStringLiteral("album"));

    // Lyrics becoming available again must not snatch the strip away from a
    // pick the user made in the meantime.
    m_nowPlaying->setLyricsAvailable(true);
    QCOMPARE(m_panel->property("currentTab").toString(), QStringLiteral("album"));
}

QTEST_MAIN(TestMusicPlayerPanel)
#include "tst_music_player_panel.moc"
