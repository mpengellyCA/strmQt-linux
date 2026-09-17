// The shell: tabs, a history-backed page stack, the PIN prompt and the live
// connection to the TV.

import { post, setToken, onUnauthorized, connectEvents } from './api.js';
import { store, subscribe, setStatus, setQueue, setConnected } from './store.js';
import { initPlayer } from './player.js';
import { homeView, librariesView, libraryView, listView, itemView, searchView } from './browse.js';
import { remoteView } from './remote.js';
import { h, syncOverlays, unwindOverlays, overlayDepth } from './ui.js';

const ROUTES = {
  home: { view: homeView, tab: 'home', root: true },
  libraries: { view: librariesView, tab: 'libraries', root: true },
  search: { view: searchView, tab: 'search', root: true },
  remote: { view: remoteView, tab: 'remote', root: true },
  library: { view: libraryView, tab: 'libraries' },
  item: { view: itemView },
  list: { view: listView },
};

const viewHost = document.getElementById('view');
const topTitle = document.getElementById('topbar-title');
const topBack = document.getElementById('topbar-back');
const topbar = document.getElementById('topbar');
const tabs = [...document.querySelectorAll('.tab')];

const pages = new Map(); // entry id → { node, scroll, view, title, used }
const MAX_PAGES = 14;
let current = null;
let sequence = Date.now();
let player = null;

function pageFor(entry) {
  let page = pages.get(entry.id);
  if (page) return page;
  const node = h('div', { class: `page page-${entry.name}` });
  page = { node, scroll: 0, view: null, title: '', used: 0 };
  pages.set(entry.id, page);
  const ctx = {
    go: (name, params) => navigate(name, params),
    openItem: (item) => navigate('item', { id: item.id, name: item.name }),
    openPlayer: () => player?.open(),
    setTitle: (title) => {
      page.title = title;
      if (current?.id === entry.id) topTitle.textContent = title;
    },
  };
  const route = ROUTES[entry.name] || ROUTES.home;
  page.view = route.view(node, entry.params || {}, ctx) || null;
  return page;
}

function prune() {
  if (pages.size <= MAX_PAGES) return;
  const victims = [...pages.entries()]
    .filter(([id]) => id !== current?.id)
    .sort((a, b) => a[1].used - b[1].used)
    .slice(0, pages.size - MAX_PAGES);
  for (const [id, page] of victims) {
    page.view?.dispose?.();
    pages.delete(id);
  }
}

function show(entry) {
  if (current) {
    const previous = pages.get(current.id);
    if (previous) {
      previous.scroll = window.scrollY;
      previous.view?.onHide?.();
    }
  }
  current = entry;
  const page = pageFor(entry);
  page.used = performance.now();
  viewHost.replaceChildren(page.node);

  const route = ROUTES[entry.name] || ROUTES.home;
  const tab = entry.tab || route.tab || 'home';
  for (const button of tabs) {
    const on = button.dataset.tab === tab;
    button.classList.toggle('on', on);
    if (on) button.setAttribute('aria-current', 'page');
    else button.removeAttribute('aria-current');
  }
  topTitle.textContent = page.title;
  topBack.hidden = !(entry.depth > 0);
  document.body.dataset.route = entry.name;
  window.scrollTo(0, page.scroll);
  window.dispatchEvent(new Event('scroll'));
  page.view?.onShow?.();
  prune();
}

async function navigate(name, params = {}, { replace = false } = {}) {
  if (overlayDepth()) await unwindOverlays();
  const route = ROUTES[name] || ROUTES.home;
  const tab = route.tab || current?.tab || 'home';
  const depth = route.root ? 0 : (current?.depth || 0) + 1;
  const entry = { id: ++sequence, name, params, tab, depth };
  const state = { entry, overlays: [] };
  if (replace) history.replaceState(state, '');
  else history.pushState(state, '');
  show(entry);
}

