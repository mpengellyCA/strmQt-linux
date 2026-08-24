#include "EmbyClient.h"

#include <memory>

#include "EmbyDtoMapper.h"
#include "core/Log.h"

#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace strmqt::emby {

namespace {

constexpr int kTransferTimeoutMs = 15'000;
const auto kClientName = QLatin1String("StrmQt");

// Fields needed for one item's version picker, media-info/track surfaces, and
// chapter navigation. List DTOs deliberately do not retain any of these heavy
// arrays, so only itemDetails() asks the server for them.
QStringList itemMediaFields()
{
    return {QStringLiteral("MediaSources"), QStringLiteral("MediaStreams"),
            QStringLiteral("Chapters")};
}

// Extra fields worth requesting for ONE item and never for a page of them:
// People alone is ~60 objects per movie, so folding these into a list fetch
// would multiply every 100-item grid response by that.
QStringList itemDetailFields()
{
    return {QStringLiteral("ProviderIds"),   QStringLiteral("ExternalUrls"),
            QStringLiteral("Studios"),       QStringLiteral("GenreItems"),
            QStringLiteral("People"),        QStringLiteral("Taglines"),
            QStringLiteral("PremiereDate"),  QStringLiteral("OfficialRating"),
            QStringLiteral("CommunityRating"), QStringLiteral("CriticRating"),
            QStringLiteral("RemoteTrailers"), QStringLiteral("ProductionLocations"),
            QStringLiteral("EndDate")};
}

// Merge extra field names into a caller-supplied list without duplicating.
QString mergedFields(const QStringList &requested, const QStringList &extra)
{
    QStringList fields = requested;
    for (const QString &name : extra) {
        if (!fields.contains(name, Qt::CaseInsensitive))
            fields.append(name);
    }
    return fields.join(QLatin1Char(','));
}

QString versionString()
{
#ifdef STRMQT_VERSION
    return QStringLiteral(STRMQT_VERSION);
#else
    return QStringLiteral("0.0.0");
#endif
}

} // namespace

EmbyClient::EmbyClient(QObject *parent) : QObject(parent), m_nam(new QNetworkAccessManager(this))
{
    m_nam->setAutoDeleteReplies(true);
}

void EmbyClient::setBaseUrl(const QUrl &url)
{
    // Only a real change is a request boundary. Re-asserting the address the
    // client already has (restore(), a repeated login attempt) must not cancel
    // work that belongs to the identity staying in place.
    if (url == m_baseUrl)
        return;
    retireOutstandingRequests();
    m_baseUrl = url;
    emit identityChanged();
}

void EmbyClient::setSession(const QString &accessToken, const QString &userId)
{
    if (accessToken == m_accessToken && userId == m_userId)
        return;
    retireOutstandingRequests();
    m_accessToken = accessToken;
    m_userId = userId;
    emit identityChanged();
}

EmbyClient::RequestContext EmbyClient::requestContext() const
{
    return {m_baseUrl, m_deviceId, m_deviceName, m_accessToken, m_userId, m_requestEpoch};
}

void EmbyClient::retireOutstandingRequests()
{
    ++m_requestEpoch;
    const QList<QNetworkReply *> replies = m_nam->findChildren<QNetworkReply *>();
    for (QNetworkReply *reply : replies) {
        if (reply && !reply->isFinished())
            reply->abort();
    }
}

QUrl EmbyClient::requestUrl(const QString &path, const QUrlQuery &query) const
{
    return requestUrl(path, query, requestContext());
}

QUrl EmbyClient::requestUrl(const QString &path, const QUrlQuery &query,
                            const RequestContext &context) const
{
    QUrl url = context.baseUrl;
    // Preserve any reverse-proxy base path on the server URL.
    QString fullPath = url.path();
    if (fullPath.endsWith(QLatin1Char('/')))
        fullPath.chop(1);
    url.setPath(fullPath + path);
    if (!query.isEmpty())
        url.setQuery(query);
    return url;
}

QNetworkRequest EmbyClient::baseRequest(const QUrl &url) const
{
    return baseRequest(url, requestContext());
}

QNetworkRequest EmbyClient::baseRequest(const QUrl &url, const RequestContext &context) const
{
    QNetworkRequest request(url);
    request.setTransferTimeout(kTransferTimeoutMs);
    request.setRawHeader("Accept", "application/json");
    const QString authorization =
        QStringLiteral("MediaBrowser Client=\"%1\", Device=\"%2\", DeviceId=\"%3\", "
                       "Version=\"%4\"")
            .arg(kClientName,
                 context.deviceName.isEmpty() ? QStringLiteral("linux") : context.deviceName,
                 context.deviceId, versionString());
    request.setRawHeader("X-Emby-Authorization", authorization.toUtf8());
    if (!context.accessToken.isEmpty())
        request.setRawHeader("X-Emby-Token", context.accessToken.toUtf8());
    return request;
}

QNetworkReply *EmbyClient::startGet(const QString &path, const QUrlQuery &query)
{
    return startGet(path, query, requestContext());
}

QNetworkReply *EmbyClient::startGet(const QString &path, const QUrlQuery &query,
                                    const RequestContext &context)
{
    return m_nam->get(baseRequest(requestUrl(path, query, context), context));
}

QNetworkReply *EmbyClient::startPost(const QString &path, const QJsonObject &body)
{
    return startPost(path, body, requestContext());
}

QNetworkReply *EmbyClient::startPost(const QString &path, const QJsonObject &body,
                                     const RequestContext &context)
{
    QNetworkRequest request = baseRequest(requestUrl(path, {}, context), context);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    return m_nam->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
}

