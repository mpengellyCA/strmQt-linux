#include <QSignalSpy>
#include <QtTest>

#include "MockEmbyServer.h"
#include "app/controllers/SearchController.h"
#include "server/emby/EmbyClient.h"

using namespace strmqt;

namespace {
const auto kUserId = QStringLiteral("a1b2c3d4e5f60718293a4b5c6d7e8f90");
const auto kToken = QStringLiteral("not-a-real-token-fixture-only");

QByteArray itemPage(const QString &id)
{
    return QStringLiteral("{\"Items\":[{\"Id\":\"%1\",\"Name\":\"%1\","
                          "\"Type\":\"Movie\"}],\"TotalRecordCount\":1}")
        .arg(id)
        .toUtf8();
}
} // namespace

class SearchCoherenceTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void delayedReplacementClearsRetainedRowsAndPublishesCommittedSuccess();
    void failureAndEmptyCancellationPublishEmptyTerminalModels();
    void supersededQueryNeverPublishesAnIntermediateTerminal();
    void accountResetPublishesAnEmptyTerminalModel();

private:
    void setItemsReply(int status, const QByteArray &body, int delayMs = 0);
    int itemRequestCount() const;

    MockEmbyServer *m_mock = nullptr;
    emby::EmbyClient *m_client = nullptr;
    QString m_itemsPath;
};

void SearchCoherenceTest::init()
{
    m_mock = new MockEmbyServer(this);
    QVERIFY(m_mock->start());
    m_client = new emby::EmbyClient(this);
    m_client->setBaseUrl(m_mock->baseUrl());
    m_client->setSession(kToken, kUserId);
    m_itemsPath = QStringLiteral("/Users/%1/Items").arg(kUserId);
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Persons"), 200,
                     QByteArrayLiteral("{\"Items\":[],\"TotalRecordCount\":0}"));
    m_mock->addRoute(QStringLiteral("GET"), QStringLiteral("/Genres"), 200,
                     QByteArrayLiteral("{\"Items\":[],\"TotalRecordCount\":0}"));
}

void SearchCoherenceTest::cleanup()
{
    delete m_client;
    m_client = nullptr;
    delete m_mock;
    m_mock = nullptr;
}

void SearchCoherenceTest::setItemsReply(int status, const QByteArray &body, int delayMs)
{
    m_mock->addRoute(QStringLiteral("GET"), m_itemsPath, status, body);
    m_mock->setRouteDelay(QStringLiteral("GET"), m_itemsPath, delayMs);
}

int SearchCoherenceTest::itemRequestCount() const
{
    int count = 0;
    for (const MockEmbyServer::ReceivedRequest &request : m_mock->requests()) {
        if (request.path == m_itemsPath)
            ++count;
    }
    return count;
}

void SearchCoherenceTest::delayedReplacementClearsRetainedRowsAndPublishesCommittedSuccess()
{
    setItemsReply(200, itemPage(QStringLiteral("old")));
    SearchController search(m_client);
    search.setQuery(QStringLiteral("old"));
    QTRY_COMPARE_WITH_TIMEOUT(search.model()->rowCount(), 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!search.searching(), 5000);

    QString terminalName;
    connect(&search, &SearchController::searchingChanged, &search, [&] {
        if (!search.searching() && search.model()->rowCount() > 0)
            terminalName = search.model()->get(0).value(QStringLiteral("name")).toString();
    });

    setItemsReply(200, itemPage(QStringLiteral("new")), 250);
    search.setQuery(QStringLiteral("new"));

    // This is still inside the debounce: another query's row is already gone
    // and the lifecycle is active before any replacement request exists.
    QVERIFY(search.searching());
    QCOMPARE(search.model()->rowCount(), 0);
    QTest::qWait(100);
    QCOMPARE(itemRequestCount(), 1);
    QCOMPARE(search.model()->rowCount(), 0);
    QVERIFY(search.searching());

    QTRY_COMPARE_WITH_TIMEOUT(itemRequestCount(), 2, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!search.searching(), 5000);
    QCOMPARE(search.model()->rowCount(), 1);
    QCOMPARE(terminalName, QStringLiteral("new"));
}

