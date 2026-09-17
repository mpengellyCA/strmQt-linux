// Driving the TV's own interface: a touchpad (swipe to move, tap to select),
// a D-pad for people who prefer buttons, and the keys the desktop listens for.

import { post } from './api.js';
import { store, subscribe, playback } from './store.js';
import { togglePause, seekRelative, command } from './player.js';
import { h, icon, setIcon, haptic, segmented, toast } from './ui.js';

const CONTEXT_TEXT = {
  login: 'The TV is on the sign-in screen',
  browse: 'The TV is browsing the library',
  music: 'The TV is in music',
  player: 'The TV is playing',
  overlay: 'A menu is open on the TV',
};

let sending = Promise.resolve();

// Keys go out in order: a quick swipe-swipe-tap must land as down, down, OK.
function sendKey(key) {
  sending = sending
    .then(() => post('/api/navigate', { key }))
    .catch((error) => {
      if (error.status !== 401) toast(error.message, 'error');
    });
  return sending;
}

function sendDestination(destination) {
  haptic();
  return command.navigate({ destination });
}

function readMode() {
  try {
    return localStorage.getItem('strmqt_remote_mode') || 'pad';
  } catch {
    return 'pad';
  }
}

function writeMode(mode) {
  try {
    localStorage.setItem('strmqt_remote_mode', mode);
  } catch {
    // Not remembered.
  }
}

const STEP_PX = 44;
const LONG_PRESS_MS = 520;

function touchpad() {
  const dot = h('span', { class: 'pad-dot' });
  const arrows = {
    up: h('span', { class: 'pad-arrow up' }, icon('up')),
    down: h('span', { class: 'pad-arrow down' }, icon('down')),
    left: h('span', { class: 'pad-arrow left' }, icon('left')),
    right: h('span', { class: 'pad-arrow right' }, icon('right')),
  };
  const hint = h('p', { class: 'pad-hint', text: 'Swipe to move. Tap to select. Hold for options.' });
  const pad = h('div', {
    class: 'pad', role: 'application', tabindex: '0',
    'aria-label': 'Touchpad. Swipe to move, tap to select, hold for options. Arrow keys and Enter also work.',
  }, Object.values(arrows), dot, hint);

  let start = null;
  let longTimer = 0;

  const flash = (direction) => {
    const arrow = arrows[direction];
    arrow.classList.remove('flash');
    void arrow.offsetWidth;
    arrow.classList.add('flash');
  };

  const move = (direction) => {
    haptic(6);
    flash(direction);
    sendKey(direction);
  };

  pad.addEventListener('pointerdown', (event) => {
    pad.setPointerCapture(event.pointerId);
    const rect = pad.getBoundingClientRect();
    start = { x: event.clientX, y: event.clientY, t: performance.now(), moved: false, long: false };
    dot.style.left = `${event.clientX - rect.left}px`;
    dot.style.top = `${event.clientY - rect.top}px`;
    dot.classList.add('on');
    hint.classList.add('gone');
    clearTimeout(longTimer);
    longTimer = setTimeout(() => {
      if (!start || start.moved) return;
      start.long = true;
      haptic(20);
      pad.classList.add('held');
      sendKey('menu');
    }, LONG_PRESS_MS);
  });

  pad.addEventListener('pointermove', (event) => {
    if (!start) return;
    const rect = pad.getBoundingClientRect();
    dot.style.left = `${event.clientX - rect.left}px`;
    dot.style.top = `${event.clientY - rect.top}px`;
    const dx = event.clientX - start.x;
    const dy = event.clientY - start.y;
    // Each STEP_PX of travel along the dominant axis is one step; a long drag
    // walks down a list without lifting the finger.
    if (Math.max(Math.abs(dx), Math.abs(dy)) < STEP_PX) return;
    start.moved = true;
    clearTimeout(longTimer);
    if (Math.abs(dx) > Math.abs(dy)) {
      move(dx > 0 ? 'right' : 'left');
      start.x += Math.sign(dx) * STEP_PX;
      start.y = event.clientY;
    } else {
      move(dy > 0 ? 'down' : 'up');
      start.y += Math.sign(dy) * STEP_PX;
      start.x = event.clientX;
    }
  });

  const end = (cancelled) => {
    clearTimeout(longTimer);
    dot.classList.remove('on');
    pad.classList.remove('held');
    if (!start) return;
    const wasTap = !start.moved && !start.long && performance.now() - start.t < LONG_PRESS_MS;
    start = null;
    if (wasTap && !cancelled) {
      haptic(10);
      pad.classList.remove('tapped');
      void pad.offsetWidth;
      pad.classList.add('tapped');
      sendKey('select');
    }
  };
  pad.addEventListener('pointerup', () => end(false));
  pad.addEventListener('pointercancel', () => end(true));
  pad.addEventListener('keydown', keyboardHandler);
  return pad;
}

