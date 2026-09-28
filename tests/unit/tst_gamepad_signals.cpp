#include <QtTest>

#include "input/GamepadManager.h"
#include "input/InputMap.h"

#include <csignal>

using namespace strmqt;

namespace {

bool isDefault(int signal)
{
    struct sigaction action = {};
    sigaction(signal, nullptr, &action);
    return (action.sa_flags & SA_SIGINFO) == 0 && action.sa_handler == SIG_DFL;
}

} // namespace

// SDL_INIT_GAMEPAD brings SDL's event subsystem with it, and that, unless told
// otherwise, catches SIGINT and SIGTERM and turns them into an SDL quit event
// nobody reads: the app ignored kill, Ctrl+C and a session logout.
class GamepadSignalsTest : public QObject
{
    Q_OBJECT

private slots:
    void terminationSignalsStayWithTheProcess();
};

void GamepadSignalsTest::terminationSignalsStayWithTheProcess()
{
    // SDL only replaces a handler that is SIG_DFL, and QTest may have put its
    // own there; start from the state a plain process starts in.
    std::signal(SIGINT, SIG_DFL);
    std::signal(SIGTERM, SIG_DFL);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    InputMap input(dir.filePath(QStringLiteral("input.ini")));
    GamepadManager pads(&input);
    if (!pads.available())
        QSKIP("SDL's gamepad subsystem does not initialise on this machine");

    QVERIFY(isDefault(SIGTERM));
    QVERIFY(isDefault(SIGINT));
}

QTEST_MAIN(GamepadSignalsTest)
#include "tst_gamepad_signals.moc"