QNetworkReply *EmbyClient::startPostQuery(const QString &path, const QUrlQuery &query)
{
    QNetworkRequest request = baseRequest(requestUrl(path, query));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    return m_nam->post(request, QByteArrayLiteral("{}"));
}

QNetworkReply *EmbyClient::startDelete(const QString &path)
{
    return m_nam->deleteResource(baseRequest(requestUrl(path, {})));
}

QFuture<Result<bool>> EmbyClient::finishStatus(QNetworkReply *reply)
{
    auto promise = std::make_shared<QPromise<Result<bool>>>();
    QFuture<Result<bool>> future = promise->future();
    promise->start();
    const quint64 epoch = m_requestEpoch;
    connect(reply, &QNetworkReply::finished, this, [this, reply, promise, epoch] {
        if (epoch != m_requestEpoch)
            promise->addResult(Result<bool>::failure(QStringLiteral("request canceled")));
        else if (reply->error() != QNetworkReply::NoError)
            promise->addResult(Result<bool>::failure(reply->errorString()));
        else
            promise->addResult(Result<bool>::success(true));
        promise->finish();
    });
    return future;
}

template<class T> QFuture<Result<T>> EmbyClient::failedFuture(const QString &error)
{
    QPromise<Result<T>> promise;
    QFuture<Result<T>> future = promise.future();
    promise.start();
    promise.addResult(Result<T>::failure(error));
    promise.finish();
    return future;
}

template<class T>
QFuture<Result<T>> EmbyClient::finishJson(QNetworkReply *reply,
                                          std::function<Result<T>(const QJsonDocument &)> parse)
{
    auto promise = std::make_shared<QPromise<Result<T>>>();
    QFuture<Result<T>> future = promise->future();
    promise->start();

    const quint64 epoch = m_requestEpoch;
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, promise, parse = std::move(parse), epoch] {
        if (epoch != m_requestEpoch) {
            promise->addResult(Result<T>::failure(QStringLiteral("request canceled")));
            promise->finish();
            return;
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            const QString message =
                status > 0 ? QStringLiteral("HTTP %1: %2").arg(status).arg(reply->errorString())
                           : reply->errorString();
            qCWarning(logServer) << "request failed:" << reply->url().path() << message;
            promise->addResult(Result<T>::failure(message));
            promise->finish();
            return;
        }

        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            promise->addResult(Result<T>::failure(
                QStringLiteral("invalid JSON: %1").arg(parseError.errorString())));
            promise->finish();
            return;
        }

        promise->addResult(parse(doc));
        promise->finish();
    });

    return future;
}

QFuture<Result<ServerInfo>> EmbyClient::publicSystemInfo()
{
    QNetworkReply *reply = startGet(QStringLiteral("/System/Info/Public"), {});
    return finishJson<ServerInfo>(reply, [](const QJsonDocument &doc) {
        const QJsonObject json = doc.object();
        ServerInfo info;
        info.id = json.value(QLatin1String("Id")).toString();
        info.name = json.value(QLatin1String("ServerName")).toString();
        info.version = json.value(QLatin1String("Version")).toString();
        return Result<ServerInfo>::success(info);
    });
}

QFuture<Result<SessionInfo>> EmbyClient::authenticateByName(const QString &username,
                                                            const QString &password)
{
    QJsonObject body;
    body.insert(QLatin1String("Username"), username);
    body.insert(QLatin1String("Pw"), password);

    QNetworkReply *reply = startPost(QStringLiteral("/Users/AuthenticateByName"), body);
    return finishJson<SessionInfo>(reply, [this](const QJsonDocument &doc) {
        const SessionInfo session = parseAuthResult(doc.object());
        if (!session.isValid())
            return Result<SessionInfo>::failure(
                QStringLiteral("authentication response missing token or user id"));
        m_accessToken = session.accessToken;
        m_userId = session.user.id;
        emit identityChanged();
        qCInfo(logServer) << "authenticated as" << session.user.name;
        return Result<SessionInfo>::success(session);
    });
}

QFuture<Result<QList<MediaItem>>> EmbyClient::publicUsers()
{
    if (m_baseUrl.isEmpty())
        return failedFuture<QList<MediaItem>>(QStringLiteral("no server"));
    // Deliberately not gated on hasSession(): this is what a login screen asks
    // for before anyone has signed in.
    QNetworkReply *reply = startGet(QStringLiteral("/Users/Public"), {});
    return finishJson<QList<MediaItem>>(reply, [](const QJsonDocument &doc) {
        // A bare array on the wire, not an ItemsPage.
        return Result<QList<MediaItem>>::success(parseItemArray(doc.array()));
    });
}

QFuture<Result<QList<Library>>> EmbyClient::userViews()
{
    if (!hasSession())
        return failedFuture<QList<Library>>(QStringLiteral("not authenticated"));

    QNetworkReply *reply = startGet(QStringLiteral("/Users/%1/Views").arg(m_userId), {});
    return finishJson<QList<Library>>(reply, [](const QJsonDocument &doc) {
        return Result<QList<Library>>::success(parseViews(doc.object()));
    });
}

