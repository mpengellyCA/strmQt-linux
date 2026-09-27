// The key diagnostics (src/input/KeyEventLogger.h): off unless asked for, and
// when asked, one line per key the window receives, carrying what an unknown
// remote button can only be identified by — its native scan code and keysym.

#include "core/Log.h"
#include "input/KeyEventLogger.h"

#include <QKeyEvent>
#include <QLoggingCategory>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTest>

class TestKeyEventLogger : public QObject
{
    Q_OBJECT

private slots:
    void offByDefault()
    {
        QVERIFY(!logKeys().isDebugEnabled());
    }

    void describesTheNativeCodes()
    {
        // XF86OK: key 0, recognisable only by its keysym.
        const QKeyEvent ok(QEvent::KeyPress, 0, Qt::NoModifier, 360, 0x10081160, 0);
        const QString line = strmqt::KeyEventLogger::describe(&ok);
        QVERIFY2(line.startsWith(QLatin1String("press")), qPrintable(line));
        QVERIFY2(line.contains(QLatin1String("key=0x0 \"(no Qt key)\"")), qPrintable(line));
        QVERIFY2(line.contains(QLatin1String("scan=360")), qPrintable(line));
        QVERIFY2(line.contains(QLatin1String("keysym=0x10081160")), qPrintable(line));

        const QKeyEvent next(QEvent::KeyRelease, Qt::Key_MediaNext, Qt::KeypadModifier, 171,
                             0x1008ff17, 0, QString(), true);
        const QString released = strmqt::KeyEventLogger::describe(&next);
        QVERIFY2(released.startsWith(QLatin1String("release")), qPrintable(released));
        QVERIFY2(released.contains(QLatin1String("\"Media Next\"")), qPrintable(released));
        QVERIFY2(released.contains(QLatin1String("mods=0x20000000")), qPrintable(released));
        QVERIFY2(released.endsWith(QLatin1String(" repeat")), qPrintable(released));

        // Control characters stay on one line.
        const QKeyEvent ret(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
        QVERIFY(strmqt::KeyEventLogger::describe(&ret).contains(QLatin1String("text=\"\\u000d\"")));
    }

    void logsEveryPressAndReleaseAtTheWindow()
    {
        QLoggingCategory::setFilterRules(QStringLiteral("strmqt.input.keys.debug=true"));
        QVERIFY(logKeys().isDebugEnabled());

        QQuickWindow window;
        auto *item = new QQuickItem;
        item->setParentItem(window.contentItem());
        item->setFocus(true);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        strmqt::KeyEventLogger logger;
        qApp->installEventFilter(&logger);
        QTest::ignoreMessage(QtDebugMsg, QRegularExpression(QStringLiteral(
                                             "^press +key=0x1000090 \"Home Page\".*scan=180")));
        QTest::ignoreMessage(QtDebugMsg, QRegularExpression(QStringLiteral(
                                             "^release key=0x1000090 \"Home Page\".*scan=180")));
        for (QEvent::Type type : {QEvent::KeyPress, QEvent::KeyRelease}) {
            QKeyEvent event(type, Qt::Key_HomePage, Qt::NoModifier, 180, 0x1008ff18, 0);
            QCoreApplication::sendEvent(&window, &event);
        }
        qApp->removeEventFilter(&logger);
        QLoggingCategory::setFilterRules(QString());
    }
};

QTEST_MAIN(TestKeyEventLogger)
#include "tst_key_event_logger.moc"
