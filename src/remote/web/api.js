// Talking to StrmQt: JSON over the same origin, a PIN token when the TV asks
// for one, and a Server-Sent Events stream for live state.

const TOKEN_KEY = 'strmqt_token';

let token = '';
try {
  token = localStorage.getItem(TOKEN_KEY) || '';
} catch {
  token = '';
}

const unauthorizedListeners = new Set();

export class ApiError extends Error {
  constructor(status, message) {
    super(message);
    this.status = status;
  }
}

export function onUnauthorized(listener) {
  unauthorizedListeners.add(listener);
}

function announceUnauthorized() {
  for (const listener of unauthorizedListeners) listener();
}

export function setToken(value) {
  token = value;
  try {
    localStorage.setItem(TOKEN_KEY, value);
  } catch {
    // Private mode: the token lasts as long as the page.
  }
}

export function withQuery(path, params = {}) {
  const query = new URLSearchParams();
  for (const [key, value] of Object.entries(params)) {
    if (value === undefined || value === null || value === '') continue;
    if (Array.isArray(value)) {
      if (value.length) query.set(key, value.join(','));
      continue;
    }
    query.set(key, String(value));
  }
  const text = query.toString();
  return text ? `${path}?${text}` : path;
}

async function request(method, path, body, signal) {
  const headers = {};
  if (token) headers.Authorization = `Bearer ${token}`;
  const init = { method, headers, signal };
  if (body !== undefined) {
    headers['Content-Type'] = 'application/json';
    init.body = JSON.stringify(body);
  }

  let response;
  try {
    response = await fetch(path, init);
  } catch (error) {
    if (error.name === 'AbortError') throw error;
    throw new ApiError(0, 'Can’t reach StrmQt. Check the app is open and on the same network.');
  }

  const text = await response.text();
  let data = null;
  if (text) {
    try {
      data = JSON.parse(text);
    } catch {
      data = null;
    }
  }

  if (response.status === 401) {
    announceUnauthorized();
    throw new ApiError(401, 'Enter the PIN shown on the TV.');
  }
  if (!response.ok) {
    throw new ApiError(response.status, (data && data.error) || `StrmQt answered ${response.status}.`);
  }
  return data;
}

export const get = (path, params, signal) => request('GET', withQuery(path, params), undefined, signal);
export const post = (path, body = {}) => request('POST', path, body);

// Widths the server is asked for, so neighbouring cards share cached images.
const WIDTH_STEPS = [120, 180, 260, 360, 520, 760, 1100, 1600];

export function imageUrl(ref, cssWidth) {
  if (!ref || !ref.itemId) return '';
  const wanted = cssWidth * Math.min(window.devicePixelRatio || 1, 2.5);
  const width = WIDTH_STEPS.find((step) => step >= wanted) || WIDTH_STEPS[WIDTH_STEPS.length - 1];
  const id = encodeURIComponent(ref.itemId);
  const type = encodeURIComponent(ref.type || 'Primary');
  // <img> cannot send a header, so the token rides in the query.
  return withQuery(`/api/image/${id}/${type}`, { w: width, tag: ref.tag, token });
}

// One event stream for the life of the page. EventSource hides HTTP status, so
// on an error the PIN endpoint is asked whether the problem is authorisation.
export function connectEvents(handlers) {
  let source = null;
  let retries = 0;
  let timer = 0;

  const open = () => {
    clearTimeout(timer);
    source?.close();
    source = new EventSource(withQuery('/api/events', { token }));
    source.addEventListener('open', () => {
      retries = 0;
      handlers.connection?.(true);
    });
    source.addEventListener('status', (event) => handlers.status?.(JSON.parse(event.data)));
    source.addEventListener('queue', (event) => handlers.queue?.(JSON.parse(event.data)));
    source.addEventListener('error', async () => {
      if (source.readyState === EventSource.CONNECTING) {
        handlers.connection?.(false);
        return; // The browser is already retrying.
      }
      source.close();
      handlers.connection?.(false);
      try {
        const pin = await get('/api/auth/pin');
        if (pin.required && !pin.authorized) {
          announceUnauthorized();
          return;
        }
      } catch {
        // Unreachable: fall through to a backed-off retry.
      }
      const delay = Math.min(15000, 1000 * 2 ** retries++);
      timer = setTimeout(open, delay);
    });
  };

  open();

  return {
    reconnect() {
      retries = 0;
      open();
    },
    get alive() {
      return source && source.readyState === EventSource.OPEN;
    },
  };
}