QFuture<Result<ItemsPage>> EmbyClient::items(const ItemsQuery &query)
{
    if (!hasSession())
        return failedFuture<ItemsPage>(QStringLiteral("not authenticated"));

    QUrlQuery params;
    if (!query.parentId.isEmpty())
        params.addQueryItem(QStringLiteral("ParentId"), query.parentId);
    if (!query.searchTerm.isEmpty())
        params.addQueryItem(QStringLiteral("SearchTerm"), query.searchTerm);
    if (!query.sortBy.isEmpty()) {
        params.addQueryItem(QStringLiteral("SortBy"), query.sortBy);
        params.addQueryItem(QStringLiteral("SortOrder"), query.sortDescending
                                                             ? QStringLiteral("Descending")
                                                             : QStringLiteral("Ascending"));
    }
    if (!query.includeItemTypes.isEmpty())
        params.addQueryItem(QStringLiteral("IncludeItemTypes"),
                            query.includeItemTypes.join(QLatin1Char(',')));
    if (!query.fields.isEmpty())
        params.addQueryItem(QStringLiteral("Fields"), query.fields.join(QLatin1Char(',')));
    if (!query.filters.isEmpty())
        params.addQueryItem(QStringLiteral("Filters"), query.filters.join(QLatin1Char(',')));
    if (!query.genreIds.isEmpty())
        params.addQueryItem(QStringLiteral("GenreIds"), query.genreIds.join(QLatin1Char(',')));
    if (!query.personIds.isEmpty())
        params.addQueryItem(QStringLiteral("PersonIds"), query.personIds.join(QLatin1Char(',')));
    if (!query.studioIds.isEmpty())
        params.addQueryItem(QStringLiteral("StudioIds"), query.studioIds.join(QLatin1Char(',')));
    if (!query.artistIds.isEmpty())
        params.addQueryItem(QStringLiteral("ArtistIds"), query.artistIds.join(QLatin1Char(',')));
    if (!query.albumArtistIds.isEmpty()) {
        params.addQueryItem(QStringLiteral("AlbumArtistIds"),
                            query.albumArtistIds.join(QLatin1Char(',')));
    }
    if (!query.yearFilters.isEmpty())
        params.addQueryItem(QStringLiteral("Years"), query.yearFilters.join(QLatin1Char(',')));
    if (!query.nameStartsWith.isEmpty())
        params.addQueryItem(QStringLiteral("NameStartsWith"), query.nameStartsWith);
    if (!query.listItemIds.isEmpty())
        params.addQueryItem(QStringLiteral("ListItemIds"),
                            query.listItemIds.join(QLatin1Char(',')));
    if (query.recursive)
        params.addQueryItem(QStringLiteral("Recursive"), QStringLiteral("true"));
    params.addQueryItem(QStringLiteral("StartIndex"), QString::number(query.startIndex));
    params.addQueryItem(QStringLiteral("Limit"), QString::number(query.limit));

    QNetworkReply *reply = startGet(QStringLiteral("/Users/%1/Items").arg(m_userId), params);
    return finishJson<ItemsPage>(reply, [](const QJsonDocument &doc) {
        return Result<ItemsPage>::success(parseItemsPage(doc.object()));
    });
}

QFuture<Result<ItemsPage>> EmbyClient::resumeItems(int limit)
{
    if (!hasSession())
        return failedFuture<ItemsPage>(QStringLiteral("not authenticated"));

    QUrlQuery params;
    params.addQueryItem(QStringLiteral("Limit"), QString::number(limit));
    params.addQueryItem(QStringLiteral("MediaTypes"), QStringLiteral("Video"));
    // PremiereDate is the episode's air date, which a card wants and which the
    // server omits unless asked. A role with nothing behind it is worse than no
    // role at all.
    params.addQueryItem(QStringLiteral("Fields"), QStringLiteral("Overview,PremiereDate"));

    QNetworkReply *reply = startGet(QStringLiteral("/Users/%1/Items/Resume").arg(m_userId), params);
    return finishJson<ItemsPage>(reply, [](const QJsonDocument &doc) {
        return Result<ItemsPage>::success(parseItemsPage(doc.object()));
    });
}

QFuture<Result<QList<MediaItem>>> EmbyClient::latestItems(const QString &parentId, int limit)
{
    if (!hasSession())
        return failedFuture<QList<MediaItem>>(QStringLiteral("not authenticated"));

    QUrlQuery params;
    params.addQueryItem(QStringLiteral("Limit"), QString::number(limit));
    params.addQueryItem(QStringLiteral("Fields"), QStringLiteral("Overview"));
    if (!parentId.isEmpty())
        params.addQueryItem(QStringLiteral("ParentId"), parentId);

    QNetworkReply *reply = startGet(QStringLiteral("/Users/%1/Items/Latest").arg(m_userId), params);
    return finishJson<QList<MediaItem>>(reply, [](const QJsonDocument &doc) {
        return Result<QList<MediaItem>>::success(parseItemArray(doc.array()));
    });
}

QFuture<Result<ItemsPage>> EmbyClient::nextUp(int limit)
{
    if (!hasSession())
        return failedFuture<ItemsPage>(QStringLiteral("not authenticated"));

    QUrlQuery params;
    params.addQueryItem(QStringLiteral("UserId"), m_userId);
    params.addQueryItem(QStringLiteral("Limit"), QString::number(limit));
    params.addQueryItem(QStringLiteral("Fields"), QStringLiteral("Overview"));

    QNetworkReply *reply = startGet(QStringLiteral("/Shows/NextUp"), params);
    return finishJson<ItemsPage>(reply, [](const QJsonDocument &doc) {
        return Result<ItemsPage>::success(parseItemsPage(doc.object()));
    });
}

QFuture<Result<ItemsPage>> EmbyClient::seasons(const QString &seriesId)
{
    if (!hasSession())
        return failedFuture<ItemsPage>(QStringLiteral("not authenticated"));
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("UserId"), m_userId);
    QNetworkReply *reply = startGet(QStringLiteral("/Shows/%1/Seasons").arg(seriesId), params);
    return finishJson<ItemsPage>(reply, [](const QJsonDocument &doc) {
        return Result<ItemsPage>::success(parseItemsPage(doc.object()));
    });
}

