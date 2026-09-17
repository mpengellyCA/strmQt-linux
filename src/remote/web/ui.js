// DOM building blocks. Everything the server sends is set as text or as an
// attribute on a node built here — never parsed as markup.

export function h(tag, props, ...children) {
  const node = document.createElement(tag);
  if (props) {
    for (const [key, value] of Object.entries(props)) {
      if (value === undefined || value === null || value === false) continue;
      if (key === 'class') node.className = value;
      else if (key === 'text') node.textContent = value;
      else if (key === 'style') {
        for (const [prop, v] of Object.entries(value)) {
          if (prop.startsWith('--')) node.style.setProperty(prop, v);
          else node.style[prop] = v;
        }
      } else if (key === 'dataset') Object.assign(node.dataset, value);
      else if (key.startsWith('on') && typeof value === 'function') {
        node.addEventListener(key.slice(2).toLowerCase(), value);
      } else if (value === true) node.setAttribute(key, '');
      else node.setAttribute(key, String(value));
    }
  }
  append(node, children);
  return node;
}

export function append(node, children) {
  for (const child of children.flat(Infinity)) {
    if (child === null || child === undefined || child === false || child === '') continue;
    node.append(child instanceof Node ? child : document.createTextNode(String(child)));
  }
  return node;
}

const SVG = 'http://www.w3.org/2000/svg';

export function icon(name, className = '') {
  const svg = document.createElementNS(SVG, 'svg');
  svg.setAttribute('class', `icon icon-${name} ${className}`.trim());
  svg.setAttribute('aria-hidden', 'true');
  const use = document.createElementNS(SVG, 'use');
  use.setAttribute('href', `#i-${name}`);
  svg.append(use);
  return svg;
}

export function setIcon(svg, name) {
  svg.firstElementChild?.setAttribute('href', `#i-${name}`);
}

export function iconButton(name, label, onClick, className = '') {
  return h('button', { class: `icon-btn ${className}`.trim(), type: 'button', 'aria-label': label, title: label, onClick },
    icon(name));
}

export function button(label, { iconName, kind = 'secondary', onClick, className = '' } = {}) {
  return h('button', { class: `btn btn-${kind} ${className}`.trim(), type: 'button', onClick },
    iconName ? icon(iconName) : null, h('span', { text: label }));
}

export function haptic(ms = 8) {
  try {
    navigator.vibrate?.(ms);
  } catch {
    // Not every browser lets a page vibrate.
  }
}

// ── Formatting ─────────────────────────────────────────────────────────────

export function clock(ms, withHours = false) {
  const total = Math.max(0, Math.floor((ms || 0) / 1000));
  const hours = Math.floor(total / 3600);
  const minutes = Math.floor((total % 3600) / 60);
  const seconds = total % 60;
  const ss = String(seconds).padStart(2, '0');
  if (hours || withHours) return `${hours}:${String(minutes).padStart(2, '0')}:${ss}`;
  return `${minutes}:${ss}`;
}

export function duration(ms) {
  const minutes = Math.round((ms || 0) / 60000);
  if (minutes < 60) return `${minutes} min`;
  const hours = Math.floor(minutes / 60);
  const rest = minutes % 60;
  return rest ? `${hours} h ${rest} min` : `${hours} h`;
}

export function endsAt(remainingMs) {
  const end = new Date(Date.now() + remainingMs);
  return end.toLocaleTimeString([], { hour: 'numeric', minute: '2-digit' });
}

export function bitrate(bps) {
  if (!bps) return '';
  if (bps >= 1e6) return `${(bps / 1e6).toFixed(bps >= 1e7 ? 0 : 1)} Mbps`;
  return `${Math.round(bps / 1e3)} kbps`;
}

export function fileSize(bytes) {
  if (!bytes) return '';
  const gb = bytes / 1024 ** 3;
  return gb >= 1 ? `${gb.toFixed(1)} GB` : `${Math.round(bytes / 1024 ** 2)} MB`;
}

export function count(n, one, many) {
  return `${Number(n).toLocaleString()} ${n === 1 ? one : many}`;
}

export function episodeCode(item) {
  const season = item.parentIndexNumber;
  const episode = item.indexNumber;
  if (season !== undefined && episode !== undefined) return `S${season} E${episode}`;
  if (episode !== undefined) return `E${episode}`;
  return '';
}

// ── Toasts ─────────────────────────────────────────────────────────────────

