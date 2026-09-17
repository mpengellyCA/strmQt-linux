#pragma once

// What Emby 4.9.5 was measured to honour for music (Phase 1 Task 2 of the
// Crate plan; evidence in docs/superpowers/plans/2026-09-16-music-crate-verifications.md).
// Code branches on these instead of probing at runtime. Re-measure before
// changing one.
namespace strmqt::emby::caps {

inline constexpr bool kAudioCodecsFiltersAudio = true;   // V1a
inline constexpr bool kAudioCodecsFiltersAlbums = false; // V1b (drops every album)
inline constexpr bool kHiResFilter = false;              // V1c
inline constexpr const char *kHiResQueryKey = "";        // V1c
inline constexpr const char *kHiResQueryValue = "";      // V1c
inline constexpr bool kYearsAcceptsDecade = true;        // V2
inline constexpr const char *kReleaseTypeField = "";     // V3 ("" = none)
inline constexpr bool kLyricsAvailable = true;           // V4 (untimed text subtitle stream)
inline constexpr bool kSimilarArtists = true;            // V5
inline constexpr const char *kSimilarArtistsPath = "/Artists/{id}/Similar"; // V5
inline constexpr bool kMinDateCreated = true;            // V6
inline constexpr bool kAlbumPlayCountSort = false;       // V7 (albums keep no play data)
inline constexpr bool kGenreItemCounts = false;          // V8
inline constexpr bool kMediaStreamsOnLists = true;       // V9
inline constexpr bool kAlbumRuntimeOnLists = true;       // V10

} // namespace strmqt::emby::caps