QFuture<Result<ItemsPage>> EmbyClient::episodes(const QString &seriesId, const QString &seasonId)
{
    if (!hasSession())
        return failedFuture<ItemsPage>(QStringLiteral("not authenticated"));
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("UserId"), m_userId);
    if (!seasonId.isEmpty())
        params.addQueryItem(QStringLiteral("SeasonId"), seasonId);
    params.addQueryItem(QStringLiteral("Fields"), QStringLiteral("Overview"));
    QNetworkReply *reply = startGet(QStringLiteral("/Shows/%1/Episodes").arg(seriesId), params);
    return finishJson<ItemsPage>(reply, [](const QJsonDocument &doc) {
        return Result<ItemsPage>::success(parseItemsPage(doc.object()));
    });
}

QFuture<Result<QList<MediaItem>>> EmbyClient::nextEpisode(const QString &seriesId,
                                                          const QString &episodeId)
{
    if (!hasSession() || seriesId.isEmpty() || episodeId.isEmpty())
        return failedFuture<QList<MediaItem>>(QStringLiteral("no series context"));
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("UserId"), m_userId);
    params.addQueryItem(QStringLiteral("StartItemId"), episodeId);
    // Two rows: the current episode and the one after it.
    params.addQueryItem(QStringLiteral("Limit"), QStringLiteral("2"));
    QNetworkReply *reply = startGet(QStringLiteral("/Shows/%1/Episodes").arg(seriesId), params);
    return finishJson<QList<MediaItem>>(reply, [](const QJsonDocument &doc) {
        const QList<MediaItem> items =
            parseItemArray(doc.object().value(QLatin1String("Items")).toArray());
        // Drop the current episode; what is left is the next one, or nothing at
        // the end of a series.
        return Result<QList<MediaItem>>::success(items.mid(1));
    });
}

QFuture<Result<ItemDetails>> EmbyClient::itemDetails(const QString &itemId)
{
    if (!hasSession())
        return failedFuture<ItemDetails>(QStringLiteral("not authenticated"));
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("Fields"),
                        mergedFields(itemDetailFields(), itemMediaFields()));
    QNetworkReply *reply =
        startGet(QStringLiteral("/Users/%1/Items/%2").arg(m_userId, itemId), params);
    return finishJson<ItemDetails>(reply, [](const QJsonDocument &doc) {
        return Result<ItemDetails>::success(parseItemDetails(doc.object()));
    });
}

QFuture<Result<QList<MediaItem>>> EmbyClient::similar(const QString &itemId, int limit)
{
    if (!hasSession())
        return failedFuture<QList<MediaItem>>(QStringLiteral("not authenticated"));
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("UserId"), m_userId);
    params.addQueryItem(QStringLiteral("Limit"), QString::number(limit));
    QNetworkReply *reply = startGet(QStringLiteral("/Items/%1/Similar").arg(itemId), params);
    return finishJson<QList<MediaItem>>(reply, [](const QJsonDocument &doc) {
        return Result<QList<MediaItem>>::success(
            parseItemArray(doc.object().value(QLatin1String("Items")).toArray()));
    });
}

QFuture<Result<ItemsPage>> EmbyClient::instantMix(const QString &itemId, int limit)
{
    if (!hasSession())
        return failedFuture<ItemsPage>(QStringLiteral("not authenticated"));
    if (itemId.isEmpty())
        return failedFuture<ItemsPage>(QStringLiteral("no item to mix from"));
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("UserId"), m_userId);
    params.addQueryItem(QStringLiteral("Limit"), QString::number(limit));
    // No StartIndex: the endpoint does not page (see the header).
    QNetworkReply *reply = startGet(QStringLiteral("/Items/%1/InstantMix").arg(itemId), params);
    return finishJson<ItemsPage>(reply, [](const QJsonDocument &doc) {
        return Result<ItemsPage>::success(parseItemsPage(doc.object()));
    });
}

namespace {
// The subset of ItemsQuery the artist endpoints were measured to honour. Sending
// the rest would not fail — this server ignores what it does not know — which is
// exactly why the set is stated here rather than assumed.
QUrlQuery artistParams(const QString &userId, const ItemsQuery &query)
{
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("UserId"), userId);
    if (!query.parentId.isEmpty())
        params.addQueryItem(QStringLiteral("ParentId"), query.parentId);
    params.addQueryItem(QStringLiteral("StartIndex"), QString::number(query.startIndex));
    params.addQueryItem(QStringLiteral("Limit"), QString::number(query.limit));
    params.addQueryItem(QStringLiteral("SortBy"), query.sortBy.isEmpty()
                                                      ? QStringLiteral("SortName")
                                                      : query.sortBy);
    params.addQueryItem(QStringLiteral("SortOrder"), query.sortDescending
                                                         ? QStringLiteral("Descending")
                                                         : QStringLiteral("Ascending"));
    if (!query.nameStartsWith.isEmpty())
        params.addQueryItem(QStringLiteral("NameStartsWith"), query.nameStartsWith);
    if (!query.genreIds.isEmpty())
        params.addQueryItem(QStringLiteral("GenreIds"), query.genreIds.join(QLatin1Char(',')));
    // Sent, but unmeasured — see the header. Every other parameter in this
    // function changed the answer on the live server; this one could not be
    // told apart from being ignored without writing a favourite into the user's
    // library, so it is documented as unknown rather than claimed as honoured.
    if (!query.filters.isEmpty())
        params.addQueryItem(QStringLiteral("Filters"), query.filters.join(QLatin1Char(',')));
    return params;
}
} // namespace