export function toast(message, kind = 'info') {
  const host = document.getElementById('toasts');
  const node = h('div', { class: `toast toast-${kind}`, role: kind === 'error' ? 'alert' : 'status', text: message });
  host.append(node);
  requestAnimationFrame(() => node.classList.add('in'));
  setTimeout(() => {
    node.classList.remove('in');
    setTimeout(() => node.remove(), 260);
  }, kind === 'error' ? 4800 : 2600);
}

// Runs a command against the TV and says so if it fails. Returns the result,
// or undefined after a reported failure.
export async function run(promise, success) {
  try {
    const result = await promise;
    if (success) toast(success);
    return result;
  } catch (error) {
    if (error.status !== 401) toast(error.message || 'That didn’t work.', 'error');
    return undefined;
  }
}

// ── Overlays and history ───────────────────────────────────────────────────
// The player and every sheet take a history entry, so the phone's own back
// gesture closes the top one instead of leaving the page.

const overlays = [];
let unwindResolve = null;
// history.back() is asynchronous: count the ones already on their way so a
// close followed at once by a navigation doesn't step back twice.
let inFlight = 0;

export function openOverlay(name, close) {
  overlays.push({ name, close });
  const state = history.state || {};
  history.pushState({ ...state, overlays: overlays.map((o) => o.name) }, '');
}

export function closeTopOverlay() {
  if (overlays.length - inFlight <= 0) return;
  inFlight += 1;
  history.back();
}

export function hasOverlay(name) {
  return overlays.some((o) => o.name === name);
}

export function overlayDepth() {
  return overlays.length;
}

// Called from popstate. Closes whatever the new entry does not have.
export function syncOverlays(state) {
  const depth = state?.overlays?.length || 0;
  let closed = 0;
  while (overlays.length > depth) {
    overlays.pop().close();
    closed += 1;
  }
  inFlight = Math.max(0, inFlight - Math.max(closed, 1));
  if (unwindResolve && overlays.length === 0) {
    const resolve = unwindResolve;
    unwindResolve = null;
    resolve();
  }
}

export function unwindOverlays() {
  if (!overlays.length) return Promise.resolve();
  return new Promise((resolve) => {
    unwindResolve = resolve;
    const remaining = overlays.length - inFlight;
    if (remaining > 0) {
      inFlight += remaining;
      history.go(-remaining);
    }
  });
}

// ── Sheets ─────────────────────────────────────────────────────────────────

const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)');

export function openSheet({ title, subtitle, build, tall = false }) {
  const layer = document.getElementById('sheet-layer');
  const body = h('div', { class: 'sheet-body' });
  const heading = h('h2', { class: 'sheet-title', text: title, id: `sheet-${Date.now()}` });
  const panel = h('div', { class: `sheet${tall ? ' sheet-tall' : ''}`, role: 'dialog', 'aria-modal': 'true', 'aria-labelledby': heading.id },
    h('div', { class: 'sheet-grip', 'aria-hidden': 'true' }),
    h('header', { class: 'sheet-head' },
      h('div', { class: 'sheet-heading' }, heading, subtitle ? h('p', { class: 'sheet-sub', text: subtitle }) : null),
      iconButton('close', 'Close', () => closeTopOverlay())),
    body);
  const scrim = h('div', { class: 'sheet-scrim', onClick: () => closeTopOverlay() });
  const wrap = h('div', { class: 'sheet-wrap' }, scrim, panel);
  layer.append(wrap);

  const previousFocus = document.activeElement;
  let cleanup = null;
  const sheet = {
    body,
    setSubtitle(text) {
      let sub = panel.querySelector('.sheet-sub');
      if (!sub) {
        sub = h('p', { class: 'sheet-sub' });
        heading.after(sub);
      }
      sub.textContent = text;
    },
    close: () => closeTopOverlay(),
  };
  cleanup = build(body, sheet) || null;

  requestAnimationFrame(() => {
    wrap.classList.add('open');
    panel.focus({ preventScroll: true });
  });
  panel.tabIndex = -1;

  // Drag the grip or header down to dismiss.
  let startY = null;
  let dragged = 0;
  const head = panel.querySelector('.sheet-head');
  const grip = panel.querySelector('.sheet-grip');
  const onDown = (event) => {
    if (event.target.closest('button')) return;
    startY = event.clientY;
    dragged = 0;
    panel.style.transition = 'none';
    event.currentTarget.setPointerCapture(event.pointerId);
  };
  const onMove = (event) => {
    if (startY === null) return;
    dragged = Math.max(0, event.clientY - startY);
    panel.style.transform = `translateY(${dragged}px)`;
  };
  const onUp = () => {
    if (startY === null) return;
    startY = null;
    panel.style.transition = '';
    panel.style.transform = '';
    if (dragged > 90) closeTopOverlay();
  };
  for (const target of [grip, head]) {
    target.addEventListener('pointerdown', onDown);
    target.addEventListener('pointermove', onMove);
    target.addEventListener('pointerup', onUp);
    target.addEventListener('pointercancel', onUp);
  }

  const onKey = (event) => {
    if (event.key === 'Escape') {
      event.preventDefault();
      closeTopOverlay();
    }
  };
  panel.addEventListener('keydown', onKey);

  openOverlay('sheet', () => {
    cleanup?.();
    wrap.classList.remove('open');
    const remove = () => wrap.remove();
    if (reducedMotion.matches) remove();
    else setTimeout(remove, 280);
    if (previousFocus instanceof HTMLElement) previousFocus.focus({ preventScroll: true });
  });
  return sheet;
}