function dpad() {
  let repeatTimer = 0;
  let repeatDelay = 0;
  const stop = () => {
    clearTimeout(repeatDelay);
    clearInterval(repeatTimer);
  };
  const hold = (key) => (event) => {
    event.preventDefault();
    event.currentTarget.setPointerCapture?.(event.pointerId);
    haptic(8);
    sendKey(key);
    stop();
    repeatDelay = setTimeout(() => {
      repeatTimer = setInterval(() => {
        haptic(4);
        sendKey(key);
      }, 140);
    }, 420);
  };
  const arm = (key, label, iconName, className) => {
    const b = h('button', { class: `dpad-btn ${className}`, type: 'button', 'aria-label': label }, icon(iconName));
    b.addEventListener('pointerdown', hold(key));
    b.addEventListener('pointerup', stop);
    b.addEventListener('pointercancel', stop);
    b.addEventListener('pointerleave', stop);
    b.addEventListener('keydown', (event) => {
      if (event.key === 'Enter' || event.key === ' ') {
        event.preventDefault();
        sendKey(key);
      }
    });
    return b;
  };
  const ok = h('button', { class: 'dpad-ok', type: 'button', text: 'OK', onClick: () => { haptic(10); sendKey('select'); } });
  const node = h('div', { class: 'dpad', role: 'group', 'aria-label': 'Direction pad' },
    arm('up', 'Up', 'up', 'up'),
    arm('left', 'Left', 'left', 'left'),
    ok,
    arm('right', 'Right', 'right', 'right'),
    arm('down', 'Down', 'down', 'down'));
  node.addEventListener('keydown', keyboardHandler);
  return node;
}

function keyboardHandler(event) {
  const keys = { ArrowUp: 'up', ArrowDown: 'down', ArrowLeft: 'left', ArrowRight: 'right', Enter: 'select', Backspace: 'back', Escape: 'back' };
  const key = keys[event.key];
  if (!key || event.target.closest('.dpad-btn, .dpad-ok')) return;
  event.preventDefault();
  sendKey(key);
}

function keyButton(label, iconName, onPress) {
  return h('button', {
    class: 'key', type: 'button',
    onClick: () => {
      haptic();
      onPress();
    },
  }, icon(iconName), h('span', { text: label }));
}

export function remoteView(node, params, ctx) {
  ctx.setTitle('Remote');
  const contextLine = h('p', { class: 'remote-context' });
  const surface = h('div', { class: 'remote-surface' });
  let mode = readMode();

  const renderSurface = () => {
    surface.replaceChildren(mode === 'dpad' ? dpad() : touchpad());
  };
  const modeSwitch = segmented([{ value: 'pad', label: 'Touchpad' }, { value: 'dpad', label: 'Buttons' }], {
    current: mode,
    label: 'Remote style',
    onPick: (value) => {
      mode = value;
      writeMode(value);
      renderSurface();
    },
  });

  const playIcon = icon('pause');
  const playKey = h('button', { class: 'key key-play', type: 'button', 'aria-label': 'Play or pause', onClick: togglePause }, playIcon);
  const muteIcon = icon('volume');
  const transport = h('div', { class: 'remote-transport' },
    h('button', { class: 'key', type: 'button', 'aria-label': 'Volume down', onClick: () => { haptic(); command.volume('down'); } }, h('span', { class: 'key-glyph mono', text: '−' })),
    h('button', { class: 'key', type: 'button', 'aria-label': 'Back 10 seconds', onClick: () => seekRelative(-10000) }, icon('back10')),
    playKey,
    h('button', { class: 'key', type: 'button', 'aria-label': 'Forward 30 seconds', onClick: () => seekRelative(30000) }, icon('fwd30')),
    h('button', { class: 'key', type: 'button', 'aria-label': 'Volume up', onClick: () => { haptic(); command.volume('up'); } }, h('span', { class: 'key-glyph mono', text: '+' })));
  const muteKey = h('button', { class: 'key', type: 'button', 'aria-label': 'Mute', onClick: () => { haptic(); command.volume('toggleMute'); } }, muteIcon, h('span', { text: 'Mute' }));

  const primaryKeys = h('div', { class: 'keys keys-primary' },
    keyButton('Back', 'back', () => sendKey('back')),
    keyButton('Home', 'home', () => sendDestination('home')),
    keyButton('Options', 'more', () => sendKey('menu')));

  const moreKeys = h('div', { class: 'keys' },
    keyButton('Menu', 'menu', () => sendKey('toggleMenu')),
    keyButton('Search', 'search', () => sendDestination('search')),
    keyButton('Settings', 'settings', () => sendDestination('settings')),
    keyButton('On-screen', 'osd', () => sendDestination('osd')),
    keyButton('Page up', 'page-up', () => sendKey('pageUp')),
    keyButton('Page down', 'page-down', () => sendKey('pageDown')),
    keyButton('Prev tab', 'tab-prev', () => sendKey('prevTab')),
    keyButton('Next tab', 'tab-next', () => sendKey('nextTab')),
    keyButton('Full screen', 'fullscreen', () => sendKey('fullscreen')),
    muteKey);

  const render = () => {
    const context = store.status?.app?.context || '';
    const p = playback();
    contextLine.textContent = store.connected ? (CONTEXT_TEXT[context] || 'Connected to the TV') : 'Waiting for the TV';
    transport.hidden = !p.active;
    setIcon(playIcon, p.paused ? 'play' : 'pause');
    setIcon(muteIcon, p.muted ? 'mute' : 'volume');
    muteKey.querySelector('span').textContent = p.muted ? 'Unmute' : 'Mute';
  };

  node.append(
    h('div', { class: 'remote' },
      h('div', { class: 'remote-top' }, contextLine, modeSwitch.node),
      surface,
      primaryKeys,
      transport,
      h('details', { class: 'more-keys' },
        h('summary', null, h('span', { text: 'More buttons' }), icon('down')),
        moreKeys)));
  renderSurface();
  render();
  const unsubscribe = subscribe((kind) => {
    if (kind === 'status' || kind === 'connection') render();
  });
  return { dispose: unsubscribe };
}
