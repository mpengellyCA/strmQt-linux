#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>

#include <functional>

class QLocalServer;

namespace strmqt {

// HostLink — how a StrmLight's code typed into the desktop StrmQt reaches the
// streaming host's StrmQt, which is the one the StrmLight pairs with (its
// remote is on its own port; Settings::setStreamingHostInstance).
//
// A local socket in the user's runtime directory, open to this user only:
// whoever can reach it could as well read the settings the paired remotes are
// kept in. One line each way: "pair 1234" and "ok" or "no".
//
// `strmqt admin` asks the host for more (app/admin/Admin.h): a line naming
// the request and the length of what follows it, "youtube-music-import 2048",
// then that many bytes; the answer is one line, "ok" or "no" and a sentence.
class HostLink : public QObject
{
    Q_OBJECT

public:
    // What the host made of a request: whether it was done, and in words.
    struct Answer
    {
        bool ok = false;
        QString text;
    };
    using Handler = std::function<Answer(const QByteArray &body)>;
    // The most a request may carry: a YouTube sign-in is a few kilobytes.
    static constexpr qint64 kMaxBody = 1 << 20;

    explicit HostLink(QString socketPath = defaultSocketPath(), QObject *parent = nullptr);
    ~HostLink() override;

    static QString defaultSocketPath();

    // The host's side: `take` arms the code and says whether it did.
    bool listen(std::function<bool(const QString &code)> take);
    bool isListening() const;

    // The desktop's side: whether a host is there and took the code. Waits
    // for at most `timeoutMs`, so not for the host's own event loop: never
    // call it from the process that listens.
    bool pass(const QString &code, int timeoutMs = 1000) const;

    // The host's side: what `verb` does. A verb nobody handles is answered no.
    void handle(const QByteArray &verb, Handler handler);
    // The admin's side, with the same caution as pass(). No host is an answer
    // too: not ok, and says so.
    Answer ask(const QByteArray &verb, const QByteArray &body = {}, int timeoutMs = 5000) const;

private:
    QString m_path;
    QLocalServer *m_server = nullptr;
    std::function<bool(const QString &)> m_take;
    QHash<QByteArray, Handler> m_handlers;
};

} // namespace strmqt
