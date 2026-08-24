#include <QtTest>

#include "MockEmbyServer.h"
#include "server/emby/EmbyClient.h"

using namespace strmqt;
using emby::EmbyClient;

namespace {

QString fixturePath(const QString &name)
{
    return QStringLiteral(STRMQT_FIXTURES_DIR "/") + name;
}

// Fixture user id from auth_by_name.json — path segments depend on it.
const auto kUserId = QStringLiteral("a1b2c3d4e5f60718293a4b5c6d7e8f90");
const auto kToken = QStringLiteral("not-a-real-token-fixture-only");

template<class T> Result<T> waitFor(QFuture<Result<T>> future)
{
    // The mock server runs on this thread, so spin the event loop instead of blocking.
    if (!QTest::qWaitFor([&] { return future.isFinished(); }, 5000))
        return Result<T>::failure(QStringLiteral("timeout waiting for future"));
    return future.result();
}

} // namespace

class EmbyClientTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void publicInfoWithoutAuth();
    void authenticateAdoptsSession();
    void authenticatedCallsCarryToken();
    void railsAndItems();
    void mediaDetailFieldsRequested();
    void httpErrorSurfaces();
    void invalidJsonSurfaces();
    void unauthenticatedCallsFailFast();
    void imageUrlBuilder();
    void deviceProfileCoversLosslessAudio();
    void sessionChangeCancelsOutstandingRequests();
    void reassertingTheSameIdentityKeepsRequestsAlive();
    void renameCannotChainAWriteAcrossServers();

private:
    MockEmbyServer *m_mock = nullptr;
    EmbyClient *m_client = nullptr;
};

void EmbyClientTest::init()
{
    m_mock = new MockEmbyServer(this);
    QVERIFY(m_mock->start());

    m_client = new EmbyClient(this);
    m_client->setBaseUrl(m_mock->baseUrl());
    m_client->setDeviceId(QStringLiteral("test-device-id"));
    m_client->setDeviceName(QStringLiteral("test-host"));
}

void EmbyClientTest::cleanup()
{
    delete m_client;
    m_client = nullptr;
    delete m_mock;
    m_mock = nullptr;
}

void EmbyClientTest::publicInfoWithoutAuth()
{
    QVERIFY(m_mock->addRouteFromFile(QStringLiteral("GET"), QStringLiteral("/System/Info/Public"),
                                     fixturePath(QStringLiteral("system_info_public.json"))));

    const auto result = waitFor(m_client->publicSystemInfo());
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.value.name, QStringLiteral("emby.example.org"));
    QCOMPARE(result.value.version, QStringLiteral("4.9.5.0"));

    // No token yet → no token header on the wire.
    const auto request =
        m_mock->lastRequestFor(QStringLiteral("GET"), QStringLiteral("/System/Info/Public"));
    QVERIFY(!request.headers.contains("x-emby-token"));
    QVERIFY(request.headers.value("x-emby-authorization").contains("Client=\"StrmQt\""));
    QVERIFY(request.headers.value("x-emby-authorization").contains("DeviceId=\"test-device-id\""));
}

void EmbyClientTest::authenticateAdoptsSession()
{
    QVERIFY(m_mock->addRouteFromFile(QStringLiteral("POST"),
                                     QStringLiteral("/Users/AuthenticateByName"),
                                     fixturePath(QStringLiteral("auth_by_name.json"))));

    const auto result =
        waitFor(m_client->authenticateByName(QStringLiteral("mike"), QStringLiteral("pw")));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.value.user.name, QStringLiteral("mike"));
    QCOMPARE(m_client->accessToken(), kToken);
    QCOMPARE(m_client->userId(), kUserId);
    QVERIFY(m_client->hasSession());

    const auto request =
        m_mock->lastRequestFor(QStringLiteral("POST"), QStringLiteral("/Users/AuthenticateByName"));
    const QJsonObject body = QJsonDocument::fromJson(request.body).object();
    QCOMPARE(body.value(QLatin1String("Username")).toString(), QStringLiteral("mike"));
    QCOMPARE(body.value(QLatin1String("Pw")).toString(), QStringLiteral("pw"));
}

void EmbyClientTest::authenticatedCallsCarryToken()
{
    m_client->setSession(kToken, kUserId);
    QVERIFY(m_mock->addRouteFromFile(QStringLiteral("GET"),
                                     QStringLiteral("/Users/%1/Views").arg(kUserId),
                                     fixturePath(QStringLiteral("views.json"))));

    const auto result = waitFor(m_client->userViews());
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.value.size(), 3);
    QCOMPARE(result.value[0].name, QStringLiteral("Movies"));

    const auto request = m_mock->lastRequestFor(QStringLiteral("GET"),
                                                QStringLiteral("/Users/%1/Views").arg(kUserId));
    QCOMPARE(request.headers.value("x-emby-token"), kToken.toLatin1());
}

