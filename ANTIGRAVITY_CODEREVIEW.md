# Antigravity End-to-End Code Review — StrmQt

**Date:** 2026-09-14  
**Reviewer:** Google Antigravity (Advanced Agentic Coding)  
**Scope:** Whole-project audit of `src/` (application architecture, playback lifecycle, concurrency & threading, memory management, platform integrations, security, QML/C++ boundaries, tests, and build system).  
**Relationship to Earlier Reviews:** Builds upon and verifies the remediations from `AUDIT.md`, `KIMI_CODEREVIEW.md`, `FABLE_CODEREVIEW.md`, and `SOL_CODEREVIEW.md`. Prior fixes (e.g. load epochs, image cache partitioning, delegate reuse, bounded navigation history) were confirmed in place. Candidate findings below were verified against the live C++20/Qt 6.11 codebase and test suite (42/42 tests passing).

---

## Executive Summary

The StrmQt codebase demonstrates exceptionally high architectural discipline. The separation of concerns between QML presentation, C++ controllers, and backend-neutral DTOs is robust. Previous audit issues regarding ghost playback, unbounded navigation page graphs, and unpartitioned disk caches have been systematically addressed:
- Navigation history is strictly bounded (`BoundedNavigationStack.qml`).
- Image cache is partitioned by server/user SHA-256 digests and enforces decoded pixel ceilings (`EmbyImageProvider`).
- Media items are indexed by ID to prevent quadratic scans on live updates (`MediaItemModel`).
- Playback commands carry monotonic intent epochs (`ItemActions`, `PlayerController`).

However, this deep technical sweep identified **2 High-severity defects, 4 Medium-severity issues, and 2 Low/Architectural findings** that warrant remediation:
1. **Critical UI Stutter & Frame Lock in VLC Video Pipeline**: Mutex held across the entire decoding interval.
2. **OpenGL State Desync during MpvRenderer Destruction**: Missing Qt RHI command fences.
3. **Plaintext Vault File Creation Race Condition**: TOCTOU permissions exposure.
4. **Orphaned User Data on Profile Removal**: `sessions/<scope>` settings survive account deletion.
5. **Sensitive Token Redaction Gap**: Tokens can leak in general Qt network and WebSocket logs.
6. **Disk Thrashing from Rapid Gamepad Volume Updates**: Unthrottled `QSettings` writes.
7. **Unbounded Avatar Image Requests**: Profile picker bypasses `EmbyImageProvider`.
8. **Server Abstraction Debt**: Direct coupling between controllers and `EmbyClient`.

---

## Priority Map

| ID | Severity | Area | Summary |
|---|---|---|---|
| **AGY-01** | High | Playback / Concurrency | `vlcframes::Buffers` holds mutex across entire decoding span, blocking GUI thread |
| **AGY-02** | High | Rendering / Concurrency | `~MpvRenderer()` and `synchronize()` free mpv context without Qt RHI command fencing |
| **AGY-03** | High | Security / Platform | `writePlaintextSecretFile` creates vault file before setting 0600 permissions |
| **AGY-04** | Medium | Session / Privacy | Profile removal from registry does not wipe `sessions/<scope>` preferences |
| **AGY-05** | Medium | Logging / Security | `redactSensitiveText` is only invoked by `MpvPlayer`; no global log message filter |
| **AGY-06** | Medium | Persistence / I/O | Gamepad stick volume scrubbing triggers rapid unthrottled `QSettings` writes |
| **AGY-07** | Low | UI / Caching | `profileAvatarUrl` bypasses `EmbyImageProvider` and image dimension/memory limits |
| **AGY-08** | Low | Architecture | `MediaServerBackend` is an empty interface; controllers directly bind to `EmbyClient` |

---

## Detailed Findings