QFuture<Result<ItemsPage>> EmbyClient::musicArtists(const ItemsQuery &query)
{
    if (!hasSession())
        return failedFuture<ItemsPage>(QStringLiteral("not authenticated"));
    QNetworkReply *reply = startGet(QStringLiteral("/Artists"), artistParams(m_userId, query));
    return finishJson<ItemsPage>(reply, [](const QJsonDocument &doc) {
        return Result<ItemsPage>::success(parseItemsPage(doc.object()));
    });
}

QFuture<Result<ItemsPage>> EmbyClient::albumArtists(const ItemsQuery &query)
{
    if (!hasSession())
        return failedFuture<ItemsPage>(QStringLiteral("not authenticated"));
    QNetworkReply *reply =
        startGet(QStringLiteral("/Artists/AlbumArtists"), artistParams(m_userId, query));
    return finishJson<ItemsPage>(reply, [](const QJsonDocument &doc) {
        return Result<ItemsPage>::success(parseItemsPage(doc.object()));
    });
}

QFuture<Result<QList<MediaItem>>> EmbyClient::persons(const QString &searchTerm, int limit)
{
    if (!hasSession())
        return failedFuture<QList<MediaItem>>(QStringLiteral("not authenticated"));
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("UserId"), m_userId);
    params.addQueryItem(QStringLiteral("Limit"), QString::number(limit));
    if (!searchTerm.isEmpty())
        params.addQueryItem(QStringLiteral("SearchTerm"), searchTerm);
    QNetworkReply *reply = startGet(QStringLiteral("/Persons"), params);
    return finishJson<QList<MediaItem>>(reply, [](const QJsonDocument &doc) {
        return Result<QList<MediaItem>>::success(
            parseItemArray(doc.object().value(QLatin1String("Items")).toArray()));
    });
}

QFuture<Result<QList<MediaItem>>> EmbyClient::genres(const QString &searchTerm, int limit)
{
    if (!hasSession())
        return failedFuture<QList<MediaItem>>(QStringLiteral("not authenticated"));
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("UserId"), m_userId);
    params.addQueryItem(QStringLiteral("Limit"), QString::number(limit));
    if (!searchTerm.isEmpty())
        params.addQueryItem(QStringLiteral("SearchTerm"), searchTerm);
    QNetworkReply *reply = startGet(QStringLiteral("/Genres"), params);
    return finishJson<QList<MediaItem>>(reply, [](const QJsonDocument &doc) {
        return Result<QList<MediaItem>>::success(
            parseItemArray(doc.object().value(QLatin1String("Items")).toArray()));
    });
}

QFuture<Result<ItemsPage>> EmbyClient::musicGenres(const QString &parentId, int startIndex,
                                                   int limit)
{
    if (!hasSession())
        return failedFuture<ItemsPage>(QStringLiteral("not authenticated"));
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("UserId"), m_userId);
    // Honoured, and worth stating because the sibling endpoints are not:
    // measured on 4.9.5.0, ParentId=<music library> answered 289 genres while
    // the film library, the TV library, the collections library and a made-up
    // id each answered 0. Omitted when empty, which means "every library" — and
    // that is the same 289 here, because only a music library has music genres.
    if (!parentId.isEmpty())
        params.addQueryItem(QStringLiteral("ParentId"), parentId);
    params.addQueryItem(QStringLiteral("StartIndex"), QString::number(startIndex));
    params.addQueryItem(QStringLiteral("Limit"), QString::number(limit));
    params.addQueryItem(QStringLiteral("SortBy"), QStringLiteral("SortName"));
    QNetworkReply *reply = startGet(QStringLiteral("/MusicGenres"), params);
    return finishJson<ItemsPage>(reply, [](const QJsonDocument &doc) {
        return Result<ItemsPage>::success(parseItemsPage(doc.object()));
    });
}

QFuture<Result<bool>> EmbyClient::setPlayed(const QString &itemId, bool played)
{
    if (!hasSession())
        return failedFuture<bool>(QStringLiteral("not authenticated"));
    const QString path = QStringLiteral("/Users/%1/PlayedItems/%2").arg(m_userId, itemId);
    return finishStatus(played ? startPost(path, {}) : startDelete(path));
}

QFuture<Result<bool>> EmbyClient::setFavorite(const QString &itemId, bool favorite)
{
    if (!hasSession())
        return failedFuture<bool>(QStringLiteral("not authenticated"));
    const QString path = QStringLiteral("/Users/%1/FavoriteItems/%2").arg(m_userId, itemId);
    return finishStatus(favorite ? startPost(path, {}) : startDelete(path));
}

QFuture<Result<bool>> EmbyClient::refreshMetadata(const QString &itemId, bool recursive,
                                                  const QString &metadataRefreshMode,
                                                  const QString &imageRefreshMode)
{
    if (!hasSession())
        return failedFuture<bool>(QStringLiteral("not authenticated"));
    if (itemId.isEmpty())
        return failedFuture<bool>(QStringLiteral("no item id"));

    QUrlQuery params;
    params.addQueryItem(QStringLiteral("Recursive"),
                        recursive ? QStringLiteral("true") : QStringLiteral("false"));
    params.addQueryItem(QStringLiteral("MetadataRefreshMode"), metadataRefreshMode);
    params.addQueryItem(QStringLiteral("ImageRefreshMode"), imageRefreshMode);
    return finishStatus(startPostQuery(QStringLiteral("/Items/%1/Refresh").arg(itemId), params));
}

