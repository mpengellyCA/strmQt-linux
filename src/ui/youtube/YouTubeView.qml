import QtQuick
import QtWebEngine

// YouTube's own ten-foot interface (youtube.com/tv), in a web view.
//
// This file is the only one that names QtWebEngine, and it is in the module
// only when the build has it (STRMQT_WITH_YOUTUBE): YouTubePage loads it by
// URL and never names the type, the same arrangement the VLC video plane has.
//
// The view holds the keyboard for real. YouTube's interface is built for a
// remote — arrows, Enter, Back — and every non-pointer device here ends up as
// exactly those keys delivered to the focused item (ARCHITECTURE.md §5), so a
// pad, a remote and the phone drive it with no translation layer of ours.
//
// What a web view cannot do is DECLINE a key: Chromium accepts every one, so
// the navigator never hears about an arrow YouTube had no use for. The script
// below is the stand-in for declining. It watches YouTube's own focus, and a
// Left that moved nothing is reported to the page, which opens the rail.
WebEngineView {
    id: view

    // YouTube draws its own keyboard for search, operated by the arrows and
    // Enter delivered here; ours must not open over it (OnScreenKeyboard.h).
    property bool onScreenKeyboard: false

    // The route's address; set once by the page before the view loads.
    property url address: "https://www.youtube.com/tv"

    // Left was pressed and YouTube's focus did not move: its left edge.
    signal leftEdgeReached
    // YouTube moved to another of its own screens ("#/watch?v=…", "#/").
    signal routeChanged(string route)
    // A video started playing with sound the user can hear.
    signal playbackStarted
    // YouTube asked to close itself (its own "Exit YouTube" dialog).
    signal exitRequested

    // What YouTube is playing, as the mini player needs it (Main.qml):
    //   watching  a video is up (as opposed to a preview on a shelf)
    //   playing, title, position, duration (seconds), volume (0–1), muted
    signal stateReported(var state)

    function pausePlayback(): void {
        view.runJavaScript("document.querySelectorAll('video').forEach(function (v) {"
                           + " if (!v.paused) v.pause(); });");
    }
    // The verbs below act on the video YouTube is showing, through the page's
    // own <video>: there is no other handle on its player.
    function playPause(): void {
        view.runJavaScript("window.__strmqtVideo && (function (v) { if (v)"
                           + " (v.paused ? v.play() : v.pause()); })(window.__strmqtVideo());");
    }
    function resumePlayback(): void {
        view.runJavaScript("window.__strmqtVideo && (function (v) { if (v && v.paused && !v.ended)"
                           + " v.play(); })(window.__strmqtVideo());");
    }
    function seekBy(seconds: real): void {
        view.runJavaScript("window.__strmqtVideo && (function (v) { if (v && isFinite(v.duration))"
                           + " v.currentTime = Math.max(0, Math.min(v.duration - 1, v.currentTime + ("
                           + Number(seconds) + "))); })(window.__strmqtVideo());");
    }
    function seekTo(seconds: real): void {
        view.runJavaScript("window.__strmqtVideo && (function (v) { if (v && isFinite(v.duration))"
                           + " v.currentTime = Math.max(0, Math.min(v.duration - 1, "
                           + Number(seconds) + ")); })(window.__strmqtVideo());");
    }
    function setVolume(volume: real): void {
        view.runJavaScript("window.__strmqtVideo && (function (v) { if (v) { v.muted = false;"
                           + " v.volume = " + Math.max(0, Math.min(1, Number(volume)))
                           + "; } })(window.__strmqtVideo());");
    }
    function setMuted(muted: bool): void {
        view.runJavaScript("window.__strmqtVideo && (function (v) { if (v) v.muted = "
                           + (muted ? "true" : "false") + "; })(window.__strmqtVideo());");
    }
    // Stop is leaving the video: paused, and YouTube back on its front page,
    // so nothing is "up" any more and the mini player has nothing to show.
    function stopPlayback(): void {
        view.pausePlayback();
        view.runJavaScript("if (location.hash.indexOf('#/watch') === 0) location.hash = '#/';");
    }

    url: view.address
    // Asked for again later (a second trailer): YouTube is one page, and a new
    // address inside it is a move within that page, not a reload. A function
    // and not a change handler: the same trailer asked for twice is the same
    // address, and YouTube has moved on from it in between.
    function open(address: url): void {
        view.address = address;
        view.url = address;
    }

    // What the page's console must say first for a line to be the bridge's.
    // Made up afresh each run and known only to the script below, so another
    // site, or a frame from one inside YouTube, cannot speak for it.
    readonly property string bridgeWord: "strmqt:" + Math.random().toString(36).slice(2)
                                         + Math.random().toString(36).slice(2) + ":"
    backgroundColor: "#0f0f0f"

    // A TV identifies itself, and youtube.com/tv redirects anything else to
    // the desktop site.
    profile: WebEngineProfile {
        storageName: "youtube"
        offTheRecord: false
        persistentCookiesPolicy: WebEngineProfile.ForcePersistentCookies
        httpUserAgent: "Mozilla/5.0 (PS4; Leanback Shell) Gecko/20100101 Firefox/65.0"
                       + " LeanbackShell/01.00.01.75 Sony PS4/ (PS4, , no, CH)"
    }

    settings.playbackRequiresUserGesture: false
    settings.fullScreenSupportEnabled: false
    settings.javascriptCanOpenWindows: false
    settings.focusOnNavigationEnabled: false

    userScripts.collection: [
        // The page is always on show, as far as it can tell. A TV application
        // pauses when it is told it has been hidden — that is how a console
        // suspends it — and it does not resume when it is shown again. Here
        // "hidden" is the view moving between its page and the mini player,
        // or the window being minimised with a video the user left playing.
        // Before any of YouTube's own script, so this listener is the first.
        {
            "name": "strmqt-shown",
            "injectionPoint": WebEngineScript.DocumentCreation,
            "worldId": WebEngineScript.MainWorld,
            "runsOnSubFrames": false,
            "sourceCode": "(function () {"
                + " if (window.__strmqtShown) return; window.__strmqtShown = true;"
                + " function fixed(name, value) {"
                + "  try { Object.defineProperty(Document.prototype, name,"
                + "   { configurable: true, get: function () { return value; } }); } catch (e) {}"
                + " }"
                + " fixed('hidden', false); fixed('webkitHidden', false);"
                + " fixed('visibilityState', 'visible'); fixed('webkitVisibilityState', 'visible');"
                + " ['visibilitychange', 'webkitvisibilitychange'].forEach(function (name) {"
                + "  window.addEventListener(name, function (e) { e.stopImmediatePropagation(); }, true);"
                + " });"
                + "})();"
        },
        {
            "name": "strmqt-bridge",
            "injectionPoint": WebEngineScript.DocumentReady,
            "worldId": WebEngineScript.MainWorld,
            "runsOnSubFrames": false,
            "sourceCode": "(function () {"
                + " if (window.__strmqt) return; window.__strmqt = true;"
                + " if (location.hostname !== 'www.youtube.com') return;"
                + " function say(m) { console.log('" + view.bridgeWord + "' + m); }"
                // What YouTube has focused, as something that differs
                // whenever its cursor moved: the element itself, what it
                // says, where it is, and how far the lists around it have
                // scrolled. Position alone is not enough — in a shelf the
                // cursor stays put and the row slides under it.
                + " function where() {"
                + "  var a = document.activeElement; if (!a) return '';"
                + "  var r = a.getBoundingClientRect();"
                + "  var s = a.tagName + '|' + Math.round(r.left) + '|' + Math.round(r.top)"
                + "        + '|' + Math.round(r.width) + '|' + location.hash + '|'"
                + "        + (a.textContent || '').slice(0, 160) + '|'"
                + "        + ((a.querySelector && a.querySelector('img')) ? a.querySelector('img').src : '');"
                + "  for (var p = a, i = 0; p && p.style && i < 12; p = p.parentElement, ++i)"
                + "   s += '|' + p.style.transform + ',' + p.scrollLeft + ',' + p.scrollTop;"
                + "  return s;"
                + " }"
                // While a video is up, Left is a seek and moves no focus.
                + " function watching() { return location.hash.indexOf('#/watch') === 0; }"
                // YouTube's own menu is its left-hand edge: an unmoved Left
                // anywhere else is a list still loading or an animation, not
                // the edge. Where there is no menu at all (signing in, an
                // error screen) an unmoved Left is all there is to go on.
                + " function atLeftSide() {"
                + "  var guide = document.querySelector('ytlr-guide-response');"
                + "  if (!guide || guide.getBoundingClientRect().width === 0) return true;"
                + "  return guide.contains(document.activeElement);"
                + " }"
                + " document.addEventListener('keydown', function (e) {"
                + "  if (e.keyCode !== 37 || e.repeat || watching()) return;"
                + "  var node = document.activeElement, before = where();"
                + "  setTimeout(function () {"
                + "   if (document.activeElement === node && where() === before && atLeftSide())"
                + "    say('edge-left');"
                + "  }, 220);"
                + " }, true);"
                + " document.addEventListener('playing', function (e) {"
                + "  if (e.target && !e.target.muted) say('playing');"
                + " }, true);"
                // The video YouTube is showing: the one that is playing, else
                // the one that has something loaded.
                + " window.__strmqtVideo = function () {"
                + "  var all = document.querySelectorAll('video'), loaded = null;"
                + "  for (var i = 0; i < all.length; ++i) {"
                + "   if (!all[i].paused && !all[i].ended) return all[i];"
                + "   if (!loaded && all[i].currentSrc && all[i].readyState > 0) loaded = all[i];"
                + "  }"
                + "  return loaded;"
                + " };"
                + " var told = '';"
                + " function report() {"
                + "  var v = window.__strmqtVideo(), meta = null;"
                + "  try { meta = navigator.mediaSession && navigator.mediaSession.metadata; } catch (e) {}"
                + "  var state = JSON.stringify({"
                + "   watching: watching(),"
                + "   playing: !!v && !v.paused && !v.ended,"
                + "   title: (meta && meta.title) || document.title || '',"
                + "   artist: (meta && meta.artist) || '',"
                + "   position: v ? Math.floor(v.currentTime) : 0,"
                + "   duration: v && isFinite(v.duration) ? Math.floor(v.duration) : 0,"
                + "   volume: v ? v.volume : 1,"
                + "   muted: v ? v.muted : false"
                + "  });"
                + "  if (state !== told) { told = state; say('state:' + state); }"
                + " }"
                + " ['playing', 'pause', 'ended', 'volumechange', 'durationchange', 'seeked',"
                + "  'loadedmetadata', 'emptied'].forEach(function (name) {"
                + "  document.addEventListener(name, report, true);"
                + " });"
                + " window.addEventListener('hashchange', report);"
                + " setInterval(report, 1000);"
                // YouTube draws on a fixed 16:9 canvas, centred, and leaves
                // the rest of the window black. In its own background colour
                // the bars are margin instead of a hole in the page — except
                // beside a video, where black is what belongs there.
                + " var paint = document.createElement('style');"
                + " paint.textContent = 'html:not(.strmqt-watch), html:not(.strmqt-watch) body'"
                + "  + ' { background: #0f0f0f !important; }';"
                + " document.documentElement.appendChild(paint);"
                + " function mark() {"
                + "  document.documentElement.classList.toggle('strmqt-watch', watching());"
                + " }"
                + " mark();"
                + " window.addEventListener('hashchange', function () {"
                + "  mark();"
                + "  say('route:' + location.hash);"
                + " });"
                + "})();"
        }
    ]

    onJavaScriptConsoleMessage: (level, message, line, source) => {
        if (!message.startsWith(view.bridgeWord))
            return;
        const said = message.substring(view.bridgeWord.length);
        if (said === "edge-left")
            view.leftEdgeReached();
        else if (said === "playing")
            view.playbackStarted();
        else if (said.startsWith("route:"))
            view.routeChanged(said.substring(6));
        else if (said.startsWith("state:")) {
            // The page's words, so they are parsed as data and nothing more —
            // and only an object is a state.
            try {
                const state = JSON.parse(said.substring(6));
                if (state !== null && typeof state === "object" && !Array.isArray(state))
                    view.stateReported(state);
            } catch (error) {
            }
        }
    }

    onWindowCloseRequested: view.exitRequested()
    // The TV interface opens nothing in a new window of its own accord; a link
    // that tries (terms, help) stays where it is rather than opening a browser
    // window nothing in this app could close from a remote.
    onNewWindowRequested: request => {}
    onRenderProcessTerminated: (status, code) => {
        console.warn("YouTube view: render process ended (" + status + "), reloading");
        view.reload();
    }
}
