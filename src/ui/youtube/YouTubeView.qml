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

    function pausePlayback(): void {
        view.runJavaScript("document.querySelectorAll('video').forEach(function (v) {"
                           + " if (!v.paused) v.pause(); });");
    }

    url: view.address
    // Asked for again later (a second trailer): YouTube is one page, and a new
    // address inside it is a move within that page, not a reload.
    onAddressChanged: view.url = view.address
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
        {
            "name": "strmqt-bridge",
            "injectionPoint": WebEngineScript.DocumentReady,
            "worldId": WebEngineScript.MainWorld,
            "runsOnSubFrames": false,
            "sourceCode": "(function () {"
                + " if (window.__strmqt) return; window.__strmqt = true;"
                + " function say(m) { console.log('strmqt:' + m); }"
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
        if (message === "strmqt:edge-left")
            view.leftEdgeReached();
        else if (message === "strmqt:playing")
            view.playbackStarted();
        else if (message.startsWith("strmqt:route:"))
            view.routeChanged(message.substring(13));
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