void EmbyClientTest::railsAndItems()
{
    m_client->setSession(kToken, kUserId);
    QVERIFY(m_mock->addRouteFromFile(QStringLiteral("GET"),
                                     QStringLiteral("/Users/%1/Items/Resume").arg(kUserId),
                                     fixturePath(QStringLiteral("resume.json"))));
    QVERIFY(m_mock->addRouteFromFile(QStringLiteral("GET"),
                                     QStringLiteral("/Users/%1/Items/Latest").arg(kUserId),
                                     fixturePath(QStringLiteral("latest.json"))));
    QVERIFY(m_mock->addRouteFromFile(QStringLiteral("GET"), QStringLiteral("/Shows/NextUp"),
                                     fixturePath(QStringLiteral("nextup.json"))));
    QVERIFY(m_mock->addRouteFromFile(QStringLiteral("GET"),
                                     QStringLiteral("/Users/%1/Items").arg(kUserId),
                                     fixturePath(QStringLiteral("items_movies.json"))));

    const auto resume = waitFor(m_client->resumeItems(5));
    QVERIFY2(resume.ok(), qPrintable(resume.error));
    QCOMPARE(resume.value.items.size(), 1);
    QVERIFY(resume.value.items[0].isResumable());

    const auto latest = waitFor(m_client->latestItems(QString(), 10));
    QVERIFY2(latest.ok(), qPrintable(latest.error));
    QCOMPARE(latest.value.size(), 2);

    const auto nextUp = waitFor(m_client->nextUp(5));
    QVERIFY2(nextUp.ok(), qPrintable(nextUp.error));
    QCOMPARE(nextUp.value.items[0].seriesName, QStringLiteral("Breaking Bad"));
    const auto nextUpRequest =
        m_mock->lastRequestFor(QStringLiteral("GET"), QStringLiteral("/Shows/NextUp"));
    QVERIFY(nextUpRequest.query.contains(QStringLiteral("UserId=") + kUserId));

    ItemsQuery query;
    query.parentId = QStringLiteral("f137a2dd21bbc1b99aa5c0f6bf02a805");
    query.sortBy = QStringLiteral("SortName");
    query.recursive = true;
    query.includeItemTypes = {QStringLiteral("Movie")};
    const auto page = waitFor(m_client->items(query));
    QVERIFY2(page.ok(), qPrintable(page.error));
    QCOMPARE(page.value.totalRecordCount, 42);
    const auto itemsRequest = m_mock->lastRequestFor(
        QStringLiteral("GET"), QStringLiteral("/Users/%1/Items").arg(kUserId));
    QVERIFY(itemsRequest.query.contains(QStringLiteral("Recursive=true")));
    QVERIFY(itemsRequest.query.contains(QStringLiteral("IncludeItemTypes=Movie")));
    QVERIFY(itemsRequest.query.contains(QStringLiteral("SortBy=SortName")));
}

void EmbyClientTest::mediaDetailFieldsRequested()
{
    // The version picker, media-info surface, and chapter navigation all need
    // fields Emby omits unless asked for by name (ARCHITECTURE.md).
    m_client->setSession(kToken, kUserId);
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Users/%1/Items").arg(kUserId), 200,
                     QByteArrayLiteral("{\"Items\":[],\"TotalRecordCount\":0}"));
    m_mock->addRoute(QStringLiteral("GET"),
                     QStringLiteral("/Users/%1/Items/301001").arg(kUserId), 200,
                     QByteArrayLiteral("{\"Id\":\"301001\"}"));

    ItemsQuery query;
    query.fields = {QStringLiteral("Overview")};
    QVERIFY(waitFor(m_client->items(query)).ok());
    const QString itemsFields =
        QUrlQuery(m_mock
                      ->lastRequestFor(QStringLiteral("GET"),
                                       QStringLiteral("/Users/%1/Items").arg(kUserId))
                      .query)
            .queryItemValue(QStringLiteral("Fields"));
    // Caller-requested fields survive; the media fields are merged in.
    QCOMPARE(itemsFields.split(QLatin1Char(',')),
             QStringList({QStringLiteral("Overview"), QStringLiteral("MediaSources"),
                          QStringLiteral("MediaStreams"), QStringLiteral("Chapters")}));

    QVERIFY(waitFor(m_client->itemDetails(QStringLiteral("301001"))).ok());
    const QString detailFields =
        QUrlQuery(m_mock
                      ->lastRequestFor(QStringLiteral("GET"),
                                       QStringLiteral("/Users/%1/Items/301001").arg(kUserId))
                      .query)
            .queryItemValue(QStringLiteral("Fields"));
    const QStringList detailList = detailFields.split(QLatin1Char(','));
    // The single-item endpoint keeps the media fields...
    for (const QString &field : {QStringLiteral("MediaSources"), QStringLiteral("MediaStreams"),
                                 QStringLiteral("Chapters")})
        QVERIFY2(detailList.contains(field), qPrintable(field));
    // ...and adds what only a details page needs.
    for (const QString &field :
         {QStringLiteral("ProviderIds"), QStringLiteral("ExternalUrls"), QStringLiteral("People"),
          QStringLiteral("GenreItems"), QStringLiteral("Studios")})
        QVERIFY2(detailList.contains(field), qPrintable(field));

    // The invariant that matters more than either list: the heavy fields must
    // NOT ride along on a browse page. People alone is ~60 objects per item, so
    // leaking it into a 100-item grid fetch multiplies that page by the same.
    const QStringList itemsList = itemsFields.split(QLatin1Char(','));
    for (const QString &field : {QStringLiteral("People"), QStringLiteral("ExternalUrls"),
                                 QStringLiteral("ProviderIds"), QStringLiteral("GenreItems"),
                                 QStringLiteral("Studios")})
        QVERIFY2(!itemsList.contains(field), qPrintable(field));
}

