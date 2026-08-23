# AGENTS.md — StrmQt repo rules

Binding conventions for anyone — human or agent — working in this repository.
Read `ARCHITECTURE.md` first: it explains how the application is built and, more
usefully, why several parts of it look the way they do.

## Build & test

```bash
cmake --preset dev                 # configure (Ninja, warnings-as-errors)
cmake --build --preset dev         # build
ctest --preset dev                 # all tests
./build/dev/strmqt                 # run the app
```

Never commit code that fails configure, build, or tests. If a pre-existing failure blocks
you, say so instead of working around it silently.

## Hard rules

- C++20, Qt 6 (6.11 on the target machine). No .NET, no Emby SDKs, no wrapper libraries
  around libmpv/libvlc/ffmpeg — direct C API calls only.
- New dependencies need a stated reason before use, recorded in the commit that adds them.
- No credentials, tokens, or server passwords in the repo. Secrets go through
  `platform/SecretsStore` (KWallet on KDE).
- TLS certificate errors are fatal in release builds — never add SSL-ignore code.
- Scope discipline: change only what the current milestone needs. No drive-by refactors,
  renames, or reformatting of untouched code.

### Agent-fleet builds — the orchestrator cleans up

Each parallel agent gets its own build directory (`/tmp/w<wave><agent>`), and one is
**~1 GB**. `/tmp` here is a **16 GB tmpfs**, so roughly a dozen of them fill it.

**The orchestrator deletes every agent build directory at the wave's integration gate,
in the same step as the commit.** Not "eventually", and never the agents' job — they
cannot know when their tree stopped being useful, and an agent deleting a sibling's
directory mid-wave is worse than the disk filling.

This is not housekeeping. A full `/tmp` fails builds that do not even live there,
because GCC writes its assembler temporaries to `/tmp` regardless of `-B`. It has
already cost one agent a failed link and another a lost build directory, and the
failure mode is the dangerous one: **a plausible-looking log with a non-zero exit
code**. Relocating only the build directory does not help; `TMPDIR` has to move too.

```bash
df -h /tmp                 # check before launching a wave
rm -rf /tmp/w<wave>*       # at the gate, once every agent has reported
```

## Layout

- `src/core` — settings, session, cache, logging (no QtGui dependency).
- `src/server` — `MediaServerBackend` interface + per-service backends (`emby/` first).
  Backend-neutral DTOs in `src/server/dto`.
- `src/playback` — `PlayerBackend` interface, `PlayerController` (watchdog/ladder/resume),
  engines in `mpv/`, `vlc/`, `qtmultimedia/`.
- `src/platform` — MPRIS2, KDE Connect bridge, secrets, power inhibit, HDR probing.
  Everything here must degrade gracefully when the host lacks the feature.
- `src/input` — InputRouter unifying keyboard/mouse/SDL3 gamepad/KDE Connect events.
- `src/ui` — QML only: custom `StrmStyle`, reusable components, pages. No business logic
  in QML/JS; QML talks to controllers exposed from C++.

## Code style

- Match the surrounding file. `.clang-format` / `.clang-tidy` are authoritative once added
  (creating them is an M0 task).
- Qt idioms: signals/slots for async results, `Q_PROPERTY` + `Q_INVOKABLE` for QML-facing
  controllers, parent-child ownership, `QString` at boundaries / UTF-8 across the wire.
- JSON parsing is tolerant: unknown fields ignored, missing fields defaulted. Emby API
  drift must never crash the app.

## Testing

- Qt Test for unit tests (`tests/unit`); recorded Emby JSON fixtures live in
  `tests/mocks/emby` — no network in unit tests.
- Integration tests may spin up a local mock server; playback smoke tests use
  ffmpeg-generated media (including a truncated file for broken-tail recovery).
- Add tests when adding logic to `src/core`, `src/server`, or `src/playback`. UI polish
  and QML wiring are verified manually and noted in the commit.

## Git

- Commit per working increment with a conventional message (`feat(scope): ...`,
  `fix(scope): ...`, `test(scope): ...`, `docs: ...`).
- Never push, force-push, amend, rebase, or delete branches unless the user explicitly asks.
