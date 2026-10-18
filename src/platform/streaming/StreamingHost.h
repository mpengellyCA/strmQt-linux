#pragma once

#include "HostFiles.h"
#include "SunshineApi.h"

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

class QProcess;

namespace strmqt {

class SecretsStore;

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
    // The headless screen's scale in percent, as KWin's display settings set
    // it. Applied at once to a running host; `scaleAvailable` is false without
    // kscreen-doctor, which applies it.
    Q_PROPERTY(int scale READ scale WRITE setScale NOTIFY configChanged)
    Q_PROPERTY(bool scaleAvailable READ scaleAvailable CONSTANT)
    // What a StrmLight asks Sunshine for, read before each stream: frame rate,
    // bitrate in kbit/s (0: by size and frame rate, `automaticBitrate`), codec
    // ("auto", "hevc", "av1", "h264") and sound ("2", "51", "71").
    Q_PROPERTY(int streamFps READ streamFps WRITE setStreamFps NOTIFY configChanged)
    Q_PROPERTY(int streamBitrate READ streamBitrate WRITE setStreamBitrate NOTIFY configChanged)
    Q_PROPERTY(int automaticBitrate READ automaticBitrate NOTIFY configChanged)
    Q_PROPERTY(QString streamCodec READ streamCodec WRITE setStreamCodec NOTIFY configChanged)
    Q_PROPERTY(QString streamAudio READ streamAudio WRITE setStreamAudio NOTIFY configChanged)
    // Pairing, through the host Sunshine's own API. Each entry of `devices`
    // is {name, uuid}, a StrmLight: nothing else may stay paired.
    Q_PROPERTY(QVariantList devices READ devices NOTIFY pairingChanged)
    Q_PROPERTY(bool pairingReachable READ pairingReachable NOTIFY pairingChanged)
    Q_PROPERTY(QString pairingMessage READ pairingMessage NOTIFY pairingChanged)
    Q_PROPERTY(bool pairingFailed READ pairingFailed NOTIFY pairingChanged)

public:
    // Where things go and what is run. The defaults are the real ones; tests
    // name scratch directories, their own unit names and a harmless command.
    struct Environment
    {
        QString unitDir;
        QString stateDir;
        QString hostUnit = QLatin1String(streaming::kHostUnit);
        QString sunshineUnit = QLatin1String(streaming::kSunshineUnit);
        QString audioUnit = QLatin1String(streaming::kAudioUnit);
        QString socket = QStringLiteral("strmqt-host");
        QString strmqt;
        QString kwin;
        QString sunshine;
        QString pipewire;
        QString wireplumber;
        QString systemctl;
        // Optional: without it the screen's scale cannot be changed.
        QString kscreenDoctor;
        // The desktop's XDG_CONFIG_HOME, for the StrmQt on the screen.
        QString configHome;
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
    int scale() const { return m_config.scalePercent; }
    void setScale(int percent);
    bool scaleAvailable() const { return !m_env.kscreenDoctor.isEmpty(); }
    int streamFps() const { return m_config.fps; }
    void setStreamFps(int fps);
    int streamBitrate() const { return m_config.bitrateKbps; }
    void setStreamBitrate(int kbps);
    int automaticBitrate() const;
    QString streamCodec() const { return m_config.codec; }
    void setStreamCodec(const QString &codec);
    QString streamAudio() const { return m_config.audio; }
    void setStreamAudio(const QString &audio);
    QString webUiAddress() const;
    QVariantList devices() const { return m_devices; }
    bool pairingReachable() const { return m_pairingReachable; }
    QString pairingMessage() const { return m_pairingMessage; }
    bool pairingFailed() const { return m_pairingFailed; }

