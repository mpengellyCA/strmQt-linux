#include <QSignalSpy>
#include <QtTest>

#include "server/emby/EmbyWebSocket.h"

#include <QJsonObject>

using namespace strmqt::emby;

// Parsing and URL construction for Emby's event socket. Every shape below was
// either recorded from Emby 4.9.5.0 on 2026-08-22 or is deliberate server drift:
// AGENTS.md requires tolerant parsing, so a missing or wrong-typed field must
// default, never crash.
class EmbyWebSocketTest : public QObject
{
    Q_OBJECT

private slots:
    void socketUrlDerivesSchemeAndPath_data();
    void socketUrlDerivesSchemeAndPath();
    void libraryChangedRecordedShape();
    void libraryChangedTolerantOfDrift();
    void userDataChangedRecordedShape();
    void userDataChangedTolerantOfDrift();
    void dispatchEmitsSignals();
    void malformedFramesAreDropped();
    void unhandledTypesReachTheEscapeHatch();
};

void EmbyWebSocketTest::socketUrlDerivesSchemeAndPath_data()
{
    QTest::addColumn<QString>("base");
    QTest::addColumn<QString>("expected");

    QTest::newRow("https") << "https://strm.example.ca"
                           << "wss://strm.example.ca/embywebsocket";
    QTest::newRow("http") << "http://127.0.0.1:8096" << "ws://127.0.0.1:8096/embywebsocket";
    QTest::newRow("trailing slash") << "https://example.ca/"
                                    << "wss://example.ca/embywebsocket";
    QTest::newRow("sub path") << "https://example.ca/emby"
                              << "wss://example.ca/emby/embywebsocket";
}

void EmbyWebSocketTest::socketUrlDerivesSchemeAndPath()
{
    QFETCH(QString, base);
    QFETCH(QString, expected);

    const QUrl url = EmbyWebSocket::socketUrl(QUrl(base), QStringLiteral("tok"),
                                              QStringLiteral("dev"));
    QCOMPARE(url.toString(QUrl::RemoveQuery), expected);
    QCOMPARE(url.query(), QStringLiteral("api_key=tok&deviceId=dev"));
}

void EmbyWebSocketTest::libraryChangedRecordedShape()
{
    // Verbatim Data from a recorded Emby 4.9.5.0 LibraryChanged.
    const auto json = QByteArrayLiteral(
        R"({"FoldersAddedTo":[],"FoldersRemovedFrom":[],"ItemsAdded":[],"ItemsRemoved":[],)"
        R"("ItemsUpdated":["1855520"],"CollectionFolders":[],"IsEmpty":false})");
    const QJsonObject data = QJsonDocument::fromJson(json).object();

    QStringList added;
    QStringList removed;
    QStringList updated;
    EmbyWebSocket::parseLibraryChanged(data, &added, &removed, &updated);

    QVERIFY(added.isEmpty());
    QVERIFY(removed.isEmpty());
    QCOMPARE(updated, QStringList{QStringLiteral("1855520")});
}

void EmbyWebSocketTest::libraryChangedTolerantOfDrift()
{
    QStringList added;
    QStringList removed;
    QStringList updated;

    // Everything missing.
    EmbyWebSocket::parseLibraryChanged(QJsonObject{}, &added, &removed, &updated);
    QVERIFY(added.isEmpty() && removed.isEmpty() && updated.isEmpty());

    // Wrong types where arrays belong, plus object and numeric ids, plus the
    // folder fields that fold into added/removed.
    const auto json = QByteArrayLiteral(
        R"({"ItemsAdded":[{"Id":"a"},{"ItemId":"b"},7,null,"c"],)"
        R"("ItemsRemoved":"not-an-array",)"
        R"("FoldersAddedTo":["a","d"],)"
        R"("FoldersRemovedFrom":["e"],)"
        R"("ItemsUpdated":{"nope":true},)"
        R"("CollectionFolders":["4"]})");
    const QJsonObject data = QJsonDocument::fromJson(json).object();
    EmbyWebSocket::parseLibraryChanged(data, &added, &removed, &updated);

    QCOMPARE(added, (QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("7"),
                                 QStringLiteral("c"), QStringLiteral("d")}));
    QCOMPARE(removed, QStringList{QStringLiteral("e")});
    QCOMPARE(updated, QStringList{QStringLiteral("4")});
}

void EmbyWebSocketTest::userDataChangedRecordedShape()
{
    // Verbatim Data from a recorded Emby 4.9.5.0 UserDataChanged.
    const auto json = QByteArrayLiteral(
        R"({"UserId":"5ba53c73022e4dc3a30442ba5d4eba90","UserDataList":[)"
        R"({"PlaybackPositionTicks":0,"PlayCount":1,"IsFavorite":false,)"
        R"("LastPlayedDate":"2026-08-22T14:18:18.0000000Z","Played":true,"ItemId":"1855520"}]})");
    const QJsonObject data = QJsonDocument::fromJson(json).object();

    const auto entries = EmbyWebSocket::parseUserDataList(data);
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().itemId, QStringLiteral("1855520"));
    QVERIFY(entries.first().played);
    QVERIFY(!entries.first().favorite);
    QCOMPARE(entries.first().playCount, 1);
    QCOMPARE(entries.first().playbackPositionTicks, 0);
}