### AGY-01 — VLC Frame Buffer Double-Locking Causes GUI Thread Stutter
- **File:** [`src/playback/vlc/VlcPlayer.h:75-109`](file:///home/mike/Dev/EmbyQt/src/playback/vlc/VlcPlayer.h#L75-L109)
- **Severity:** High
- **Mechanism:**  
  In `vlcframes::Buffers`:
  ```cpp
  void *lock(void **planes) {
      m_mutex.lock();
      // ... select target (&m_back or &m_scratch) ...
      return reinterpret_cast<void *>(m_nextToken);
  }
  void unlock() { m_mutex.unlock(); }
  ```
  The libvlc video callback protocol invokes `lockCb`, then proceeds to decode the video frame directly into the returned buffer, and calls `unlockCb` only after decoding has finished. Because `m_mutex` is held continuously from `lock()` to `unlock()`, the mutex is held for the entire frame decode duration (several milliseconds per frame, or tens of ms on 4K/high-bitrate media).  
  Meanwhile, Qt Quick's GUI/render thread calls `VlcPlayer::currentFrame()` during `VlcVideoItem::paint()`, which calls `m_frames.current()`:
  ```cpp
  QImage current() const {
      QMutexLocker lock(&m_mutex);
      return m_hasFront ? m_front : QImage{};
  }
  ```
  The GUI thread is forced to block waiting for libvlc to finish decoding each frame.
- **Scenario:** During playback using the VLC backend, moving the mouse, displaying the OSD, navigating menus, or resizing the window produces severe UI stutter and frame drops because the Qt event loop stalls on `m_mutex`.
- **Recommendation:** Double buffering is designed to decouple the reader (`m_front`) from the writer (`m_back`). `m_mutex` should only protect buffer pointer swaps and token validation, not the decoding interval:
  1. In `lock()`, acquire `m_mutex`, select the target buffer, advance tokens, release `m_mutex`, and return `target->bits()`.
  2. `unlock()` becomes a no-op.
  3. `display()` briefly locks `m_mutex` to swap `m_front` and `m_back`.

---

### AGY-02 — MPV OpenGL Context Destruction Lacks RHI External Command Fences
- **File:** [`src/playback/mpv/MpvVideoItem.cpp:72-78, 89-95`](file:///home/mike/Dev/EmbyQt/src/playback/mpv/MpvVideoItem.cpp#L72-L78)
- **Severity:** High
- **Mechanism:**  
  In `MpvVideoItem.cpp:118-120`, the author correctly documented:  
  `// mpv issues raw GL alongside the RHI — fence it off from Qt's own state.`  
  and wrapped `mpv_render_context_render` inside `window->beginExternalCommands()` and `window->endExternalCommands()`.  
  However, in `~MpvRenderer()` and `MpvRenderer::synchronize()`:
  ```cpp
  ~MpvRenderer() override {
      m_window.store(nullptr, std::memory_order_release);
      if (m_context) {
          mpv_render_context_set_update_callback(m_context, nullptr, nullptr);
          mpv_render_context_free(m_context);
      }
  }
  ```
  `mpv_render_context_free(m_context)` is invoked directly on the render thread without external command fencing. Freeing the mpv render context deletes OpenGL textures, FBOs, and shader programs. Because Qt RHI is not notified, its cached OpenGL state goes out of sync with driver state.
- **Scenario:** When navigating back from `PlayerPage` or switching media backends, `~MpvRenderer()` tears down the mpv context. The Qt scene graph continues rendering subsequent browse views, resulting in corrupted UI textures, black screens, or driver crashes.
- **Recommendation:** Retain `m_window` until after teardown and wrap context destruction with external command fences:
  ```cpp
  QQuickWindow *win = m_window.load(std::memory_order_acquire);
  if (win) win->beginExternalCommands();
  mpv_render_context_set_update_callback(m_context, nullptr, nullptr);
  mpv_render_context_free(m_context);
  if (win) win->endExternalCommands();
  m_window.store(nullptr, std::memory_order_release);
  ```

---

### AGY-03 — Plaintext Vault Creation Race Condition (TOCTOU Permissions Exposure)
- **File:** [`src/platform/SecretsStore.cpp:166-180`](file:///home/mike/Dev/EmbyQt/src/platform/SecretsStore.cpp#L166-L180)
- **Severity:** High
- **Mechanism:**  
  When KWallet is unavailable and `SecretsStore` falls back to the vault file (`secrets.ini`), it writes the secret using `QSettings`:
  ```cpp
  QSettings store(path, QSettings::IniFormat);
  store.setValue(key, value);
  store.sync();
  // ...
  QFile::setPermissions(path, ownerOnly);
  ```
  `QSettings::sync()` creates the file using the default process `umask` (typically `0022` or `0002`), writing the access token to a world- or group-readable file (`0644` / `0664`). The permissions are only tightened to `0600` *after* the file has been written and synced to disk.
- **Scenario:** On a shared Linux system, an untrusted local user or background monitoring daemon can watch `~/.local/share/StrmQt/` with `inotify` and read the access token during the window between `sync()` and `setPermissions()`.
- **Recommendation:** Create the parent directory with `0700` (`QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner`), and initialize the vault file with `0600` permissions (or temporarily wrap the creation block with `umask(0077)`) *before* writing any secrets.

---

### AGY-04 — Orphaned Session Preferences on Account Removal
- **File:** [`src/core/Settings.cpp:170-193`](file:///home/mike/Dev/EmbyQt/src/core/Settings.cpp#L170-L193), [`src/app/controllers/SessionController.cpp:349-372`](file:///home/mike/Dev/EmbyQt/src/app/controllers/SessionController.cpp#L349-L372)
- **Severity:** Medium
- **Mechanism:**  
  When an account profile is deleted via `SessionController::removeProfile()`, the access token is removed from `SecretsStore` and `Settings::removeAccountProfile()` removes the entry from the `accounts/registry` JSON list in `strmqt.ini`.  
  However, `removeAccountProfile()` does not prune the `sessions/<scope>/*` settings hierarchy. All scoped preferences—including playback resume points (`resume/itemId`, `resume/positionMs`), recent search history, remembered tracks, and library display modes—remain indefinitely in `strmqt.ini`.
- **Scenario:** A user logs out and selects "Forget Profile" on a shared computer. A subsequent user can inspect `~/.config/StrmQt/strmqt.ini` and view the previous user's complete watch history, search queries, and media preferences.
- **Recommendation:** In `Settings::removeAccountProfile()`, calculate the session scope via `sessionScopeFor(serverUrl, userId)` and remove the entire `sessions/<scope>` group:
  ```cpp
  const QString scope = sessionScopeFor(serverUrl, userId);
  if (!scope.isEmpty()) {
      m_store.remove(QStringLiteral("sessions/%1").arg(scope));
      m_store.sync();
  }
  ```

---

### AGY-05 — Sensitive Token Redaction Gap in Server and WebSocket Logging
- **File:** [`src/core/Log.cpp:12-32`](file:///home/mike/Dev/EmbyQt/src/core/Log.cpp#L12-L32), [`src/playback/mpv/MpvPlayer.cpp:764-765`](file:///home/mike/Dev/EmbyQt/src/playback/mpv/MpvPlayer.cpp#L764-L765)
- **Severity:** Medium
- **Mechanism:**  
  `Log.cpp` provides `redactSensitiveText(text)` which strips `api_key`, `access_token`, and auth headers. However, this helper is only called in `MpvPlayer.cpp` for mpv diagnostic messages.  
  `initLogging()` merely configures `qSetMessagePattern` and does not install a `QtMessageHandler`. Consequently, any other subsystem that outputs URLs or HTTP error descriptions—such as `EmbyClient` warnings (`qCWarning(logServer) << "request failed:" << reply->url().path() << ...`), `EmbyWebSocket` error dumps, or QML `Image.Error` messages—prints raw URLs containing API tokens to stderr or the system journal.
- **Scenario:** A reverse proxy or server returns an HTTP 500 or WebSocket error on an endpoint with query-parameter authentication (`/embywebsocket?api_key=...`). The unredacted URL and token are printed to stderr and saved in the systemd journal or application logs.
- **Recommendation:** Install a central message handler in `initLogging()` via `qInstallMessageHandler()` that passes formatted messages through `redactSensitiveText()` before writing to standard error.

---

### AGY-06 — Rapid Disk Thrashing on Gamepad Volume Scrubbing
- **File:** [`src/core/Settings.cpp:382-389`](file:///home/mike/Dev/EmbyQt/src/core/Settings.cpp#L382-L389)
- **Severity:** Medium
- **Mechanism:**  
  In `Settings::setVolume`:
  ```cpp
  void Settings::setVolume(int percent) {
      const int clamped = qBound(0, percent, kMaxVolume);
      if (clamped == volume()) return;
      m_store.setValue(kVolumeKey, clamped);
      emit volumeChanged();
  }
  ```
  Unlike `setLastPlayback()`, which uses a 60-second dirty timer (`m_pendingLastPlayback`) to avoid disk thrashing, `setVolume()` immediately writes to `m_store` on every single percent change. Holding the gamepad right stick adjusts volume at a rate of up to 26 events per second, causing continuous atomic rewrites of `strmqt.ini`.
- **Scenario:** A user holds the gamepad stick to fade volume from 100 to 20. Dozens of INI rewrite requests are posted to the event loop, causing disk churn and increasing the window for file corruption if an unexpected crash or power loss occurs.
- **Recommendation:** Implement a short debounce timer (e.g. 500 ms) for volume persistence, or throttle volume writes in `PlayerController` so only the settled volume is committed to `QSettings`.

---

### AGY-07 — Unbounded Avatar Requests Bypass Image Provider Limits and Cache
- **File:** [`src/app/controllers/SessionController.cpp:132-135`](file:///home/mike/Dev/EmbyQt/src/app/controllers/SessionController.cpp#L132-L135), [`src/ui/controls/StrmAvatar.qml:76-79`](file:///home/mike/Dev/EmbyQt/src/ui/controls/StrmAvatar.qml#L76-L79)
- **Severity:** Low
- **Mechanism:**  
  `SessionController::profileAvatarUrl` returns a raw HTTP URL (`https://.../Users/.../Images/Primary`), which `StrmAvatar.qml` feeds into `StrmImage`. Because the scheme is raw HTTP rather than `image://emby/...`:
  1. The request bypasses `EmbyImageProvider`'s partition cache and decode thread pool.
  2. It bypasses `imagelimits.h` protections against oversized payloads and image decompression bombs.
  3. During offscreen self-tests (`STRMQT_SELFTEST=1`), the avatar loader attempts real network requests against previously stored profile servers, emitting HTTP 404/transfer warnings.
- **Scenario:** A developer or CI environment runs `STRMQT_SELFTEST=1` on a machine with stored profiles; unmocked external network requests are dispatched, failing with log warnings.
- **Recommendation:** Route profile avatars through `EmbyImageFetcher::sourceFor()` or an unauthenticated public image route within `EmbyImageProvider`, and stub avatar URLs when `STRMQT_SELFTEST` is active.

---

### AGY-08 — Server Backend Abstraction Debt
- **File:** [`src/server/MediaServerBackend.h:7-20`](file:///home/mike/Dev/EmbyQt/src/server/MediaServerBackend.h#L7-L20)
- **Severity:** Low (Architectural)
- **Mechanism:**  
  While `PlayerBackend` provides an interface implemented by `MpvPlayer` and `VlcPlayer`, `MediaServerBackend` is an empty nominal class. Controllers (`HomeController`, `LibraryController`, `MusicController`, etc.) depend directly on `emby::EmbyClient *`. Supporting Jellyfin or Plex is currently blocked by tight coupling across all controllers rather than just writing a new backend implementation.
- **Recommendation:** Evolve `MediaServerBackend` into a cohesive interface representing the queries and verbs controllers consume, or explicitly document that the server seam is currently consolidated around Emby REST conventions.

---

## Code Smell & Clean Code Review

| Smell | Location | Evaluation |
|---|---|---|
| **Feature Envy** | `src/ui/shell/MiniPlayer.qml` | derivation of subtitle/title strings from queue items was previously duplicated across `NowPlayingPanel`; now largely unified in `MediaItemModel::dataForItem`. Remaining QML derivations are presentation-only. |
| **Primitive Obsession** | `src/server/emby/EmbyClient.cpp` | Endpoint IDs and query parameters are passed as `QString`. Acceptable given QtNetwork patterns and Emby wire format requirements. |
| **Speculative Generality** | `src/server/MediaServerBackend.h` | An empty interface that currently provides no runtime polymorphism (see AGY-08). |

---

## Verification & Validation Summary

- **Unit & Integration Tests:** 42 of 42 suites passed (`ctest --preset dev` executed in 77.55s).
- **Offscreen Self-Test:** 13 of 13 pages successfully instantiated (`STRMQT_SELFTEST=1 QT_QPA_PLATFORM=offscreen ./build/dev/strmqt`).
- **QML Lint:** Exits 0, warning fingerprint matches reviewed baseline.