namespace {

// What we declare we can play natively. mpv/ffmpeg decode nearly everything, so
// direct play is broad; the transcode profile is the HLS fallback rung.
// `mode` narrows what the profile advertises:
//   "directPlay" strips the transcoding section, so the server has nothing to
//               fall back to and either direct-plays or refuses;
//   "transcode"  strips direct play, forcing the server to re-encode.
// Both are diagnostic escape hatches; "auto" is the profile as designed.
QJsonObject deviceProfileJson(int maxBitrateKbps = 0,
                              const QString &mode = QStringLiteral("auto"))
{
    QJsonObject videoDirect;
    videoDirect.insert(QLatin1String("Container"),
                       QLatin1String("mp4,mkv,webm,avi,mov,ts,m2ts,flv,ogv,wtv,3gp"));
    videoDirect.insert(QLatin1String("Type"), QLatin1String("Video"));

    // Every container mpv/ffmpeg decodes, not just the streaming-era ones.
    //
    // This matters more than it looks. Measured against the target library
    // (5,600 tracks sampled): flac 5,515 · mp3 51 · **dsf 34**, the last with
    // codec dsd_lsbf_planar. `dsf` was absent from the old list, so the server
    // answered DirectPlay=false and handed back a transcode URL — a DSD file
    // lossily re-encoded on the way to a player that decodes DSD natively.
    // Verified after the change: the same track returns DirectPlay=true.
    //
    // A container the server cannot direct-play still has the audio
    // TranscodingProfile below to fall back to, which is a defined outcome
    // rather than the server improvising one.
    QJsonObject audioDirect;
    audioDirect.insert(QLatin1String("Container"),
                       QLatin1String("mp3,flac,ogg,oga,aac,m4a,m4b,wav,wv,opus,wma,aiff,aif,"
                                     "ape,alac,dsf,dff,mka,mpc,tta,tak,ac3,dts,au,caf"));
    audioDirect.insert(QLatin1String("Type"), QLatin1String("Audio"));

    // Audio fallback. Without one, an undecodable container left the server to
    // improvise, which it does by re-encoding to a lossy stream. FLAC keeps the
    // fallback lossless where the source was.
    QJsonObject audioTranscoding;
    audioTranscoding.insert(QLatin1String("Container"), QLatin1String("flac"));
    audioTranscoding.insert(QLatin1String("Type"), QLatin1String("Audio"));
    audioTranscoding.insert(QLatin1String("AudioCodec"), QLatin1String("flac"));
    audioTranscoding.insert(QLatin1String("Protocol"), QLatin1String("http"));

    QJsonObject transcoding;
    transcoding.insert(QLatin1String("Container"), QLatin1String("ts"));
    transcoding.insert(QLatin1String("Type"), QLatin1String("Video"));
    transcoding.insert(QLatin1String("VideoCodec"), QLatin1String("h264,hevc"));
    transcoding.insert(QLatin1String("AudioCodec"), QLatin1String("aac,ac3"));
    transcoding.insert(QLatin1String("Protocol"), QLatin1String("hls"));

    QJsonArray subtitles;
    for (const char *format : {"srt", "subrip", "ass", "ssa", "pgssub", "sub", "vtt"}) {
        QJsonObject profile;
        profile.insert(QLatin1String("Format"), QLatin1String(format));
        profile.insert(QLatin1String("Method"), QLatin1String("Embed"));
        subtitles.append(profile);
    }

    QJsonObject deviceProfile;
    deviceProfile.insert(QLatin1String("MaxStreamingBitrate"),
                         maxBitrateKbps > 0 ? maxBitrateKbps * 1000 : 120'000'000);
    if (mode != QLatin1String("transcode")) {
        deviceProfile.insert(QLatin1String("DirectPlayProfiles"),
                             QJsonArray{videoDirect, audioDirect});
    }
    if (mode != QLatin1String("directPlay")) {
        deviceProfile.insert(QLatin1String("TranscodingProfiles"),
                             QJsonArray{transcoding, audioTranscoding});
    }
    // A capped stream that the server may only remux is a contradiction: the
    // bitrate ceiling can only be met by re-encoding, so the transcoding
    // profile has to carry the same ceiling or the server ignores it.
    if (maxBitrateKbps > 0 && mode != QLatin1String("directPlay")) {
        transcoding.insert(QLatin1String("MaxStreamingBitrate"), maxBitrateKbps * 1000);
        deviceProfile.insert(QLatin1String("TranscodingProfiles"),
                             QJsonArray{transcoding, audioTranscoding});
    }
    deviceProfile.insert(QLatin1String("SubtitleProfiles"), subtitles);
    return deviceProfile;
}

QJsonObject progressBody(const PlaybackProgress &progress)
{
    QJsonObject body;
    body.insert(QLatin1String("ItemId"), progress.itemId);
    body.insert(QLatin1String("MediaSourceId"), progress.mediaSourceId);
    body.insert(QLatin1String("PlaySessionId"), progress.playSessionId);
    body.insert(QLatin1String("PlayMethod"), playMethodName(progress.method));
    body.insert(QLatin1String("PositionTicks"), static_cast<double>(progress.positionTicks));
    body.insert(QLatin1String("IsPaused"), progress.paused);
    body.insert(QLatin1String("CanSeek"), true);
    return body;
}

} // namespace

void EmbyClient::setQualityPreferences(int maxBitrateKbps, const QString &mode)
{
    m_maxBitrateKbps = qMax(0, maxBitrateKbps);
    m_playbackMode = mode.isEmpty() ? QStringLiteral("auto") : mode;
}

