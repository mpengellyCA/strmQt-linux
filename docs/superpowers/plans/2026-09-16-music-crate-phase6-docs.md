# Music Crate, Phase 6: Docs and Final Gate

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the written record match the shipped Crate music surface, sweep references to deleted code, settle the qmllint baseline deliberately, and run the final gate for the release.

**Architecture:** see the index, `docs/superpowers/plans/2026-09-16-music-crate.md`. **Read its Global Constraints and Shared vocabulary before any task.** Phases 1–5 must be complete: every phase gate passed and every visual check was confirmed by the user.

**Spec:** `docs/superpowers/specs/2026-09-16-music-crate-design.md` §9–§11.

## Waves

| Wave | Tasks | Notes |
|---|---|---|
| 6a | 1, 2 | Independent documents (two agents; the orchestrator commits at the gate) |
| 6b | 3 | Needs the final tree |
| 6c | 4 | The final gate; the orchestrator runs it, with the user for the walkthrough |

---

### Task 1: ARCHITECTURE.md

**Files:**
- Modify: `ARCHITECTURE.md` §1, §2, §3 "Queue", §4 "Design language" and "Pages", §5, §7

**Interfaces:**
- Consumes: the shipped code from Phases 1–5, and `docs/superpowers/plans/2026-09-16-music-crate-verifications.md` (Phase 1 Task 2).
- Produces: documentation only.

Every factual sentence below must be checked against the tree before it is committed. If the code differs (for example, a phase recorded a contract note), write what the code does and not what this plan says.

- [ ] **Step 1: Check the facts this task writes down**

Run:

```bash
ls src/app/music src/app/music/models src/app/controllers/music src/ui/music src/server/dto/music
grep -n "objectName: \"music\|objectName: \"album\|objectName: \"artist" src/ui/Main.qml
grep -n "sourceLabel" src/app/PlayQueue.h
grep -n "animateRecord" src/core/Settings.h
ctest --preset dev -N | tail -1
cat docs/superpowers/plans/2026-09-16-music-crate-verifications.md
```

Expected:
- The five directories list the files named in the index's file structure.
- Main.qml has the page objectNames `musicHomePage`, `musicBrowsePage`, `albumPage`, `artistPage` and `musicPlaylistPage`.
- `sourceLabel` and `animateRecord` exist.
- `ctest -N` prints `Total Tests: N`. Note N for Step 7.
- The verifications file lists the V1–V10 outcomes.

If any of these is missing, stop and report it. The phase that owns it is not finished.

- [ ] **Step 2: §1: name the music domain layer**

After the paragraph on rule 4 ("Playback engines are interfaces…"), insert:

```markdown
Music has a domain layer of its own between the client and the pages. Emby's
item payloads are reshaped once, in C++, into music value types (`src/server/dto/music/`:
`Track`, `Album`, `AlbumSleeve`, `ArtistProfile`, `GenreBin`, `Station`, …) by
`EmbyMusicMapper`, which derives the format badge, featured artists and release type.
`MusicRepository` (`src/app/music/`) is the only music unit that holds an `EmbyClient`:
it composes several requests into one answer (a sleeve is the album, its tracks and
more by the artist, fetched in parallel), degrades a failed *secondary* request to an
empty part, and caches per account in memory. Typed list models and one controller per
surface sit above it, so a music page binds display-ready roles ("48 min",
"FLAC 24/96", "12 records") and never reshapes data itself. `MusicPlayback` owns every
music play verb and hands `ItemActions` ordinary queue maps, so the queue and the
verbs never learn about music types.
```

- [ ] **Step 3: §2: measured music behaviour**

Phase 1 Task 2 added rows to "Emby behaviour worth knowing". Compare them with the verifications file:
- Every V-item with a *consequence* in the code (a filter shown or hidden, a fallback query, an omitted tab) has one row.
- Each row states the measured fact and what the code does because of it.
- Delete any row that was only an example layout.
- Add a missing row in the table's style: bold only for behaviour that silently misleads.

For example, when V2 found `Years` does not take a decade list:

```markdown
| `Years=1970,…,1979` does not filter `MusicAlbum` | The Decade pill sends `MinPremiereDate`/`MaxPremiereDate` bounds instead. "Earlier" is a single upper bound at the end of 1949. |
```

Write only the rows the file supports.

- [ ] **Step 4: §3 "Queue": provenance**

Append to the "Queue" subsection:

```markdown
A queue also remembers **where it came from**: `PlayQueue::sourceLabel` ("Sunburned
Almanac", "Station · Heavy rotation", "Radio · Björk") is set by the play verb that
filled it and shown as "Playing from" in the music player. Remembering is only safe
because it is forgotten eagerly: replacing or clearing the queue, or adding anything
to it, drops the label, while reordering and removing keep it, and a film that
interrupts a record gives it back with the rest of the snapshot. Where no verb named
the source, the player falls back to `contextLabel`, which is derived from what the
queue holds.
```

- [ ] **Step 5: §4: the Crate dialect and the pages**

At the end of "Design language", append:

```markdown
Music speaks a dialect of it called *Crate*: the same ground, accent and typefaces,
set louder. Archivo is pushed wide and heavy for headings and hero titles, data sits
in Plex Mono, and an album is a square sleeve with a deep shadow and no card behind
it. Crate adds tokens (`Theme.crate*`) and no colours; the cover wash keeps its clamp.
```

Replace the pages list and the `MusicPage` paragraph (from `Pages: Login · Home …` to the end of the paragraph that ends `…has to keep offering film lists.`) with:

```markdown
Pages: Login · Home · Library · Details · Series · Person · Playlist · Search ·
Settings · Player, and for music: Music Home · Music Browse · Album · Artist ·
Music Playlist.

A music library opens on **Music Home**, a page of independently loading shelves
(pick up where you left off, recently played, new in the crate, stations, genre
bins, artists you play, forgotten favourites, pull one out). Each shelf is its own
lane: it has its own skeleton, hides when empty and collapses to one Retry line when
it fails, while the others stay live. **Music Browse** is the same library read
five ways (Albums · Artists · Songs · Genres · Playlists) under a section strip,
sharing one set of filter pills and keeping a sort per section. Its A–Z crate
dividers send a letter as a `NameStartsWithOrGreater`/`NameLessThan` pair, and
choosing a genre bin is a filter, not a page. Its Playlists section is the user's
*audio* playlists; the nav rail's Playlists destination is still all of them,
because a picker raised from a film has to keep offering film lists.
```

- [ ] **Step 6: §5: the music context**

In "There are three contexts, not two", replace:

```markdown
Browse, player, and *music* — the music
library, an album and an artist page.
```

with:

```markdown
Browse, player, and *music* — Music Home,
Music Browse, an album, an artist and an audio playlist page.
```

In the shoulders paragraph, replace `the four music tabs` with `the music section strip`.

- [ ] **Step 7: §7: the suite count**

Replace `# 33 suites` with `# N suites`, using N from Step 1.

- [ ] **Step 8: Commit**

```bash
git add ARCHITECTURE.md
git commit -m "docs: describe the Crate music layer, pages and queue provenance

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: MUSIC.md superseded, README, and the stale-reference sweep

**Files:**
- Modify: `MUSIC.md` (banner), `README.md` ("Music treated as music")
- Modify: any source or test comment that names deleted code (found in Step 1)

**Interfaces:**
- Consumes: the final tree.
- Produces: documentation and comments only. No behaviour changes.

- [ ] **Step 1: Find references to deleted code**

Run:

```bash
git grep -n -E "MusicController|MusicCtl|MusicPage|AlbumPage\.qml|ArtistPage\.qml|NowPlayingPanel|tst_music_query|\bmusicPage\b" -- ':!docs/superpowers' ':!MUSIC.md' ':!*CODEREVIEW.md' ':!AUDIT.md'
```

Expected: only comments and prose. Any hit in live code (an include, a context-property use, a CMake entry) is a bug in the phase that deleted the file; stop and report it. For each comment hit, rewrite the comment to name what replaced the code:

| Deleted | Replaced by |
|---|---|
| `MusicController` (queries, paging, tabs) | `MusicBrowseController` + `MusicRepository` |
| `MusicController::playAlbum` | `MusicPlayback::playAlbum` |
| `MusicController::playlists` | `MusicBrowseController` playlists section |
| `MusicPage` | `MusicBrowsePage` (or `MusicHomePage` for a library's landing page) |
| `AlbumPage` / `ArtistPage` | `MusicAlbumPage` / `MusicArtistPage` |
| `NowPlayingPanel` (audio) | `MusicPlayerPanel` + `RecordStage` |

Keep each comment's reasoning and change only the names. Do not rewrap or reformat code you are not changing (AGENTS.md scope discipline).

- [ ] **Step 2: Mark MUSIC.md superseded**

Insert directly under the `# Music — plan to make it a first-class citizen` title:

```markdown
> **Superseded (2026-09-16).** The music surface described here was replaced by the
> *Crate* redesign: `docs/superpowers/specs/2026-09-16-music-crate-design.md`. This
> file is kept because source comments still cite its reasoning (the cover wash in
> §4, the audio path in §6). Its page, controller and component names are historical.
```

Do not delete MUSIC.md. Source comments cite its sections as rationale.

- [ ] **Step 3: README**

Replace the paragraph under `### Music treated as music` with:

```markdown
A music library opens on its own home: pick up where you left off, stations, genre
bins, the artists you actually play, and a record pulled out at random. Browse it as
albums, artists, songs, genres or playlists with filter pills (genre, decade, format,
favourites, unplayed) and A–Z crate dividers. Albums show their liner notes, discs and
format; the full player pulls the record out of the sleeve. ReplayGain volume
normalisation, instant mixes from anything, and multi-select for batch favouriting
and queueing.
```

- [ ] **Step 4: Verify nothing live changed**

Run: `cmake --build --preset dev && ctest --preset dev --output-on-failure`
Expected: builds and passes, because the only changes are comments and prose.

- [ ] **Step 5: Commit**

```bash
git add MUSIC.md README.md
git add -u src tests
git commit -m "docs: mark MUSIC.md superseded and retire references to removed music code

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: qmllint baseline

**Files:**
- Modify: QML files with new warnings (as found)
- Modify: `config/qmllint-baseline.txt` (only for the deliberate changes described below)

**Interfaces:**
- Consumes: the final tree.
- Produces: a baseline that matches the tree, with each change justified.

- [ ] **Step 1: Run the check**

Run: `bash scripts/check-qmllint-baseline.sh build/dev`
Expected: either success, or a list of new and/or removed warning fingerprints.

- [ ] **Step 2: Fix every new warning in the music QML**

For each new fingerprint under `src/ui/music/`, `src/ui/pages/Music*.qml`, or a file a Crate phase modified:
- fix the QML: add a type annotation, qualify an id lookup, or use a `required property` for delegate roles;
- do not add it to the baseline.

The script fails on type-resolution categories regardless of the baseline. Those must be fixed.

Re-run Step 1 until no new warnings remain.

- [ ] **Step 3: Drop fingerprints of deleted files**

Warnings that belonged to deleted files (`MusicPage.qml`, `AlbumPage.qml`, `ArtistPage.qml`, `NowPlayingPanel.qml`, and the removed `FilterBar` branches) now show as *removed*. Accept exactly those:

```bash
bash scripts/check-qmllint-baseline.sh build/dev --update
git diff --stat config/qmllint-baseline.txt
git diff config/qmllint-baseline.txt | grep '^+' | grep -v '^+++'
```

Expected: the last command prints nothing. The update only removed lines. If it added any line, undo the update with `git checkout config/qmllint-baseline.txt` and return to Step 2.

- [ ] **Step 4: Commit**

```bash
git add config/qmllint-baseline.txt
git add -u src/ui
git commit -m "chore(qml): settle the qmllint baseline after the Crate redesign

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: Final gate and walkthrough

**Files:** none are modified unless the gate finds a defect. A fix goes back to the phase that owns the code, as a new commit.

**Interfaces:**
- Consumes: everything.
- Produces: a release-ready tree. Never push or tag; releasing is the user's call.

- [ ] **Step 1: Clean build in a fresh directory**

