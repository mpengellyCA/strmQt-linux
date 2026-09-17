// Live state pushed by the TV. Views subscribe and re-read; nothing here talks
// to the network.

const listeners = new Set();

export const store = {
  status: null,
  queue: [],
  connected: false,
  receivedAt: 0,
};

export function subscribe(listener) {
  listeners.add(listener);
  return () => listeners.delete(listener);
}

function notify(kind) {
  for (const listener of listeners) listener(kind);
}

export function setStatus(status) {
  store.status = status;
  store.receivedAt = performance.now();
  notify('status');
}

export function setQueue(queue) {
  store.queue = Array.isArray(queue) ? queue : [];
  notify('queue');
}

export function setConnected(connected) {
  if (store.connected === connected) return;
  store.connected = connected;
  notify('connection');
}

export function playback() {
  return store.status?.playback || { active: false };
}

// Position runs on between pushes; the TV corrects it every few seconds.
export function livePosition() {
  const p = playback();
  if (!p.active) return 0;
  let position = p.positionMs || 0;
  if (!p.paused && !p.buffering && !p.busy) {
    position += (performance.now() - store.receivedAt) * (p.speed || 1);
  }
  return p.durationMs ? Math.min(position, p.durationMs) : position;
}

// Optimistic local change, overwritten by the next push.
export function patchPlayback(patch) {
  if (!store.status?.playback) return;
  if ('positionMs' in patch) store.receivedAt = performance.now();
  Object.assign(store.status.playback, patch);
  notify('status');
}