void EmbyClientTest::httpErrorSurfaces()
{
    m_client->setSession(kToken, kUserId);
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Users/%1/Views").arg(kUserId), 401,
                     QByteArrayLiteral("{}"));

    const auto result = waitFor(m_client->userViews());
    QVERIFY(!result.ok());
    QVERIFY2(result.error.contains(QStringLiteral("401")), qPrintable(result.error));
}

void EmbyClientTest::invalidJsonSurfaces()
{
    m_client->setSession(kToken, kUserId);
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Users/%1/Views").arg(kUserId), 200,
                     QByteArrayLiteral("this is not json"));

    const auto result = waitFor(m_client->userViews());
    QVERIFY(!result.ok());
    QVERIFY2(result.error.contains(QStringLiteral("JSON")), qPrintable(result.error));
}

void EmbyClientTest::unauthenticatedCallsFailFast()
{
    const auto result = waitFor(m_client->userViews());
    QVERIFY(!result.ok());
    QCOMPARE(result.error, QStringLiteral("not authenticated"));
    QVERIFY(m_mock->requests().isEmpty());
}

void EmbyClientTest::imageUrlBuilder()
{
    const QUrl url = m_client->imageUrl(QStringLiteral("301001"), QStringLiteral("Primary"), 400,
                                        QStringLiteral("aaa111"));
    QCOMPARE(url.path(), QStringLiteral("/Items/301001/Images/Primary"));
    QVERIFY(url.query().contains(QStringLiteral("maxWidth=400")));
    // Enhancers composite a television-set bezel and an Emby logo into episode
    // stills server-side, which breaks the aspect ratio the card is drawn at.
    QVERIFY(url.query().contains(QStringLiteral("EnableImageEnhancers=false")));
    QVERIFY(url.query().contains(QStringLiteral("tag=aaa111")));
}

// A music collection is not just streaming-era formats. Measured on the target
// library: flac 5,515 · mp3 51 · dsf 34 (codec dsd_lsbf_planar). `dsf` was
// absent from the DirectPlay profile, so the server answered DirectPlay=false
// and returned a transcode URL — a DSD file lossily re-encoded on its way to a
// player that decodes DSD natively.
void EmbyClientTest::deviceProfileCoversLosslessAudio()
{
    m_client->setSession(kToken, kUserId);
    m_mock->addRoute(QStringLiteral("POST"), QStringLiteral("/Items/501/PlaybackInfo"), 200,
                     QByteArrayLiteral("{\"MediaSources\":[],\"PlaySessionId\":\"x\"}"));
    waitFor(m_client->playbackInfo(QStringLiteral("501")));

    const QJsonObject body =
        QJsonDocument::fromJson(
            m_mock->lastRequestFor(QStringLiteral("POST"),
                                   QStringLiteral("/Items/501/PlaybackInfo"))
                .body)
            .object();
    const QJsonObject profile = body.value(QLatin1String("DeviceProfile")).toObject();

    QString audioContainers;
    for (const QJsonValue &entry : profile.value(QLatin1String("DirectPlayProfiles")).toArray()) {
        if (entry.toObject().value(QLatin1String("Type")).toString() == QLatin1String("Audio"))
            audioContainers = entry.toObject().value(QLatin1String("Container")).toString();
    }
    QVERIFY2(!audioContainers.isEmpty(), "no audio DirectPlay profile at all");
    for (const QString &container : {QStringLiteral("dsf"), QStringLiteral("dff"),
                                     QStringLiteral("flac"), QStringLiteral("ape"),
                                     QStringLiteral("wv"), QStringLiteral("alac")})
        QVERIFY2(audioContainers.contains(container), qPrintable(container));

    // And a defined fallback, so an undecodable container does not leave the
    // server to improvise one.
    bool hasAudioTranscode = false;
    for (const QJsonValue &entry : profile.value(QLatin1String("TranscodingProfiles")).toArray()) {
        if (entry.toObject().value(QLatin1String("Type")).toString() == QLatin1String("Audio"))
            hasAudioTranscode = true;
    }
    QVERIFY(hasAudioTranscode);
}