```bash
df -h /tmp
mkdir -p /tmp/wfinal/tmp
TMPDIR=/tmp/wfinal/tmp cmake -S . -B /tmp/wfinal -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSTRMQT_WERROR=ON
TMPDIR=/tmp/wfinal/tmp cmake --build /tmp/wfinal
TMPDIR=/tmp/wfinal/tmp ctest --test-dir /tmp/wfinal --output-on-failure
bash scripts/check-qmllint-baseline.sh /tmp/wfinal
STRMQT_SELFTEST=1 QT_QPA_PLATFORM=offscreen QT_ASSUME_STDERR_HAS_CONSOLE=1 /tmp/wfinal/strmqt; echo "selftest exit $?"
```

Expected: no warnings, all tests pass, no new qmllint warnings, `selftest exit 0`.

The flags are the `dev` preset's cache variables, spelled out so the fresh tree cannot pick up `build/dev`'s cache.

- [ ] **Step 2: Secrets and wire-hygiene check**

`BASE` is the spec commit this plan builds on:

```bash
BASE=$(git log --format=%h --grep='docs(music): add Crate redesign spec' -1)
git diff "$BASE" --stat
git diff "$BASE" -- . ':!docs/superpowers' | grep -n -i -E "https?://[^ )\"']+" | grep -v -E "localhost|127\.0\.0\.1|example\.(com|org)|claude\.com|anthropic\.com|github\.com|qt\.io"
git diff "$BASE" -- . ':!docs/superpowers' | grep -n -i -E "api_key|x-emby-token: [a-f0-9]{32}|password\s*[:=]"
git diff "$BASE" -- . ':!docs/superpowers' | grep -n -i -E "ignoreSslErrors|QSslSocket::VerifyNone"
```

Expected: the last three commands print nothing. The plan documents are excluded because they quote these patterns. Any hit is removed before continuing (AGENTS.md: no credentials or server URLs in the repo; TLS errors are fatal).

- [ ] **Step 3: Walkthrough with the user**

Ask the user to run the app against their server (`./build/dev/strmqt`) and confirm each item. Record any failure as a defect against the owning phase.

1. Opening the music library lands on Music Home. The hero resumes the album last played (or shows "Pull one out" with no history). Shelves load independently, and an empty shelf is hidden.
2. Stations: each of the five plays on one press, and the player shows "Playing from · Station · …".
3. The section strip reaches every Browse section. The shoulders cycle sections. Back/Forward restore the exact focused item.
4. Browse filters: genre (picker with counts), decade, format (only when the server filters it), favourites and unplayed. The count text reads "unfiltered → filtered RECORDS · SORT: …". Clear all appears with two or more filters, and an empty result offers clearing buttons.
5. The A–Z crate dividers jump by letter with the name sort, and the triggers do the same from a pad.
6. Album page: liner notes without empty rows, disc headings only on multi-disc albums, the artist column only on guest rows, the amber now-playing row, and More by the artist. Play, Shuffle and Radio set the right labels.
7. Artist page: the poster header; Albums / EPs & Singles / Appears on tabs, with empty tabs hidden; most played; similar artists (when the server supports them).
8. Audio playlist page: reorder, remove, rename and delete work as before. Video playlists still open the old page.
9. Docked bar: 72 px cover, amber playhead, record slice while playing. Video is unchanged.
10. Full player: the record slides out and spins while playing, decelerates on pause, holds while buffering, swaps on an album change and slides in on stop. *Animate record* off gives a static record. Up next / Album / Lyrics (when available) tabs. Every existing shortcut and pad mapping still works.
11. Favouriting a track or album anywhere updates every visible music surface without a refetch.
12. Signing out and in as another user shows none of the first user's shelves.

- [ ] **Step 4: Clean up and report**

```bash
rm -rf /tmp/wfinal /tmp/w1* /tmp/w2* /tmp/w3* /tmp/w4* /tmp/w5* /tmp/w6*
git status --short
git log --oneline "$(git log --format=%h --grep='docs(music): add Crate redesign spec' -1)"..HEAD | wc -l
```

Expected: a clean tree (apart from the user's own untracked files). Report the commit count, the test count and the walkthrough result to the user. Do not push, tag or bump the version. The release is the user's decision.