void SearchCoherenceTest::failureAndEmptyCancellationPublishEmptyTerminalModels()
{
    SearchController search(m_client);
    QList<int> terminalCounts;
    connect(&search, &SearchController::searchingChanged, &search, [&] {
        if (!search.searching())
            terminalCounts.append(search.model()->rowCount());
    });

    setItemsReply(500, QByteArrayLiteral("{}"));
    search.setQuery(QStringLiteral("failure"));
    QVERIFY(search.searching());
    QCOMPARE(search.model()->rowCount(), 0);
    QTRY_VERIFY_WITH_TIMEOUT(!search.searching(), 5000);
    QCOMPARE(terminalCounts, QList<int>{0});

    setItemsReply(200, itemPage(QStringLiteral("cancelled")), 1000);
    search.setQuery(QStringLiteral("cancelled"));
    QVERIFY(search.searching());
    QTRY_COMPARE_WITH_TIMEOUT(itemRequestCount(), 2, 5000);
    search.setQuery(QString());

    QVERIFY(!search.searching());
    QVERIFY(search.query().isEmpty());
    QCOMPARE(search.model()->rowCount(), 0);
    QCOMPARE(terminalCounts, QList<int>({0, 0}));
    QTest::qWait(1100);
    QCOMPARE(search.model()->rowCount(), 0);
}

void SearchCoherenceTest::supersededQueryNeverPublishesAnIntermediateTerminal()
{
    setItemsReply(200, itemPage(QStringLiteral("superseded")), 1000);
    SearchController search(m_client);
    search.setQuery(QStringLiteral("superseded"));
    QVERIFY(search.searching());
    QTRY_COMPARE_WITH_TIMEOUT(itemRequestCount(), 1, 5000);

    QSignalSpy lifecycle(&search, &SearchController::searchingChanged);
    QString terminalName;
    connect(&search, &SearchController::searchingChanged, &search, [&] {
        if (!search.searching() && search.model()->rowCount() > 0)
            terminalName = search.model()->get(0).value(QStringLiteral("name")).toString();
    });

    setItemsReply(200, itemPage(QStringLiteral("current")));
    search.setQuery(QStringLiteral("current"));
    QVERIFY(search.searching());
    QCOMPARE(search.model()->rowCount(), 0);
    QCOMPARE(lifecycle.count(), 0);

    QTRY_COMPARE_WITH_TIMEOUT(itemRequestCount(), 2, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!search.searching(), 5000);
    QCOMPARE(lifecycle.count(), 1);
    QCOMPARE(terminalName, QStringLiteral("current"));
    QTest::qWait(1100);
    QCOMPARE(search.model()->get(0).value(QStringLiteral("name")).toString(),
             QStringLiteral("current"));
}

void SearchCoherenceTest::accountResetPublishesAnEmptyTerminalModel()
{
    setItemsReply(200, itemPage(QStringLiteral("account-a")), 1000);
    SearchController search(m_client);
    search.setQuery(QStringLiteral("account a"));
    QTRY_COMPARE_WITH_TIMEOUT(itemRequestCount(), 1, 5000);
    QVERIFY(search.searching());

    int terminalCount = -1;
    connect(&search, &SearchController::searchingChanged, &search, [&] {
        if (!search.searching())
            terminalCount = search.model()->rowCount();
    });
    search.resetSessionState();

    QVERIFY(!search.searching());
    QVERIFY(search.query().isEmpty());
    QCOMPARE(search.model()->rowCount(), 0);
    QCOMPARE(terminalCount, 0);
    QTest::qWait(1100);
    QCOMPARE(search.model()->rowCount(), 0);
}

QTEST_GUILESS_MAIN(SearchCoherenceTest)
#include "tst_search_coherence.moc"
