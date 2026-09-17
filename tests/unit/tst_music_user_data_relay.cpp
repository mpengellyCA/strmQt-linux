#include <QtTest>

#include "app/music/MusicRepository.h"
#include "app/music/MusicUserDataRelay.h"
#include "app/music/models/AlbumGridModel.h"
#include "app/music/models/TrackListModel.h"
#include "server/emby/EmbyClient.h"

using namespace strmqt;
using namespace strmqt::music;

namespace {
QVariant role(const QAbstractItemModel &model, int row, const QByteArray &name)
{
    return model.data(model.index(row, 0), model.roleNames().key(name));
}

Track track(const QString &id)
{
    Track t;
    t.id = id;
    t.title = id;
    return t;
}
} // namespace

class MusicUserDataRelayTest : public QObject
{
    Q_OBJECT

private slots:
    void favouritePatchesEveryRegisteredModel();
    void liveEntriesCarryEveryField();
    void destroyedModelsDropOut();
};

void MusicUserDataRelayTest::favouritePatchesEveryRegisteredModel()
{
    emby::EmbyClient client;
    MusicRepository repository(&client);
    MusicUserDataRelay relay(&repository);
    TrackListModel tracks;
    AlbumGridModel albums;
    tracks.setItems({track("t1"), track("t2")});
    Album album;
    album.id = QStringLiteral("t1"); // same id in both models: both must patch
    albums.setItems({album});
    relay.addModel(&tracks);
    relay.addModel(&albums);

    relay.onFavouriteChanged(QStringLiteral("t1"), true);
    QVERIFY(role(tracks, 0, "favourite").toBool());
    QVERIFY(!role(tracks, 1, "favourite").toBool());
    QVERIFY(role(albums, 0, "favourite").toBool());

    relay.onPlayedChanged(QStringLiteral("t2"), true);
    QVERIFY(role(tracks, 1, "played").toBool());
}

void MusicUserDataRelayTest::liveEntriesCarryEveryField()
{
    emby::EmbyClient client;
    MusicRepository repository(&client);
    MusicUserDataRelay relay(&repository);
    TrackListModel tracks;
    tracks.setItems({track("t1")});
    relay.addModel(&tracks);

    relay.onUserDataPatched({QVariantMap{{"itemId", "t1"}, {"played", true}, {"favorite", true},
                                         {"positionTicks", Q_INT64_C(120000000)}, {"playCount", 3}},
                             QVariantMap{{"played", true}}}); // no id: ignored
    QVERIFY(role(tracks, 0, "favourite").toBool());
    QVERIFY(role(tracks, 0, "played").toBool());
    QCOMPARE(role(tracks, 0, "playCount").toInt(), 3);
    QCOMPARE(role(tracks, 0, "positionMs").toLongLong(), Q_INT64_C(12000));
}

void MusicUserDataRelayTest::destroyedModelsDropOut()
{
    // Binding ruling P1-4: the original version of this test asserted nothing
    // meaningful (only that applying a patch after freeing the sole model
    // didn't crash). This version keeps that crash check but also registers a
    // second, live model so we can assert the relay still delivers patches to
    // surviving models after a QPointer-tracked one is destroyed.
    emby::EmbyClient client;
    MusicRepository repository(&client);
    MusicUserDataRelay relay(&repository);

    auto *doomed = new TrackListModel;
    doomed->setItems({track("t1")});
    relay.addModel(doomed);

    TrackListModel survivor;
    survivor.setItems({track("t1")});
    relay.addModel(&survivor);

    delete doomed;

    relay.onFavouriteChanged(QStringLiteral("t1"), true); // must not touch freed memory
    QVERIFY(role(survivor, 0, "favourite").toBool());
}

QTEST_GUILESS_MAIN(MusicUserDataRelayTest)
#include "tst_music_user_data_relay.moc"