    // Nothing is looked at until this is called: opening Settings must not
    // start processes.
    Q_INVOKABLE void refresh();
    // The services' states alone; cheap enough to ask on a timer while shown.
    Q_INVOKABLE void checkState() { readState(); }
    Q_INVOKABLE void setUp();
    Q_INVOKABLE void restart();
    Q_INVOKABLE void remove();
    // Asks Sunshine for the paired devices and for who is asking to pair.
    Q_INVOKABLE void fetchPairing();
    // The same, for the streaming host's own StrmQt, which has no Settings
    // page open to ask: it refuses what is not a StrmLight from there.
    void checkPairing();
    Q_INVOKABLE void unpair(const QString &uuid);
    // A StrmLight pairs its stream and its remote with one code: the code
    // typed for its remote is also its Moonlight PIN. This enters `pin` into
    // Sunshine as soon as a Moonlight is asking, trying every 2 s for
    // `seconds`, and quietly: a try with nobody asking is not a failure.
    // This is the only way anything is paired with the host's Sunshine.
    Q_INVOKABLE void enterPinWhenAsked(const QString &pin, int seconds);
    // The host Sunshine's certificate as lowercase hex SHA-256 of its DER
    // form, which StrmLight compares with the one it paired with; empty
    // before Sunshine has written it.
    QString sunshineFingerprint() const;
    // What GET /api/strmlight/host gives a paired StrmLight: the fingerprint
    // and, under "stream", the settings above. Read from the host's settings
    // file each time: the desktop StrmQt changes them, not this instance.
    QJsonObject strmLightHost() const;
    // Sets the running screen's scale to the one chosen (kscreen-doctor).
    // The streaming host's StrmQt does it once it is up, and the desktop's
    // whenever it changes.
    void applyScale();
    // Whom a PIN goes to now. False: nobody is asking yet, so wait. Otherwise
    // `requestId` is the request to answer, empty for a Sunshine that does
    // not list them (it then gives the PIN to whoever is waiting).
    static bool pinTarget(bool requestsListed, const QVariantList &requests, QString *requestId);
    // Whether `devices` has one not in `before` (uuids). Every Moonlight asks
    // as the same client, so Sunshine can give a PIN to an abandoned request
    // instead of the StrmLight's: only a new device in the list is success.
    static bool hasNewDevice(const QStringList &before, const QVariantList &devices);
    // The name StrmQt gives each pairing it makes (enterPinWhenAsked). Only
    // StrmQt can name one: Sunshine takes the name from whoever enters the
    // PIN through its API, and only StrmQt has the API's sign-in.
    static constexpr char kStrmLightDevice[] = "StrmLight";
    // The uuids of `devices` that StrmQt did not pair for a StrmLight: a plain
    // Moonlight, paired some other way (Settings had a PIN pad for one in
    // 0.9.0). They are unpaired as soon as they are seen.
    static QStringList strangers(const QVariantList &devices);

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
    void pairingChanged();
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
        // Carries a secret: its arguments are never logged or shown.
        bool quiet = false;
    };

    void probe();
    void readState();
    void loadConfig();
    static void readConfig(const QString &stateDir, streaming::HostConfig *config);
    void saveConfig() const;
    bool writeFiles();
    void applyIfInstalled();
    void run(QList<Step> steps, const QString &done);
    void next();
    void finish(bool ok, const QString &text);
    void setMessage(const QString &text, bool failed);
    QString unitPath(const QString &unit) const;
    void connectApi();
    void configureApi();
    void setPairingMessage(const QString &text, bool failed);
    // 0.9.0 put Sunshine on base 48989, where Linux lends ports to outgoing
    // connections; a set-up or restart moves it to StrmQt's block.
    void leaveLegacyPort();
    // Adds the command that reserves the ports when Linux may lend them.
    void notePortsLent();
    void tryPendingPin();

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
    // The host Sunshine's sign-in, which only StrmQt uses. Kept in a vault
    // file beside the host's other state rather than in the desktop keyring:
    // the StrmQt that needs it is the headless one, which starts at boot with
    // no keyring unlocked, and the folder already holds what the password
    // guards (Sunshine's pairings and private key).
    SecretsStore *m_vault = nullptr;
    QString m_password;
    streaming::SunshineApi *m_api = nullptr;
    QVariantList m_devices;
    QVariantList m_requests;
    bool m_requestsListed = true;
    bool m_pairingReachable = false;
    QString m_pairingMessage;
    bool m_pairingFailed = false;
    // enterPinWhenAsked(): empty when there is nothing to enter.
    QString m_pendingPin;
    qint64 m_pendingPinUntil = 0;
    bool m_pinInFlight = false;
    QTimer *m_pinTimer = nullptr;
    // The paired devices when the PIN was given; unknown until first listed.
    QStringList m_pinBaseline;
    bool m_pinBaselineKnown = false;
    // Strangers being unpaired, so that each is asked once.
    QStringList m_refusing;
};

} // namespace strmqt
