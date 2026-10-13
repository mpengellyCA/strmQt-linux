#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QSslCertificate>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;

namespace strmqt::streaming {

// The streaming host's Sunshine, over the HTTP API its own web page uses. Only
// ever the instance StrmQt set up, only ever on this machine: the address is
// 127.0.0.1 and the one certificate trusted is the file that instance wrote.
class SunshineApi : public QObject
{
    Q_OBJECT

public:
    struct Client
    {
        QString name;
        QString uuid;
    };
    // A Moonlight that is showing a PIN and waiting for it to be typed here.
    struct Pairing
    {
        QString id;
        QString name;
    };

    explicit SunshineApi(QObject *parent = nullptr);

    // False when the certificate cannot be read: Sunshine has not run yet.
    bool configure(int webUiPort, const QString &certificatePath, const QString &user,
                   const QString &password);
    bool ready() const { return m_ready; }

    // Asks for both lists; each arrives by its own signal.
    void fetch();
    // `pairingId` is empty for a Sunshine too old to list who is asking.
    void pair(const QString &pairingId, const QString &pin, const QString &name);
    void unpair(const QString &uuid);

    // Tolerant of drift: unknown fields ignored, missing ones defaulted.
    static QList<Client> parseClients(const QByteArray &json);
    // `listed` is false when this Sunshine does not list requests at all.
    static QList<Pairing> parsePairings(const QByteArray &json, bool *listed);
    // {"status": true} is the only success; anything else is the error text.
    static QString errorIn(const QByteArray &json);

signals:
    void clientsFetched(const QList<strmqt::streaming::SunshineApi::Client> &clients);
    void pairingsFetched(const QList<strmqt::streaming::SunshineApi::Pairing> &pairings, bool listed);
    void paired(const QString &error);
    void unpaired(const QString &error);
    // The API did not answer: stopped, still starting, or the wrong password.
    void unreachable(const QString &reason);

private:
    QNetworkRequest request(const QString &path) const;
    QNetworkReply *send(const QByteArray &verb, const QString &path, const QByteArray &body = {});
    static QString failure(QNetworkReply *reply);

    QNetworkAccessManager *m_network;
    QSslCertificate m_certificate;
    QByteArray m_authorization;
    int m_port = 0;
    bool m_ready = false;
};

} // namespace strmqt::streaming
