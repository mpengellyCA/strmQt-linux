#pragma once

#include "server/dto/ItemDetails.h"
#include "server/dto/ItemsPage.h"
#include "server/dto/Library.h"
#include "server/dto/MediaItem.h"
#include "server/dto/MediaSource.h"
#include "server/dto/PlaybackTicket.h"
#include "server/dto/SessionInfo.h"
#include "server/dto/UserProfile.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>

namespace strmqt::emby {

// Emby 4.9 JSON → backend-neutral DTOs. All functions are tolerant: unknown fields
// are ignored, missing fields fall back to the DTO defaults (AGENTS.md rule — API
// drift must never crash the app).

UserProfile parseUser(const QJsonObject &json);
SessionInfo parseAuthResult(const QJsonObject &json);
Library parseLibrary(const QJsonObject &json);
QList<Library> parseViews(const QJsonObject &json);
MediaItem parseMediaItem(const QJsonObject &json);
ItemDetails parseItemDetails(const QJsonObject &json);
QList<MediaItem> parseItemArray(const QJsonArray &json);
ItemsPage parseItemsPage(const QJsonObject &json);

// "MediaStreams" entry → MediaStream. Unparseable/absent fields fall back to the
// DTO defaults; nothing here can throw or crash on server drift.
MediaStream parseMediaStream(const QJsonObject &json);
// "MediaSources" entry → MediaSource (streams included).
MediaSource parseMediaSource(const QJsonObject &json);
// Whole "MediaSources" array; non-object entries are skipped.
QList<MediaSource> parseMediaSources(const QJsonArray &json);

// "Chapters" entry → Chapter, and the whole array. Emby sends chapters only
// when Fields=Chapters is requested; an absent or malformed array is an empty
// list, never an error.
Chapter parseChapter(const QJsonObject &json);
QList<Chapter> parseChapters(const QJsonArray &json);

// PlaybackInfo response → one ordered stream ladder *per media source*. Needs
// connection context because candidate URLs must be absolute and
// self-authenticating (api_key query) — the player engine does its own HTTP and
// cannot send our headers.
PlaybackTicket parsePlaybackTicket(const QJsonObject &json, const QUrl &baseUrl,
                                   const QString &itemId, const QString &accessToken,
                                   const QString &deviceId);

} // namespace strmqt::emby
