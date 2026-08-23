#pragma once

#include "MediaItem.h"
#include "MediaSource.h"

#include <QMap>
#include <QStringList>
#include <QVariantMap>

namespace strmqt {

// One entry of Emby's "Chapters" array. Positions are 100 ns ticks on the wire;
// startMs() is what every consumer actually wants (scrubber markers, PgUp/PgDn
// jumps, the chapter list panel).
struct Chapter
{
    QString name;
    qint64 startPositionTicks = 0;
    QString imageTag; // Primary image tag for the chapter thumbnail, if any

    qint64 startMs() const { return startPositionTicks / kTicksPerMs; }

    QVariantMap toVariantMap() const
    {
        QVariantMap map;
        map.insert(QStringLiteral("name"), name);
        map.insert(QStringLiteral("startPositionTicks"), QVariant::fromValue(startPositionTicks));
        map.insert(QStringLiteral("startMs"), QVariant::fromValue(startMs()));
        map.insert(QStringLiteral("imageTag"), imageTag);
        return map;
    }
};

// A name the server also knows by id: a genre, a studio, a tag. The id is what
// makes a link navigable — querying by name works but breaks on punctuation and
// case, and Emby's own web UI navigates by id.
struct NamedId
{
    QString id;
    QString name;

    QVariantMap toVariantMap() const
    {
        QVariantMap map;
        map.insert(QStringLiteral("id"), id);
        map.insert(QStringLiteral("name"), name);
        return map;
    }
};

// One entry of Emby's "People" array. `type` is the server's vocabulary
// ("Actor", "Director", "Writer", "Producer", "GuestStar"); `role` is the
// character for an actor and empty for everyone else. `primaryImageTag` is
// empty for people the server has no headshot for, which is common enough that
// a cast card must have a real fallback rather than a broken image.
struct Person
{
    QString id;
    QString name;
    QString role;
    QString type;
    QString primaryImageTag;

    bool hasImage() const { return !primaryImageTag.isEmpty(); }

    QVariantMap toVariantMap() const
    {
        QVariantMap map;
        map.insert(QStringLiteral("id"), id);
        map.insert(QStringLiteral("name"), name);
        map.insert(QStringLiteral("role"), role);
        map.insert(QStringLiteral("type"), type);
        map.insert(QStringLiteral("primaryImageTag"), primaryImageTag);
        return map;
    }
};

// An off-site link the *server* built (Emby's "ExternalUrls"). Taken as given
// rather than assembled from ProviderIds: the server already knows that a TVDB
// movie link is /dereferrer/movie/<id> while a series link is not, and building
// those URLs here would mean re-deriving per-provider rules we would then have
// to keep in step with the server.
struct ExternalLink
{
    QString name;
    QString url;

    QVariantMap toVariantMap() const
    {
        QVariantMap map;
        map.insert(QStringLiteral("name"), name);
        map.insert(QStringLiteral("url"), url);
        return map;
    }
};

// Full single-item payload for the Details page (genres/people/tagline on top
// of the base MediaItem), plus the playable versions and chapter list the
// server sends when Fields=MediaSources,MediaStreams,Chapters is requested.
//
// mediaSources is what a version picker and a media-info surface are built on:
// without it the details page can say nothing about codec, resolution, bitrate
// or track languages until playback has already started.
struct ItemDetails
{
    MediaItem item;
    QString tagline;
    QStringList genres;
    QStringList directors;
    QStringList cast; // actor names, server order (billing)

    // Navigable and renderable forms of the same information. `people` keeps the
    // server's order, which is billing order for actors and is the order a cast
    // list is expected to read in.
    QList<Person> people;
    QList<NamedId> genreItems;
    QList<NamedId> studios;
    QList<ExternalLink> externalLinks;
    // Off-site trailer URLs the server holds (RemoteTrailers). Same shape as an
    // external link and treated the same way: opened, not played.
    QList<ExternalLink> trailers;
    QMap<QString, QString> providerIds; // "Imdb" → "tt10872600"

    QString premiereDate;      // ISO-8601 as the server sent it
    double criticRating = 0.0; // 0-100 ("93"), 0 when absent

    QList<MediaSource> mediaSources;
    QList<Chapter> chapters;

    // Actors and directors as full records, server order preserved.
    QList<Person> peopleOfType(const QString &type) const
    {
        QList<Person> out;
        for (const Person &person : people) {
            if (person.type.compare(type, Qt::CaseInsensitive) == 0)
                out.append(person);
        }
        return out;
    }
};

} // namespace strmqt