window.addEventListener('popstate', (event) => {
  const state = event.state;
  syncOverlays(state);
  if (!state?.entry) {
    navigate('home', {}, { replace: true });
    return;
  }
  if (state.entry.id !== current?.id) show(state.entry);
});

for (const button of tabs) {
  button.addEventListener('click', () => {
    const tab = button.dataset.tab;
    if (current?.name === tab && !overlayDepth()) {
      window.scrollTo({ top: 0, behavior: 'smooth' });
      const page = pages.get(current.id);
      page?.view?.refresh?.();
      return;
    }
    navigate(tab);
  });
}

topBack.addEventListener('click', () => history.back());

// The title bar gains a hairline once content scrolls under it.
window.addEventListener('scroll', () => {
  topbar.classList.toggle('scrolled', window.scrollY > 4);
  // Pages with a large heading hand their title to the bar once it has gone.
  topbar.classList.toggle('far', window.scrollY > (document.body.dataset.route === 'item' ? 220 : 64));
}, { passive: true });

// ── Connection ─────────────────────────────────────────────────────────────

const linkState = document.getElementById('link-state');
const linkText = document.getElementById('link-text');

function paintLink() {
  const locked = !document.getElementById('pin').hidden;
  linkState.dataset.state = locked ? 'locked' : store.connected ? 'live' : 'lost';
  linkText.textContent = locked ? 'Locked' : store.connected ? 'Live' : 'Reconnecting';
}

let appliedAccent = '';
function applyAccent() {
  const accent = store.status?.app?.accent;
  if (!accent || accent === appliedAccent || !/^#[0-9a-f]{6}$/i.test(accent)) return;
  appliedAccent = accent;
  document.documentElement.style.setProperty('--lamp', accent);
}

subscribe((kind) => {
  if (kind === 'connection') paintLink();
  if (kind === 'status') applyAccent();
});

const events = connectEvents({
  status: setStatus,
  queue: setQueue,
  connection: (connected) => {
    setConnected(connected);
    if (connected) hidePin();
  },
});

document.addEventListener('visibilitychange', () => {
  if (!document.hidden && !events.alive) events.reconnect();
});
window.addEventListener('online', () => events.reconnect());

// ── PIN ────────────────────────────────────────────────────────────────────

const pin = document.getElementById('pin');
const pinForm = document.getElementById('pin-form');
const pinInput = document.getElementById('pin-input');
const pinError = document.getElementById('pin-error');

function showPin() {
  if (!pin.hidden) return;
  pin.hidden = false;
  pinError.textContent = '';
  paintLink();
  requestAnimationFrame(() => pinInput.focus());
}

function hidePin() {
  if (pin.hidden) return;
  pin.hidden = true;
  paintLink();
}

onUnauthorized(showPin);

pinForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  const value = pinInput.value.trim();
  if (!value) {
    pinError.textContent = 'Type the PIN first.';
    return;
  }
  pinError.textContent = '';
  try {
    const result = await post('/api/auth/pin', { pin: value });
    setToken(result.token);
    pinInput.value = '';
    hidePin();
    events.reconnect();
    // Pages that failed while locked load again with the token.
    for (const page of pages.values()) page.view?.dispose?.();
    pages.clear();
    if (current) show(current);
  } catch (error) {
    pinError.textContent = error.status === 429 ? 'Too many tries. Wait a moment, then try again.'
      : error.status === 403 ? 'That PIN isn’t right. Check the TV.'
      : error.message;
    pinInput.select();
  }
});

// ── Start ──────────────────────────────────────────────────────────────────

player = initPlayer({ openItem: (item) => navigate('item', { id: item.id, name: item.name }) });

const initial = history.state?.entry;
if (initial && ROUTES[initial.name]) {
  sequence = Math.max(sequence, initial.id);
  history.replaceState({ entry: initial, overlays: [] }, '');
  show(initial);
} else {
  navigate('home', {}, { replace: true });
}
paintLink();
