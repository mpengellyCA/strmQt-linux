import QtQuick
import QtWebEngine

// YouTube Music (music.youtube.com), in a web view.
//
// YouTube's TV interface (YouTubeView.qml) is built for a remote and is thin
// on music; this is the site that is built for music and is not built for a
// remote at all. Three things stand in for that:
//
//  - it is drawn large. Unlike the TV interface this is an ordinary
//    responsive page, so zooming it is all it takes to read it from a sofa;
//  - the script below is a D-pad for it: an arrow moves the focus to the
//    nearest thing in that direction that can be opened or played — links,
//    buttons, and the song rows and cards the site only ever expected a
//    pointer on — and Enter opens or plays it. Chromium has spatial
//    navigation of its own (settings.spatialNavigationEnabled); it stops on
//    every scrolling container and wrapper on the way, which on this site is
//    two or three presses for each thing worth stopping on;
//  - Back goes back inside the site, and leaves it from its front page.
//
// The shell talks to it exactly as it talks to YouTubeView: the same signals
// and the same verbs (Main.qml), plus next and previous, which a queue has
// and a single video does not.
//
// Its sign-in is its own. The TV interface signs in with a code and keeps the
// result to itself; this site wants an ordinary Google sign-in, and needs a
// desktop browser's user agent to be served at all — hence a second profile.
WebEngineView {
    id: view

    property url address: "https://music.youtube.com/"

    signal leftEdgeReached
    signal routeChanged(string route)
    signal playbackStarted
    signal exitRequested
    // As YouTubeView's, with `art` (the cover's address) added. `watching`
    // here means a track is loaded in the site's player bar.
    signal stateReported(var state)

    function pausePlayback(): void {
        view.runJavaScript("document.querySelectorAll('video').forEach(function (v) {"
                           + " if (!v.paused) v.pause(); });");
    }
    function resumePlayback(): void {
        view.runJavaScript("window.__strmqtVideo && (function (v) { if (v && v.paused && !v.ended)"
                           + " v.play(); })(window.__strmqtVideo());");
    }
    function playPause(): void {
        view.runJavaScript("window.__strmqtVideo && (function (v) { if (v)"
                           + " (v.paused ? v.play() : v.pause()); })(window.__strmqtVideo());");
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
    // The site's own buttons: its queue is its own business.
    function next(): void {
        view.runJavaScript("(function (b) { if (b) b.click(); })"
                           + "(document.querySelector('ytmusic-player-bar .next-button'));");
    }
    function previous(): void {
        view.runJavaScript("(function (b) { if (b) b.click(); })"
                           + "(document.querySelector('ytmusic-player-bar .previous-button'));");
    }
    // There is no "nothing loaded" to go back to in this site's player, so
    // stop is a pause; the shell remembers that it was asked for.
    function stopPlayback(): void {
        view.pausePlayback();
    }

    url: view.address
    onAddressChanged: view.url = view.address
    backgroundColor: "#030303"
    // 720 lines of the site's own layout, whatever the window: the size the
    // TV interface settles on, and the rest of this app is drawn to match.
    readonly property real wantedZoom: Math.max(1, Math.min(3, view.height / 720))
    zoomFactor: view.wantedZoom

    // A desktop browser's user agent: the site is not served to anything
    // else. Signing in is not done here at all — Google refuses a sign-in
    // typed into an embedded browser, whatever it calls itself (two disguises
    // were tried and are gone) — but copied in from Firefox (YouTubeSignIn.h).
    profile: WebEngineProfile {
        storageName: "youtube-music"
        offTheRecord: false
        persistentCookiesPolicy: WebEngineProfile.ForcePersistentCookies
        httpUserAgent: "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko)"
                       + " Chrome/130.0.0.0 Safari/537.36"
    }

    // See YouTubeView.bridgeWord.
    readonly property string bridgeWord: "strmqt:" + Math.random().toString(36).slice(2)
                                         + Math.random().toString(36).slice(2) + ":"

    settings.playbackRequiresUserGesture: false
    settings.fullScreenSupportEnabled: false
    settings.javascriptCanOpenWindows: false
    settings.focusOnNavigationEnabled: false

    // The D-pad, Back, and what is playing. Runs in the page.
    readonly property string bridge: `(function () {
 if (window.__strmqt) return; window.__strmqt = true;
 if (location.hostname !== 'music.youtube.com') return;
 function say(m) { console.log('${view.bridgeWord}' + m); }

 // What the focus stops on. A row or a card is one stop, not one for
 // each link inside it.
 var BLOCKS = 'ytmusic-responsive-list-item-renderer, ytmusic-two-row-item-renderer,'
  + ' ytmusic-player-queue-item, ytmusic-multi-row-list-item-renderer,'
  + ' ytmusic-navigation-button-renderer, ytmusic-guide-entry-renderer';
 var ROWS = 'ytmusic-responsive-list-item-renderer, ytmusic-player-queue-item,'
  + ' ytmusic-multi-row-list-item-renderer, ytmusic-guide-entry-renderer';
 var STOPS = BLOCKS + ', a[href], button, input, [role="button"], [role="tab"],'
  + ' tp-yt-paper-item, tp-yt-paper-icon-button, tp-yt-paper-tab';

 // The cursor is drawn over the page, not on the thing it marks: an
 // outline on a card in a carousel is cut off by the carousel's edges, and
 // one on a row sits under the row's neighbours. A frame of its own, fixed
 // to the window and above everything, follows the focused thing around.
 var paint = document.createElement('style');
 paint.textContent = '[data-strmqt-focus], [data-strmqt-focus] :focus { outline: none !important; }'
  + ' #strmqt-cursor { position: fixed; z-index: 2147483647; pointer-events: none;'
  + ' box-sizing: border-box; border: 3px solid #fff; border-radius: 10px;'
  + ' box-shadow: 0 0 0 2px rgba(0, 0, 0, 0.55), inset 0 0 0 1px rgba(0, 0, 0, 0.35);'
  + ' display: none; }';
 document.documentElement.appendChild(paint);
 var cursor = document.createElement('div');
 cursor.id = 'strmqt-cursor';
 document.documentElement.appendChild(cursor);
 var cursorAt = '';
 function trace() {
  var el = marked && marked.isConnected ? marked : null, at = '';
  if (el) {
   var r = el.getBoundingClientRect(), pad = el.matches(ROWS) ? 0 : 5;
   // Inside the window, whatever the thing itself runs off.
   var left = Math.max(2, r.left - pad), top = Math.max(2, r.top - pad);
   var right = Math.min(innerWidth - 2, r.right + pad), bottom = Math.min(innerHeight - 2, r.bottom + pad);
   if (r.width > 0 && r.height > 0 && right - left > 8 && bottom - top > 8)
    at = Math.round(left) + '|' + Math.round(top) + '|' + Math.round(right - left) + '|' + Math.round(bottom - top);
  }
  if (at !== cursorAt) {
   cursorAt = at;
   if (!at) cursor.style.display = 'none';
   else {
    var p = at.split('|');
    cursor.style.left = p[0] + 'px'; cursor.style.top = p[1] + 'px';
    cursor.style.width = p[2] + 'px'; cursor.style.height = p[3] + 'px';
    cursor.style.display = 'block';
   }
  }
  requestAnimationFrame(trace);
 }
 requestAnimationFrame(trace);

 function typing(el) {
  return !!el && (el.tagName === 'INPUT' || el.tagName === 'TEXTAREA' || el.isContentEditable);
 }
 function overlay() {
  var open = document.querySelectorAll('tp-yt-iron-dropdown, tp-yt-paper-dialog');
  for (var i = open.length - 1; i >= 0; --i)
   if (open[i].getAttribute('aria-hidden') !== 'true'
       && open[i].getBoundingClientRect().width > 0) return open[i];
  return null;
 }
 function shown(el, r) {
  if (r.width < 4 || r.height < 4) return false;
  if (el.disabled || el.getAttribute('aria-disabled') === 'true') return false;
  var style = getComputedStyle(el);
  return style.visibility !== 'hidden' && style.display !== 'none' && style.opacity !== '0';
 }
 function stops() {
  var scope = overlay() || document, all = scope.querySelectorAll(STOPS), out = [];
  for (var i = 0; i < all.length; ++i) {
   var el = all[i];
   var block = el.parentElement && el.parentElement.closest(BLOCKS);
   if (block && (scope === document || scope.contains(block))) continue;
   var r = el.getBoundingClientRect();
   if (shown(el, r)) out.push({ el: el, r: r });
  }
  return out;
 }
 var marked = null;
 function mark(el) {
  if (marked && marked !== el) marked.removeAttribute('data-strmqt-focus');
  marked = el;
  if (el) el.setAttribute('data-strmqt-focus', '');
 }
 function land(el) {
  if (el.tabIndex < 0 && !el.hasAttribute('tabindex')) el.setAttribute('tabindex', '-1');
  el.focus({ preventScroll: true });
  mark(el);
  el.scrollIntoView({ block: 'nearest', inline: 'nearest' });
  var r = el.getBoundingClientRect();
  // Clear of the site's fixed header and player bar.
  if (r.top < 90) window.scrollBy(0, r.top - 110);
  else if (r.bottom > innerHeight - 90) window.scrollBy(0, r.bottom - innerHeight + 110);
 }
 function current() {
  var a = document.activeElement;
  if (!a || a === document.body || a === document.documentElement) return null;
  if (marked && marked.isConnected && (marked === a || marked.contains(a))) return marked;
  return a.closest(BLOCKS) || a;
 }
 // dx, dy: one of them is ±1. True when the focus moved.
 function move(dx, dy) {
  var all = stops(), here = current(), best = null, bestScore = Infinity;
  if (!here || !here.isConnected) {
   // Nothing focused: the first thing in what is on screen.
   var ox = innerWidth * 0.22, oy = innerHeight * 0.2;
   for (var k = 0; k < all.length; ++k) {
    var q = all[k].r;
    if (q.bottom < 0 || q.top > innerHeight || q.right < 0 || q.left > innerWidth) continue;
    var d = Math.abs(q.left - ox) + Math.abs(q.top - oy) * 2;
    if (d < bestScore) { bestScore = d; best = all[k].el; }
   }
   if (best) land(best);
   return !!best;
  }
  var h = here.getBoundingClientRect();
  var hx = h.left + h.width / 2, hy = h.top + h.height / 2;
  for (var loose = 0; loose < (dx !== 0 ? 2 : 1) && !best; ++loose)
  for (var i = 0; i < all.length; ++i) {
   var c = all[i];
   if (c.el === here || here.contains(c.el) || c.el.contains(here)) continue;
   var r = c.r, cx = r.left + r.width / 2, cy = r.top + r.height / 2, along, across;
   if (dx !== 0) {
    // Sideways stays in the row — something that overlaps it in height —
    // while the row has anything that way; past its end, whatever is
    // nearest (the site's menu, from a list that starts lower down).
    var inRow = Math.min(r.bottom, h.bottom) - Math.max(r.top, h.top)
              >= Math.min(r.height, h.height) * 0.35;
    if (inRow !== !loose) continue;
    if (loose && (r.bottom < 0 || r.top > innerHeight)) continue;
    along = dx > 0 ? r.left - h.right : h.left - r.right;
    if ((dx > 0 ? cx <= hx : cx >= hx) || along < -8) continue;
    across = Math.abs(cy - hy);
   } else {
    // Up and down stay among what the window shows across its width.
    if (r.right < 0 || r.left > innerWidth) continue;
    along = dy > 0 ? r.top - h.bottom : h.top - r.bottom;
    if ((dy > 0 ? cy <= hy : cy >= hy) || along < -8) continue;
    var overlap = Math.min(r.right, h.right) - Math.max(r.left, h.left);
    across = overlap > 0 ? 0 : Math.min(Math.abs(r.left - h.right), Math.abs(h.left - r.right)) + 40;
    across += Math.abs(r.left - h.left) * 0.05;
   }
   var score = Math.max(0, along) + across * 3;
   if (score < bestScore) { bestScore = score; best = c.el; }
  }
  if (best) land(best);
  return !!best;
 }
 function activate(el) {
  var target = el.querySelector('ytmusic-play-button-renderer')
            || el.querySelector('a[href], tp-yt-paper-item, button') || el;
  target.click();
 }

 document.addEventListener('focusin', function (e) {
  if (marked && e.target !== marked && !marked.contains(e.target)) mark(null);
 }, true);

 document.addEventListener('keydown', function (e) {
  var a = document.activeElement, code = e.keyCode;
  if (e.ctrlKey || e.altKey || e.metaKey) return;
  if (code === 13) {
   var here = current();
   if (here && here.matches(BLOCKS) && !typing(a)) {
    activate(here); e.preventDefault(); e.stopPropagation();
   }
   return;
  }
  // Back: out of a menu first (the site's own doing), out of the search
  // box, then back a page, then out of the site.
  if (code === 27 && !e.repeat) {
   if (overlay()) return;
   e.preventDefault(); e.stopPropagation();
   if (typing(a)) { a.blur(); return; }
   if (location.pathname !== '/' && history.length > 1) history.back();
   else say('exit');
   return;
  }
  if (code < 37 || code > 40) return;
  // In the search box the arrows are the caret's and the suggestions'
  // while there is something typed.
  if (typing(a) && a.value) return;
  e.preventDefault(); e.stopPropagation();
  var moved = move(code === 39 ? 1 : code === 37 ? -1 : 0, code === 40 ? 1 : code === 38 ? -1 : 0);
  if (!moved && code === 37 && !e.repeat && !overlay()) say('edge-left');
 }, true);

 document.addEventListener('playing', function (e) {
  if (e.target && !e.target.muted) say('playing');
 }, true);
 window.__strmqtVideo = function () {
  var v = document.querySelector('video');
  return v && v.currentSrc && v.readyState > 0 ? v : null;
 };
 var told = '';
 function report() {
  var v = window.__strmqtVideo(), meta = null, art = '';
  try { meta = navigator.mediaSession && navigator.mediaSession.metadata; } catch (e) {}
  if (meta && meta.artwork && meta.artwork.length)
   art = meta.artwork[meta.artwork.length - 1].src || '';
  var state = JSON.stringify({
   watching: !!v && !!meta,
   playing: !!v && !v.paused && !v.ended,
   title: (meta && meta.title) || '',
   artist: (meta && meta.artist) || '',
   art: /^https:\\/\\/[a-z0-9.-]+\\.(googleusercontent|ytimg|ggpht)\\.com\\//i.test(art) ? art : '',
   position: v ? Math.floor(v.currentTime) : 0,
   duration: v && isFinite(v.duration) ? Math.floor(v.duration) : 0,
   volume: v ? v.volume : 1,
   muted: v ? v.muted : false
  });
  if (state !== told) { told = state; say('state:' + state); }
 }
 ['playing', 'pause', 'ended', 'volumechange', 'durationchange', 'seeked',
  'loadedmetadata', 'emptied'].forEach(function (name) {
  document.addEventListener(name, report, true);
 });
 setInterval(report, 1000);
})();`

    userScripts.collection: [
        // YouTubeView.qml, "strmqt-shown": the page is never told it is hidden.
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
            "sourceCode": view.bridge
        }
    ]

    onLoadingChanged: info => {
        // A move to another site (signing in) can come back at 100%.
        if (info.status === WebEngineView.LoadSucceededStatus && view.zoomFactor !== view.wantedZoom)
            view.zoomFactor = view.wantedZoom;
    }

    onJavaScriptConsoleMessage: (level, message, line, source) => {
        if (!message.startsWith(view.bridgeWord))
            return;
        const said = message.substring(view.bridgeWord.length);
        if (said === "edge-left")
            view.leftEdgeReached();
        else if (said === "playing")
            view.playbackStarted();
        else if (said === "exit")
            view.exitRequested();
        else if (said.startsWith("state:")) {
            try {
                const state = JSON.parse(said.substring(6));
                if (state !== null && typeof state === "object" && !Array.isArray(state))
                    view.stateReported(state);
            } catch (error) {
            }
        }
    }

    onWindowCloseRequested: view.exitRequested()
    onNewWindowRequested: request => {}
    onRenderProcessTerminated: (status, code) => {
        console.warn("YouTube Music view: render process ended (" + status + "), reloading");
        view.reload();
    }
}