// A list of choices with a check against the current one.
export function choiceList(options, { current, onPick, compare = (a, b) => a === b }) {
  const list = h('div', { class: 'choices', role: 'listbox' });
  for (const option of options) {
    const selected = compare(option.value, current);
    list.append(h('button', {
      class: `choice${selected ? ' selected' : ''}${option.disabled ? ' disabled' : ''}`,
      type: 'button',
      role: 'option',
      'aria-selected': selected ? 'true' : 'false',
      disabled: option.disabled,
      onClick: () => {
        haptic();
        onPick(option.value, option);
      },
    },
    h('span', { class: 'choice-text' },
      h('span', { class: 'choice-label', text: option.label }),
      option.detail ? h('span', { class: `choice-detail${option.mono ? ' mono' : ''}`, text: option.detail }) : null),
    selected ? icon('check', 'choice-check') : h('span', { class: 'choice-check-space' })));
  }
  return list;
}

// −/value/+ for delays and sizes.
export function stepper({ label, value, step, min, max, format, onChange, resetTo }) {
  const readout = h('span', { class: 'stepper-value mono', text: format(value) });
  let current = value;
  const change = (next) => {
    current = Math.min(max, Math.max(min, Math.round(next * 1000) / 1000));
    readout.textContent = format(current);
    haptic();
    onChange(current);
  };
  const row = h('div', { class: 'stepper' },
    h('span', { class: 'stepper-label', text: label }),
    h('div', { class: 'stepper-controls' },
      iconButton('left', `Decrease ${label.toLowerCase()}`, () => change(current - step)),
      readout,
      iconButton('right', `Increase ${label.toLowerCase()}`, () => change(current + step)),
      resetTo !== undefined
        ? h('button', { class: 'text-btn', type: 'button', text: 'Reset', onClick: () => change(resetTo) })
        : null));
  return {
    node: row,
    set(next) {
      current = next;
      readout.textContent = format(next);
    },
  };
}

export function segmented(options, { current, onPick, label }) {
  const group = h('div', { class: 'segmented', role: 'radiogroup', 'aria-label': label });
  const render = (value) => {
    group.replaceChildren(...options.map((option) => h('button', {
      class: `segment${option.value === value ? ' selected' : ''}`,
      type: 'button',
      role: 'radio',
      'aria-checked': option.value === value ? 'true' : 'false',
      text: option.label,
      onClick: () => {
        haptic();
        render(option.value);
        onPick(option.value);
      },
    })));
  };
  render(current);
  return { node: group, set: render };
}

export function sectionHeading(text, action) {
  return h('div', { class: 'section-head' }, h('h2', { class: 'section-title', text }), action || null);
}

export function emptyState(title, detail, action) {
  return h('div', { class: 'empty' },
    h('p', { class: 'empty-title', text: title }),
    detail ? h('p', { class: 'empty-detail', text: detail }) : null,
    action || null);
}

export function errorState(error, retry) {
  return emptyState(
    error?.status === 503 ? 'StrmQt isn’t signed in' : 'This didn’t load',
    error?.status === 503 ? 'Sign in to a server on the TV, then try again.' : error?.message,
    retry ? button('Try again', { onClick: retry }) : null);
}

export function debounce(fn, wait) {
  let timer = 0;
  return (...args) => {
    clearTimeout(timer);
    timer = setTimeout(() => fn(...args), wait);
  };
}