void EmbyWebSocketTest::userDataChangedTolerantOfDrift()
{
    // No list at all.
    QVERIFY(EmbyWebSocket::parseUserDataList(QJsonObject{}).isEmpty());

    const auto json = QByteArrayLiteral(
        R"({"UserDataList":[)"
        R"("a string, not an object",)"
        R"({"Played":true},)"                                  // no ItemId → skipped
        R"({"ItemId":"only-an-id"},)"                          // everything else defaults
        R"({"ItemId":"full","Played":true,"IsFavorite":true,)"
        R"("PlaybackPositionTicks":123456789,"PlayCount":3}]})");
    const QJsonObject data = QJsonDocument::fromJson(json).object();

    const auto entries = EmbyWebSocket::parseUserDataList(data);
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries[0].itemId, QStringLiteral("only-an-id"));
    QVERIFY(!entries[0].played);
    QVERIFY(!entries[0].favorite);
    QCOMPARE(entries[0].playbackPositionTicks, 0);
    QCOMPARE(entries[1].itemId, QStringLiteral("full"));
    QVERIFY(entries[1].played);
    QVERIFY(entries[1].favorite);
    QCOMPARE(entries[1].playbackPositionTicks, 123456789);
    QCOMPARE(entries[1].playCount, 3);
}

void EmbyWebSocketTest::dispatchEmitsSignals()
{
    EmbyWebSocket socket;
    QSignalSpy library(&socket, &EmbyWebSocket::libraryChanged);
    QSignalSpy userData(&socket, &EmbyWebSocket::userDataChanged);
    QSignalSpy raw(&socket, &EmbyWebSocket::messageReceived);

    socket.handleTextMessage(QStringLiteral(
        R"({"MessageType":"LibraryChanged","Data":{"ItemsAdded":["x"],"ItemsUpdated":["y"]}})"));
    QCOMPARE(library.size(), 1);
    QCOMPARE(library.first().at(0).toStringList(), QStringList{QStringLiteral("x")});
    QCOMPARE(library.first().at(2).toStringList(), QStringList{QStringLiteral("y")});

    socket.handleTextMessage(QStringLiteral(
        R"({"MessageType":"UserDataChanged","Data":{"UserDataList":[{"ItemId":"z"}]}})"));
    QCOMPARE(userData.size(), 1);
    QCOMPARE(userData.first().at(0).toStringList(), QStringList{QStringLiteral("z")});

    // Every frame reaches the escape hatch, handled or not.
    QCOMPARE(raw.size(), 2);
}

void EmbyWebSocketTest::malformedFramesAreDropped()
{
    EmbyWebSocket socket;
    QSignalSpy library(&socket, &EmbyWebSocket::libraryChanged);
    QSignalSpy userData(&socket, &EmbyWebSocket::userDataChanged);
    QSignalSpy raw(&socket, &EmbyWebSocket::messageReceived);

    for (const char *frame : {"", "not json at all", "[1,2,3]", "{}", R"({"Data":{}})",
                              R"({"MessageType":"LibraryChanged","Data":"a string"})",
                              R"({"MessageType":"UserDataChanged"})"}) {
        socket.handleTextMessage(QString::fromLatin1(frame));
    }

    // The last two are well-formed frames with useless payloads: LibraryChanged
    // still means "something moved", UserDataChanged with no list means nothing.
    QCOMPARE(library.size(), 1);
    QVERIFY(library.first().at(0).toStringList().isEmpty());
    QCOMPARE(userData.size(), 0);
    QCOMPARE(raw.size(), 2);
    QVERIFY(!socket.isConnected());
}

void EmbyWebSocketTest::unhandledTypesReachTheEscapeHatch()
{
    EmbyWebSocket socket;
    QSignalSpy raw(&socket, &EmbyWebSocket::messageReceived);

    // Remote control is M12 and RefreshProgress is scan noise; neither is
    // handled, but both must be visible to a future dispatch entry.
    socket.handleTextMessage(QStringLiteral(
        R"({"MessageType":"RefreshProgress","Data":{"ItemId":"4","Progress":"96.9"}})"));
    socket.handleTextMessage(
        QStringLiteral(R"({"MessageType":"GeneralCommand","Data":{"Name":"Back"}})"));
    // Data is a bare number here, which must not upset the QJsonObject contract.
    socket.handleTextMessage(QStringLiteral(R"({"MessageType":"ForceKeepAlive","Data":60})"));

    QCOMPARE(raw.size(), 3);
    QCOMPARE(raw.at(0).at(0).toString(), QStringLiteral("RefreshProgress"));
    QCOMPARE(raw.at(0).at(1).toJsonObject().value(QStringLiteral("ItemId")).toString(),
             QStringLiteral("4"));
    QCOMPARE(raw.at(1).at(0).toString(), QStringLiteral("GeneralCommand"));
    QCOMPARE(raw.at(2).at(0).toString(), QStringLiteral("ForceKeepAlive"));
    QVERIFY(raw.at(2).at(1).toJsonObject().isEmpty());
}

QTEST_GUILESS_MAIN(EmbyWebSocketTest)
#include "tst_emby_websocket.moc"