QFuture<Result<PlaybackTicket>> EmbyClient::playbackInfo(const QString &itemId,
                                                         qint64 startPositionTicks)
{
    if (!hasSession())
        return failedFuture<PlaybackTicket>(QStringLiteral("not authenticated"));

    QJsonObject body;
    body.insert(QLatin1String("UserId"), m_userId);
    body.insert(QLatin1String("IsPlayback"), true);
    body.insert(QLatin1String("AutoOpenLiveStream"), true);
    if (startPositionTicks > 0)
        body.insert(QLatin1String("StartTimeTicks"), static_cast<double>(startPositionTicks));
    body.insert(QLatin1String("DeviceProfile"),
                deviceProfileJson(m_maxBitrateKbps, m_playbackMode));
    // Emby honours the ceiling on the request as well as inside the profile,
    // and the two disagreeing is how a cap silently does nothing.
    if (m_maxBitrateKbps > 0)
        body.insert(QLatin1String("MaxStreamingBitrate"), m_maxBitrateKbps * 1000);

    const RequestContext context = requestContext();
    QNetworkReply *reply =
        startPost(QStringLiteral("/Items/%1/PlaybackInfo").arg(itemId), body, context);
    return finishJson<PlaybackTicket>(reply, [context, itemId](const QJsonDocument &doc) {
        const PlaybackTicket ticket = parsePlaybackTicket(doc.object(), context.baseUrl, itemId,
                                                          context.accessToken, context.deviceId);
        if (!ticket.isValid())
            return Result<PlaybackTicket>::failure(
                QStringLiteral("no playable media sources for item %1").arg(itemId));
        return Result<PlaybackTicket>::success(ticket);
    });
}

QFuture<Result<ItemsPage>> EmbyClient::playlistItems(const QString &playlistId, int startIndex,
                                                    int limit)
{
    if (!hasSession())
        return failedFuture<ItemsPage>(QStringLiteral("not authenticated"));
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("UserId"), m_userId);
    params.addQueryItem(QStringLiteral("StartIndex"), QString::number(startIndex));
    params.addQueryItem(QStringLiteral("Limit"), QString::number(limit));
    QNetworkReply *reply = startGet(QStringLiteral("/Playlists/%1/Items").arg(playlistId), params);
    return finishJson<ItemsPage>(reply, [](const QJsonDocument &doc) {
        return Result<ItemsPage>::success(parseItemsPage(doc.object()));
    });
}

QFuture<Result<QString>> EmbyClient::createPlaylist(const QString &name,
                                                    const QStringList &itemIds,
                                                    const QString &mediaType)
{
    if (!hasSession())
        return failedFuture<QString>(QStringLiteral("not authenticated"));
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("Name"), name);
    params.addQueryItem(QStringLiteral("UserId"), m_userId);
    if (!itemIds.isEmpty())
        params.addQueryItem(QStringLiteral("Ids"), itemIds.join(QLatin1Char(',')));
    if (!mediaType.isEmpty())
        params.addQueryItem(QStringLiteral("MediaType"), mediaType);
    QNetworkReply *reply = startPostQuery(QStringLiteral("/Playlists"), params);
    return finishJson<QString>(reply, [](const QJsonDocument &doc) {
        const QString id = doc.object().value(QLatin1String("Id")).toVariant().toString();
        if (id.isEmpty())
            return Result<QString>::failure(QStringLiteral("server returned no playlist id"));
        return Result<QString>::success(id);
    });
}

QFuture<Result<bool>> EmbyClient::addToPlaylist(const QString &playlistId,
                                                const QStringList &itemIds)
{
    if (!hasSession())
        return failedFuture<bool>(QStringLiteral("not authenticated"));
    if (playlistId.isEmpty() || itemIds.isEmpty())
        return failedFuture<bool>(QStringLiteral("nothing to add"));
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("UserId"), m_userId);
    params.addQueryItem(QStringLiteral("Ids"), itemIds.join(QLatin1Char(',')));
    return finishStatus(
        startPostQuery(QStringLiteral("/Playlists/%1/Items").arg(playlistId), params));
}

QFuture<Result<bool>> EmbyClient::removeFromPlaylist(const QString &playlistId,
                                                     const QStringList &entryIds)
{
    if (!hasSession())
        return failedFuture<bool>(QStringLiteral("not authenticated"));
    if (playlistId.isEmpty() || entryIds.isEmpty())
        return failedFuture<bool>(QStringLiteral("nothing to remove"));
    QUrlQuery params;
    // EntryIds, not item ids: the same item can appear twice.
    params.addQueryItem(QStringLiteral("EntryIds"), entryIds.join(QLatin1Char(',')));
    QUrl url = requestUrl(QStringLiteral("/Playlists/%1/Items").arg(playlistId), params);
    QNetworkReply *reply = m_nam->deleteResource(baseRequest(url));
    return finishStatus(reply);
}

QFuture<Result<bool>> EmbyClient::movePlaylistItem(const QString &playlistId,
                                                   const QString &entryId, int newIndex)
{
    if (!hasSession())
        return failedFuture<bool>(QStringLiteral("not authenticated"));
    if (playlistId.isEmpty() || entryId.isEmpty() || newIndex < 0)
        return failedFuture<bool>(QStringLiteral("invalid move"));
    return finishStatus(startPostQuery(QStringLiteral("/Playlists/%1/Items/%2/Move/%3")
                                           .arg(playlistId, entryId, QString::number(newIndex)),
                                       {}));
}

