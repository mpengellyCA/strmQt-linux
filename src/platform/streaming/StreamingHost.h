#pragma once

#include "HostFiles.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;

namespace strmqt {

// Settings' "Streaming host": sets up, watches and removes the two systemd
// user services described in HostFiles.h. Everything is asynchronous and
// everything degrades: a machine without systemd, KWin or a usable Sunshine
// gets a list of what is missing, never a failed command.
//
// StrmQt never installs or changes Sunshine itself. A Sunshine that cannot do
// the job is reported, with what to do about it.
class StreamingHost : public QObject
{
    Q_OBJECT
    // False when this machine cannot host at all; `problems` says why.
    Q_PROPERTY(bool available READ available NOTIFY probed)
    Q_PROPERTY(QStringList problems READ problems NOTIFY probed)
    Q_PROPERTY(bool installed READ installed NOTIFY stateChanged)
    // systemd's words: "active", "inactive", "failed", "activating", ...
    Q_PROPERTY(QString screenState READ screenState NOTIFY stateChanged)
    Q_PROPERTY(QString sunshineState READ sunshineState NOTIFY stateChanged)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // The last thing that happened, in a sentence; `failed` says which kind.
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool failed READ failed NOTIFY messageChanged)
    Q_PROPERTY(int height READ height WRITE setHeight NOTIFY configChanged)
    Q_PROPERTY(bool webUiOnNetwork READ webUiOnNetwork WRITE setWebUiOnNetwork NOTIFY configChanged)
    Q_PROPERTY(int port READ port NOTIFY configChanged)
    Q_PROPERTY(QString webUiAddress READ webUiAddress NOTIFY configChanged)

public:
    // Where things go and what is run. The defaults are the real ones; tests
    // name scratch directories, their own unit names and a harmless command.
    struct Environment
    {
        QString unitDir;
        QString stateDir;
        QString hostUnit = QLatin1String(streaming::kHostUnit);
        QString sunshineUnit = QLatin1String(streaming::kSunshineUnit);
        QString socket = QStringLiteral("strmqt-host");
        QString strmqt;
        QString kwin;
        QString sunshine;
        QString systemctl;
        // `loginctl enable-linger`: start at boot, before anyone logs in.
        bool linger = true;
        bool insideFlatpak = false;
        QStringList strmqtEnvironment;

        static Environment detect();
    };

    explicit StreamingHost(QObject *parent = nullptr);
    explicit StreamingHost(Environment environment, QObject *parent = nullptr);
    ~StreamingHost() override;

    bool available() const { return m_probed && m_problems.isEmpty(); }
    QStringList problems() const { return m_problems; }
    bool installed() const { return m_installed; }
    QString screenState() const { return m_screenState; }
    QString sunshineState() const { return m_sunshineState; }
    bool running() const;
    bool busy() const { return m_busy; }
    QString message() const { return m_message; }
    bool failed() const { return m_failed; }
    int height() const { return m_config.height; }
    void setHeight(int height);
    bool webUiOnNetwork() const { return m_config.webUiOnNetwork; }
    void setWebUiOnNetwork(bool on);
    int port() const { return m_config.port; }
    QString webUiAddress() const;

    // Nothing is looked at until this is called: opening Settings must not
    // start processes.
    Q_INVOKABLE void refresh();
    // The services' states alone; cheap enough to ask on a timer while shown.
    Q_INVOKABLE void checkState() { readState(); }
    Q_INVOKABLE void setUp();
    Q_INVOKABLE void restart();
    Q_INVOKABLE void remove();

    // What this machine lacks for hosting, given what was found on it.
    static QStringList probeProblems(const Environment &environment);
    // Whether the Sunshine at `path` was built with KWin's screencast capture.
    static bool sunshineCapturesKWin(const QString &path);

signals:
    void probed();
    void stateChanged();
    void busyChanged();
    void messageChanged();
    void configChanged();
    // Every queued command has finished, whichever way.
    void settled();

private:
    struct Step
    {
        QString program;
        QStringList arguments;
        // A step that may fail without the whole thing having failed, and the
        // sentence to say when it does.
        QString toleratedAs;
    };

    void probe();
    void readState();
    void loadConfig();
    void saveConfig() const;
    bool writeFiles();
    void applyIfInstalled();
    void run(QList<Step> steps, const QString &done);
    void next();
    void finish(bool ok, const QString &text);
    void setMessage(const QString &text, bool failed);
    QString unitPath(const QString &unit) const;

    Environment m_env;
    streaming::HostConfig m_config;
    QStringList m_problems;
    bool m_probed = false;
    bool m_installed = false;
    QString m_screenState;
    QString m_sunshineState;
    bool m_busy = false;
    QString m_message;
    bool m_failed = false;
    QList<Step> m_steps;
    QString m_done;
    QStringList m_notes;
    QProcess *m_process = nullptr;
    QProcess *m_stateProcess = nullptr;
};

} // namespace strmqt
