#include <QtTest>

#include "app/YouTubeLinks.h"

using namespace strmqt;

class YouTubeLinksTest : public QObject
{
    Q_OBJECT

private slots:
    void videoIdFromEveryLinkShape_data();
    void videoIdFromEveryLinkShape();
    void addresses();
};

void YouTubeLinksTest::videoIdFromEveryLinkShape_data()
{
    QTest::addColumn<QString>("link");
    QTest::addColumn<QString>("id");

    QTest::newRow("watch") << "https://www.youtube.com/watch?v=aqz-KE-bpKQ" << "aqz-KE-bpKQ";
    QTest::newRow("watch, more params")
        << "https://www.youtube.com/watch?feature=share&v=aqz-KE-bpKQ&t=10s" << "aqz-KE-bpKQ";
    QTest::newRow("http, no www") << "http://youtube.com/watch?v=aqz-KE-bpKQ" << "aqz-KE-bpKQ";
    QTest::newRow("mobile") << "https://m.youtube.com/watch?v=aqz-KE-bpKQ" << "aqz-KE-bpKQ";
    QTest::newRow("music") << "https://music.youtube.com/watch?v=aqz-KE-bpKQ" << "aqz-KE-bpKQ";
    QTest::newRow("short link") << "https://youtu.be/aqz-KE-bpKQ?t=5" << "aqz-KE-bpKQ";
    QTest::newRow("embed") << "https://www.youtube.com/embed/aqz-KE-bpKQ" << "aqz-KE-bpKQ";
    QTest::newRow("no-cookie embed")
        << "https://www.youtube-nocookie.com/embed/aqz-KE-bpKQ?rel=0" << "aqz-KE-bpKQ";
    QTest::newRow("shorts") << "https://www.youtube.com/shorts/aqz-KE-bpKQ" << "aqz-KE-bpKQ";
    QTest::newRow("upper-case host") << "https://WWW.YouTube.com/watch?v=aqz-KE-bpKQ"
                                     << "aqz-KE-bpKQ";

    // Not one video, or not YouTube: these stay links for the browser.
    QTest::newRow("channel") << "https://www.youtube.com/@blender" << "";
    QTest::newRow("playlist") << "https://www.youtube.com/playlist?list=PL123" << "";
    QTest::newRow("watch without v") << "https://www.youtube.com/watch" << "";
    QTest::newRow("id too short") << "https://youtu.be/abc" << "";
    QTest::newRow("id with markup") << "https://www.youtube.com/watch?v=aqz-KE-bp%3CQ" << "";
    QTest::newRow("look-alike host") << "https://youtube.com.example.org/watch?v=aqz-KE-bpKQ"
                                     << "";
    QTest::newRow("other site") << "https://vimeo.com/123456789" << "";
    QTest::newRow("not http") << "file:///watch?v=aqz-KE-bpKQ" << "";
    QTest::newRow("empty") << "" << "";
}

void YouTubeLinksTest::videoIdFromEveryLinkShape()
{
    QFETCH(QString, link);
    QFETCH(QString, id);
    QCOMPARE(youtube::videoId(QUrl(link)), id);
}

void YouTubeLinksTest::addresses()
{
    QCOMPARE(youtube::watchAddress(QStringLiteral("aqz-KE-bpKQ")).toString(),
             QStringLiteral("https://www.youtube.com/tv#/watch?v=aqz-KE-bpKQ"));
    // A query is data, never part of the address's structure.
    QCOMPARE(youtube::searchAddress(QStringLiteral("  AC/DC  Back in Black & more? ")).toString(QUrl::FullyEncoded),
             QStringLiteral("https://www.youtube.com/tv#/search?q=AC%2FDC%20Back%20in%20Black%20%26%20more%3F"));
    QVERIFY(youtube::searchAddress(QStringLiteral("   ")).isEmpty());
}

QTEST_GUILESS_MAIN(YouTubeLinksTest)
#include "tst_youtube_links.moc"