QFuture<Result<bool>> EmbyClient::renameItem(const QString &itemId, const QString &name)
{
    if (!hasSession() || itemId.isEmpty() || name.trimmed().isEmpty())
        return failedFuture<bool>(QStringLiteral("nothing to rename"));

    auto promise = std::make_shared<QPromise<Result<bool>>>();
    QFuture<Result<bool>> future = promise->future();
    promise->start();
    const RequestContext context = requestContext();

    // Read-modify-write, because UpdateItem replaces the object.
    QUrlQuery params;
    QNetworkReply *reply = startGet(
        QStringLiteral("/Users/%1/Items/%2").arg(context.userId, itemId), params, context);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, itemId, name, promise, context] {
        reply->deleteLater();
        if (context.epoch != m_requestEpoch) {
            promise->addResult(Result<bool>::failure(QStringLiteral("request canceled")));
            promise->finish();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            promise->addResult(Result<bool>::failure(reply->errorString()));
            promise->finish();
            return;
        }
        QJsonObject item = QJsonDocument::fromJson(reply->readAll()).object();
        if (item.isEmpty()) {
            promise->addResult(Result<bool>::failure(QStringLiteral("item not found")));
            promise->finish();
            return;
        }
        item.insert(QLatin1String("Name"), name.trimmed());
        // ForcedSortName would otherwise keep the OLD name's sort position, so
        // a renamed playlist would file itself under a name it no longer has.
        item.remove(QLatin1String("ForcedSortName"));
        item.remove(QLatin1String("SortName"));

        QNetworkReply *post =
            startPost(QStringLiteral("/Items/%1").arg(itemId), item, context);
        connect(post, &QNetworkReply::finished, this, [this, post, promise, context] {
            post->deleteLater();
            if (context.epoch != m_requestEpoch)
                promise->addResult(Result<bool>::failure(QStringLiteral("request canceled")));
            else if (post->error() != QNetworkReply::NoError)
                promise->addResult(Result<bool>::failure(post->errorString()));
            else
                promise->addResult(Result<bool>::success(true));
            promise->finish();
        });
    });
    return future;
}

QFuture<Result<bool>> EmbyClient::deleteItem(const QString &itemId)
{
    if (!hasSession() || itemId.isEmpty())
        return failedFuture<bool>(QStringLiteral("nothing to delete"));
    return finishStatus(startDelete(QStringLiteral("/Items/%1").arg(itemId)));
}

QFuture<Result<bool>> EmbyClient::reportCapabilities(const QStringList &commands,
                                                    bool supportsMediaControl)
{
    if (!hasSession())
        return failedFuture<bool>(QStringLiteral("not authenticated"));

    QUrlQuery params;
    params.addQueryItem(QStringLiteral("Id"), m_deviceId);
    params.addQueryItem(QStringLiteral("PlayableMediaTypes"), QStringLiteral("Video,Audio"));
    params.addQueryItem(QStringLiteral("SupportedCommands"), commands.join(QLatin1Char(',')));
    params.addQueryItem(QStringLiteral("SupportsMediaControl"),
                        supportsMediaControl ? QStringLiteral("true") : QStringLiteral("false"));
    // Emby takes these in the query string with an empty body.
    return finishStatus(startPostQuery(QStringLiteral("/Sessions/Capabilities/Full"), params));
}

QFuture<Result<bool>> EmbyClient::reportPlaybackStart(const PlaybackProgress &progress)
{
    if (!hasSession())
        return failedFuture<bool>(QStringLiteral("not authenticated"));
    return finishStatus(startPost(QStringLiteral("/Sessions/Playing"), progressBody(progress)));
}

QFuture<Result<bool>> EmbyClient::reportPlaybackProgress(const PlaybackProgress &progress)
{
    if (!hasSession())
        return failedFuture<bool>(QStringLiteral("not authenticated"));
    return finishStatus(
        startPost(QStringLiteral("/Sessions/Playing/Progress"), progressBody(progress)));
}

QFuture<Result<bool>> EmbyClient::reportPlaybackStopped(const PlaybackProgress &progress)
{
    if (!hasSession())
        return failedFuture<bool>(QStringLiteral("not authenticated"));
    return finishStatus(
        startPost(QStringLiteral("/Sessions/Playing/Stopped"), progressBody(progress)));
}

QUrl EmbyClient::imageUrl(const QString &itemId, const QString &imageType, int maxWidth,
                          const QString &tag) const
{
    QUrlQuery params;
    if (maxWidth > 0)
        params.addQueryItem(QStringLiteral("maxWidth"), QString::number(maxWidth));
    if (!tag.isEmpty())
        params.addQueryItem(QStringLiteral("tag"), tag);
    params.addQueryItem(QStringLiteral("quality"), QStringLiteral("90"));
    // Emby's image "enhancers" composite decorations into the bytes it serves.
    // Measured on this server for an episode still: with enhancers ON the
    // Primary image comes back as a 640x438 PNG of a *television set* with the
    // still inside its bezel and an Emby logo across the stand, 375 KB; with
    // them OFF it is the plain 400x225 JPEG the still actually is, 48 KB.
    //
    // A native client draws its own card frame, so a second frame baked into
    // the pixels is not decoration, it is damage: it breaks the aspect ratio,
    // and the card then crops the bezel rather than the picture. Off for every
    // image, not just stills — the same enhancers apply to posters.
    params.addQueryItem(QStringLiteral("EnableImageEnhancers"), QStringLiteral("false"));
    return requestUrl(QStringLiteral("/Items/%1/Images/%2").arg(itemId, imageType), params);
}

} // namespace strmqt::emby
