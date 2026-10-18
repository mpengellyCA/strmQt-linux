#include "SunshineApi.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslConfiguration>

namespace strmqt::streaming {

namespace {
constexpr int kTimeoutMs = 5'000;
}

SunshineApi::SunshineApi(QObject *parent) : QObject(parent), m_network(new QNetworkAccessManager(this)) {}

bool SunshineApi::configure(int webUiPort, const QString &certificatePath, const QString &user,
                            const QString &password)
{
    m_ready = false;
    m_port = webUiPort;
    m_authorization = "Basic " + (user + QLatin1Char(':') + password).toUtf8().toBase64();
    QFile file(certificatePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QList<QSslCertificate> found = QSslCertificate::fromData(file.readAll(), QSsl::Pem);
    if (found.isEmpty() || found.first().isNull())
        return false;
    m_certificate = found.first();
    m_ready = webUiPort > 0 && !password.isEmpty();
    return m_ready;
}

QNetworkRequest SunshineApi::request(const QString &path) const
{
    QNetworkRequest request(QUrl(QStringLiteral("https://127.0.0.1:%1%2").arg(m_port).arg(path)));
    // Sunshine's certificate is self-signed and names no host. It is trusted
    // by being the exact file this instance wrote, and nothing else is: that
    // one certificate is the only authority, and the name checked is the name
    // it carries. No certificate error is ignored.
    QSslConfiguration tls = QSslConfiguration::defaultConfiguration();
    tls.setCaCertificates({m_certificate});
    request.setSslConfiguration(tls);
    request.setPeerVerifyName(m_certificate.subjectInfo(QSslCertificate::CommonName).value(0));
    request.setRawHeader("Authorization", m_authorization);
    request.setTransferTimeout(kTimeoutMs);
    return request;
}

QNetworkReply *SunshineApi::send(const QByteArray &verb, const QString &path, const QByteArray &body)
{
    QNetworkRequest req = request(path);
    if (!body.isEmpty())
        req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    QNetworkReply *reply = m_network->sendCustomRequest(req, verb, body);
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    return reply;
}

QString SunshineApi::failure(QNetworkReply *reply)
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 401)
        return tr("Sunshine did not accept StrmQt's sign-in. Set the streaming host up again.");
    if (status == 0)
        return tr("Sunshine is not answering yet.");
    return {};
}

void SunshineApi::fetch()
{
    if (!m_ready)
        return;
    QNetworkReply *clients = send("GET", QStringLiteral("/api/clients/list"));
    connect(clients, &QNetworkReply::finished, this, [this, clients] {
        const QString failed = failure(clients);
        if (!failed.isEmpty()) {
            emit unreachable(failed);
            return;
        }
        emit clientsFetched(parseClients(clients->readAll()));
    });
    QNetworkReply *pairings = send("GET", QStringLiteral("/api/pin"));
    connect(pairings, &QNetworkReply::finished, this, [this, pairings] {
        if (!failure(pairings).isEmpty())
            return; // the clients request reports it
        bool listed = false;
        const QList<Pairing> found = parsePairings(pairings->readAll(), &listed);
        emit pairingsFetched(found, listed);
    });
}

void SunshineApi::pair(const QString &pairingId, const QString &pin, const QString &name)
{
    if (!m_ready) {
        emit paired(tr("Sunshine is not answering yet."));
        return;
    }
    QJsonObject body{{QStringLiteral("pin"), pin}, {QStringLiteral("name"), name}};
    if (!pairingId.isEmpty())
        body.insert(QStringLiteral("pairing_id"), pairingId);
    QNetworkReply *reply = send("POST", QStringLiteral("/api/pin"), QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const QString failed = failure(reply);
        emit paired(failed.isEmpty() ? errorIn(reply->readAll()) : failed);
    });
}

void SunshineApi::unpair(const QString &uuid)
{
    if (!m_ready) {
        emit unpaired(uuid, tr("Sunshine is not answering yet."));
        return;
    }
    const QJsonObject body{{QStringLiteral("uuid"), uuid}};
    QNetworkReply *reply = send("POST", QStringLiteral("/api/clients/unpair"), QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, uuid] {
        const QString failed = failure(reply);
        emit unpaired(uuid, failed.isEmpty() ? errorIn(reply->readAll()) : failed);
    });
}

QList<SunshineApi::Client> SunshineApi::parseClients(const QByteArray &json)
{
    QList<Client> clients;
    const QJsonArray named = QJsonDocument::fromJson(json).object().value(QLatin1String("named_certs")).toArray();
    for (const QJsonValue &value : named) {
        const QJsonObject entry = value.toObject();
        const QString uuid = entry.value(QLatin1String("uuid")).toString();
        if (uuid.isEmpty())
            continue;
        QString name = entry.value(QLatin1String("name")).toString();
        if (name.isEmpty())
            name = tr("Unnamed device");
        clients.append({name, uuid});
    }
    return clients;
}

QList<SunshineApi::Pairing> SunshineApi::parsePairings(const QByteArray &json, bool *listed)
{
    QList<Pairing> pairings;
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    const QJsonValue list = root.value(QLatin1String("pairings"));
    if (listed)
        *listed = list.isArray();
    for (const QJsonValue &value : list.toArray()) {
        const QJsonObject entry = value.toObject();
        const QString id = entry.value(QLatin1String("id")).toString();
        if (id.isEmpty())
            continue;
        QString name;
        for (const char *key : {"name", "client_name", "device_name", "address", "ip"}) {
            name = entry.value(QLatin1String(key)).toString();
            if (!name.isEmpty())
                break;
        }
        pairings.append({id, name});
    }
    return pairings;
}

QString SunshineApi::errorIn(const QByteArray &json)
{
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    if (root.value(QLatin1String("status")).toBool())
        return {};
    const QString error = root.value(QLatin1String("error")).toString();
    return error.isEmpty() ? tr("Sunshine refused.") : error;
}

} // namespace strmqt::streaming