void EmbyClientTest::sessionChangeCancelsOutstandingRequests()
{
    m_client->setSession(kToken, kUserId);
    QVERIFY(m_mock->addRouteFromFile(QStringLiteral("GET"),
                                     QStringLiteral("/Users/%1/Views").arg(kUserId),
                                     fixturePath(QStringLiteral("views.json"))));
    m_mock->setRouteDelay(QStringLiteral("GET"),
                          QStringLiteral("/Users/%1/Views").arg(kUserId), 200);

    QFuture<Result<QList<Library>>> future = m_client->userViews();
    QTRY_COMPARE(m_mock->requestCount(), 1);
    m_client->setSession(QStringLiteral("new-token"), QStringLiteral("new-user"));

    const auto result = waitFor(std::move(future));
    QVERIFY(!result.ok());
    QCOMPARE(result.error, QStringLiteral("request canceled"));
}

// Cancellation belongs to a real identity change and to an explicit boundary
// (SessionController calls retireOutstandingRequests() for one). Re-asserting
// the address or session the client already has is neither, and must not take
// down work that belongs to the identity staying in place.
void EmbyClientTest::reassertingTheSameIdentityKeepsRequestsAlive()
{
    m_client->setSession(kToken, kUserId);
    QVERIFY(m_mock->addRouteFromFile(QStringLiteral("GET"),
                                     QStringLiteral("/Users/%1/Views").arg(kUserId),
                                     fixturePath(QStringLiteral("views.json"))));
    m_mock->setRouteDelay(QStringLiteral("GET"),
                          QStringLiteral("/Users/%1/Views").arg(kUserId), 200);

    QFuture<Result<QList<Library>>> future = m_client->userViews();
    QTRY_COMPARE(m_mock->requestCount(), 1);
    m_client->setBaseUrl(m_mock->baseUrl());
    m_client->setSession(kToken, kUserId);

    const auto result = waitFor(std::move(future));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QVERIFY(!result.value.isEmpty());

    // The explicit boundary still retires everything, identity unchanged.
    QFuture<Result<QList<Library>>> retired = m_client->userViews();
    QTRY_COMPARE(m_mock->requestCount(), 2);
    m_client->retireOutstandingRequests();
    const auto canceled = waitFor(std::move(retired));
    QVERIFY(!canceled.ok());
    QCOMPARE(canceled.error, QStringLiteral("request canceled"));
}

void EmbyClientTest::renameCannotChainAWriteAcrossServers()
{
    m_client->setSession(kToken, kUserId);
    const QString itemId = QStringLiteral("same-id-on-both-servers");
    m_mock->addRoute(QStringLiteral("GET"),
                     QStringLiteral("/Users/%1/Items/%2").arg(kUserId, itemId), 200,
                     QByteArrayLiteral("{\"Id\":\"same-id-on-both-servers\","
                                       "\"Name\":\"Old name\"}"));
    m_mock->setRouteDelay(QStringLiteral("GET"),
                          QStringLiteral("/Users/%1/Items/%2").arg(kUserId, itemId), 200);

    MockEmbyServer nextServer;
    QVERIFY(nextServer.start());
    nextServer.addRoute(QStringLiteral("POST"), QStringLiteral("/Items/%1").arg(itemId), 204,
                        {});

    QFuture<Result<bool>> future = m_client->renameItem(itemId, QStringLiteral("New name"));
    QTRY_COMPARE(m_mock->requestCount(), 1);
    m_client->setBaseUrl(nextServer.baseUrl());
    m_client->setSession(QStringLiteral("new-token"), QStringLiteral("new-user"));

    const auto result = waitFor(std::move(future));
    QVERIFY(!result.ok());
    QCOMPARE(result.error, QStringLiteral("request canceled"));
    QCOMPARE(nextServer.requestCount(), 0);
}

QTEST_GUILESS_MAIN(EmbyClientTest)
#include "tst_emby_client.moc"
