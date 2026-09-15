// StrmQt Mobile Web Remote — Application Logic

(function() {
  'use strict';

  // ── State ─────────────────────────────────────────────────────────────────
  const state = {
    token: localStorage.getItem('strmqt_token') || '',
    status: {
      active: false,
      paused: true,
      positionMs: 0,
      durationMs: 0,
      title: '',
      subtitle: '',
      mediaType: '',
      streamMethod: '',
      volume: 100,
      muted: false,
      audioStreams: [],
      subtitleStreams: [],
      currentAudioIndex: -1,
      currentSubtitleIndex: -1,
      itemId: ''
    },
    queue: [],
    libraries: [],
    activeLibraryId: 'home',
    currentView: 'view-remote',
    currentItemDetails: null,
    eventSource: null,
    pollTimer: null,
    clockTimer: null,
    isScrubbing: false
  };

  // ── API Fetch Helper ──────────────────────────────────────────────────────
  async function api(path, options = {}) {
    options.headers = options.headers || {};
    if (state.token) {
      options.headers['Authorization'] = 'Bearer ' + state.token;
    }
    if (options.body && typeof options.body === 'object') {
      options.headers['Content-Type'] = 'application/json';
      options.body = JSON.stringify(options.body);
    }

    try {
      const res = await fetch(path, options);
      if (res.status === 401) {
        showPinOverlay();
        throw new Error('Unauthorized');
      }
      return res;
    } catch (err) {
      console.warn('API error:', path, err);
      throw err;
    }
  }

  // ── Formatters ────────────────────────────────────────────────────────────
  function formatTime(ms) {
    if (!ms || ms < 0) return '0:00';
    const totalSecs = Math.floor(ms / 1000);
    const hours = Math.floor(totalSecs / 3600);
    const minutes = Math.floor((totalSecs % 3600) / 60);
    const seconds = totalSecs % 60;
    const secStr = seconds < 10 ? '0' + seconds : seconds;

    if (hours > 0) {
      const minStr = minutes < 10 ? '0' + minutes : minutes;
      return `${hours}:${minStr}:${secStr}`;
    }
    return `${minutes}:${secStr}`;
  }

  function imageUrl(itemId, imageType = 'Primary') {
    if (!itemId) return '';
    let url = `/api/image/${encodeURIComponent(itemId)}/${imageType}`;
    if (state.token) {
      url += `?token=${encodeURIComponent(state.token)}`;
    }
    return url;
  }

  // ── UI Updates ────────────────────────────────────────────────────────────
  function updateUI() {
    const s = state.status;

    // Play/Pause buttons
    const playIcons = document.querySelectorAll('#icon-play, .icon-np-play, #mini-icon-play');
    const pauseIcons = document.querySelectorAll('#icon-pause, .icon-np-pause, #mini-icon-pause');
    if (s.active && !s.paused) {
      playIcons.forEach(el => el.style.display = 'none');
      pauseIcons.forEach(el => el.style.display = 'block');
    } else {
      playIcons.forEach(el => el.style.display = 'block');
      pauseIcons.forEach(el => el.style.display = 'none');
    }

    // Now Playing Metadata
    document.getElementById('np-title').textContent = s.title || (s.active ? 'Playing' : 'Nothing Playing');
    document.getElementById('np-subtitle').textContent = s.subtitle || (s.active ? '' : 'Select media from library');
    document.getElementById('np-badge-type').textContent = s.mediaType || (s.active ? 'PLAYING' : 'IDLE');

    if (s.streamMethod) {
      const m = document.getElementById('np-badge-method');
      m.textContent = s.streamMethod.toUpperCase();
      m.style.display = 'inline-block';
    } else {
      document.getElementById('np-badge-method').style.display = 'none';
    }

    // Poster artwork
    const poster = document.getElementById('np-poster');
    const placeholder = document.getElementById('np-poster-placeholder');
    const backdrop = document.getElementById('np-art-backdrop');
    if (s.itemId) {
      const src = imageUrl(s.itemId, 'Primary');
      poster.src = src;
      poster.style.display = 'block';
      placeholder.style.display = 'none';
      if (backdrop) {
        backdrop.style.backgroundImage = `url("${src}")`;
      }
    } else {
      poster.style.display = 'none';
      placeholder.style.display = 'flex';
      if (backdrop) backdrop.style.backgroundImage = 'none';
    }

    // Mini-player
    const mini = document.getElementById('mini-player');
    if (s.active && state.currentView !== 'view-now-playing') {
      mini.style.display = 'flex';
      document.getElementById('mini-title').textContent = s.title || 'Playing';
      document.getElementById('mini-subtitle').textContent = s.subtitle || '';
      const miniImg = document.getElementById('mini-art-img');
      const miniPlaceholder = document.getElementById('mini-art-placeholder');
      if (s.itemId) {
        miniImg.src = imageUrl(s.itemId, 'Primary');
        miniImg.style.display = 'block';
        miniPlaceholder.style.display = 'none';
      } else {
        miniImg.style.display = 'none';
        miniPlaceholder.style.display = 'flex';
      }
    } else {
      mini.style.display = 'none';
    }

    // Progress Scrubber & Times
    if (!state.isScrubbing) {
      const scrub = document.getElementById('np-scrubber');
      if (s.durationMs > 0) {
        const ratio = Math.min(1, Math.max(0, s.positionMs / s.durationMs));
        scrub.value = Math.floor(ratio * 1000);
        document.getElementById('mini-progress-fill').style.width = (ratio * 100) + '%';
      } else {
        scrub.value = 0;
        document.getElementById('mini-progress-fill').style.width = '0%';
      }
      document.getElementById('np-time-current').textContent = formatTime(s.positionMs);
      document.getElementById('np-time-total').textContent = formatTime(s.durationMs);
    }

    // Volume
    const volSlider = document.getElementById('volume-slider');
    if (document.activeElement !== volSlider) {
      volSlider.value = s.muted ? 0 : s.volume;
      document.getElementById('volume-val').textContent = s.muted ? 'Muted' : `${s.volume}%`;
    }
    const volHigh = document.getElementById('icon-vol-high');
    const volMuted = document.getElementById('icon-vol-muted');
    if (s.muted) {
      volHigh.style.display = 'none';
      volMuted.style.display = 'block';
    } else {
      volHigh.style.display = 'block';
      volMuted.style.display = 'none';
    }

    // Stream labels
    const curAudio = s.audioStreams.find(st => st.index === s.currentAudioIndex);
    document.getElementById('lbl-audio-stream').textContent = curAudio ? (curAudio.title || curAudio.language || 'Audio') : 'Audio';

    const curSub = s.subtitleStreams.find(st => st.index === s.currentSubtitleIndex);
    document.getElementById('lbl-subtitle-stream').textContent = curSub ? (curSub.title || curSub.language || 'Subtitles') : 'Subtitles: Off';
  }

  // ── Scrubber Clock ────────────────────────────────────────────────────────
  function startClock() {
    if (state.clockTimer) clearInterval(state.clockTimer);
    state.clockTimer = setInterval(() => {
      if (state.status.active && !state.status.paused && !state.isScrubbing) {
        state.status.positionMs += 1000;
        if (state.status.durationMs > 0 && state.status.positionMs > state.status.durationMs) {
          state.status.positionMs = state.status.durationMs;
        }
        updateUI();
      }
    }, 1000);
  }

  // ── Real-Time Sync (SSE + Polling) ────────────────────────────────────────
  function connectEvents() {
    if (state.eventSource) {
      state.eventSource.close();
    }

    const badge = document.getElementById('conn-badge');
    const badgeText = document.getElementById('conn-text');

    const sseUrl = '/api/events' + (state.token ? `?token=${encodeURIComponent(state.token)}` : '');
    const es = new EventSource(sseUrl);
    state.eventSource = es;

    es.onopen = () => {
      badge.className = 'status-badge live';
      badgeText.textContent = 'LIVE';
      if (state.pollTimer) {
        clearInterval(state.pollTimer);
        state.pollTimer = null;
      }
    };

    es.addEventListener('status', (e) => {
      try {
        const data = JSON.parse(e.data);
        Object.assign(state.status, data);
        updateUI();
      } catch (err) {
        console.error('Error parsing status event:', err);
      }
    });

    es.addEventListener('queue', (e) => {
      try {
        state.queue = JSON.parse(e.data);
      } catch (err) {
        console.error('Error parsing queue event:', err);
      }
    });

    es.onerror = () => {
      badge.className = 'status-badge offline';
      badgeText.textContent = 'OFFLINE';
      es.close();
      // Fallback polling
      if (!state.pollTimer) {
        state.pollTimer = setInterval(fetchStatus, 3000);
      }
      setTimeout(connectEvents, 5000);
    };
  }

  async function fetchStatus() {
    try {
      const res = await api('/api/status');
      if (res.ok) {
        const data = await res.json();
        Object.assign(state.status, data);
        updateUI();
      }
    } catch (e) {}
  }

  // ── Remote Controls ───────────────────────────────────────────────────────
  async function sendKey(key) {
    try {
      if (navigator.vibrate) navigator.vibrate(25);
    } catch (e) {}
    await api('/api/navigate', { method: 'POST', body: { key } });
  }

  async function sendNav(destination) {
    try {
      if (navigator.vibrate) navigator.vibrate(25);
    } catch (e) {}
    await api('/api/navigate', { method: 'POST', body: { destination } });
  }

  async function sendPlayback(action, value = null) {
    try {
      if (navigator.vibrate) navigator.vibrate(30);
    } catch (e) {}
    await api('/api/playback', { method: 'POST', body: { action, value } });
    setTimeout(fetchStatus, 150);
  }

  async function sendVolume(action, value = null) {
    await api('/api/volume', { method: 'POST', body: { action, value } });
  }

  // ── Library & Browsing ────────────────────────────────────────────────────
  async function loadHome() {
    try {
      const res = await api('/api/home');
      if (!res.ok) return;
      const data = await res.json();

      renderCardsShelf('shelf-resume', 'shelf-resume-cards', data.resume || []);
      renderCardsShelf('shelf-nextup', 'shelf-nextup-cards', data.nextUp || []);
      renderCardsShelf('shelf-favorites', 'shelf-favorites-cards', data.favorites || []);
    } catch (err) {
      console.warn('loadHome failed:', err);
    }
  }

  function renderCardsShelf(sectionId, cardsId, items) {
    const section = document.getElementById(sectionId);
    const container = document.getElementById(cardsId);
    container.innerHTML = '';

    if (!items || items.length === 0) {
      section.style.display = 'none';
      return;
    }
    section.style.display = 'block';

    items.forEach(item => {
      const card = createMediaCard(item);
      container.appendChild(card);
    });
  }

  function createMediaCard(item) {
    const card = document.createElement('div');
    card.className = 'media-card';
    card.dataset.id = item.id;

    const poster = document.createElement('div');
    poster.className = 'card-poster';

    const img = document.createElement('img');
    img.loading = 'lazy';
    img.src = imageUrl(item.id, 'Primary');
    img.alt = item.name || '';
    poster.appendChild(img);

    if (item.playedPercentage && item.playedPercentage > 0 && item.playedPercentage < 100) {
      const pBar = document.createElement('div');
      pBar.className = 'card-progress-bar';
      const pFill = document.createElement('div');
      pFill.className = 'card-progress-fill';
      pFill.style.width = item.playedPercentage + '%';
      pBar.appendChild(pFill);
      poster.appendChild(pBar);
    }
    card.appendChild(poster);

    const title = document.createElement('div');
    title.className = 'card-title';
    title.textContent = item.name || '';
    card.appendChild(title);

    if (item.seriesName || item.artistName || item.productionYear) {
      const sub = document.createElement('div');
      sub.className = 'card-subtitle';
      sub.textContent = item.seriesName || item.artistName || item.productionYear || '';
      card.appendChild(sub);
    }

    card.addEventListener('click', () => openItemDetails(item.id));
    return card;
  }

  async function loadLibraries() {
    try {
      const res = await api('/api/libraries');
      if (!res.ok) return;
      const data = await res.json();
      state.libraries = data || [];

      const pillsContainer = document.getElementById('libraries-pills');
      pillsContainer.innerHTML = '';

      // Home pill
      const homePill = document.createElement('button');
      homePill.className = 'pill active';
      homePill.textContent = 'Home';
      homePill.dataset.lib = 'home';
      homePill.addEventListener('click', () => selectLibrary('home', homePill));
      pillsContainer.appendChild(homePill);

      state.libraries.forEach(lib => {
        const pill = document.createElement('button');
        pill.className = 'pill';
        pill.textContent = lib.name;
        pill.dataset.lib = lib.id;
        pill.addEventListener('click', () => selectLibrary(lib.id, pill));
        pillsContainer.appendChild(pill);
      });
    } catch (e) {}
  }

  async function selectLibrary(libId, pillElement) {
    document.querySelectorAll('#libraries-pills .pill').forEach(p => p.classList.remove('active'));
    pillElement.classList.add('active');
    state.activeLibraryId = libId;

    const shelves = document.querySelectorAll('#shelf-resume, #shelf-nextup, #shelf-favorites');
    const gridSection = document.getElementById('library-grid-section');
    const grid = document.getElementById('library-items-grid');

    if (libId === 'home') {
      gridSection.style.display = 'none';
      loadHome();
    } else {
      shelves.forEach(s => s.style.display = 'none');
      gridSection.style.display = 'block';
      const lib = state.libraries.find(l => l.id === libId);
      document.getElementById('library-grid-title').textContent = lib ? lib.name : 'Library';
      grid.innerHTML = '<div class="empty-state">Loading items...</div>';

      try {
        const res = await api(`/api/library/${encodeURIComponent(libId)}/items?limit=100`);
        if (res.ok) {
          const items = await res.json();
          grid.innerHTML = '';
          if (!items || items.length === 0) {
            grid.innerHTML = '<div class="empty-state">No items found</div>';
          } else {
            items.forEach(item => grid.appendChild(createMediaCard(item)));
          }
        }
      } catch (err) {
        grid.innerHTML = '<div class="empty-state">Failed to load items</div>';
      }
    }
  }

  // ── Item Details Modal ────────────────────────────────────────────────────
  async function openItemDetails(itemId) {
    const modal = document.getElementById('modal-details');
    modal.style.display = 'block';

    document.getElementById('detail-title').textContent = 'Loading...';
    document.getElementById('detail-meta').textContent = '';
    document.getElementById('detail-genres').textContent = '';
    document.getElementById('detail-overview').textContent = '';
    document.getElementById('detail-poster').src = imageUrl(itemId, 'Primary');
    document.getElementById('detail-episodes-section').style.display = 'none';
    document.getElementById('detail-tracks-section').style.display = 'none';

    try {
      const res = await api(`/api/item/${encodeURIComponent(itemId)}`);
      if (!res.ok) return;
      const item = await res.json();
      state.currentItemDetails = item;

      document.getElementById('detail-title').textContent = item.name || '';
      const year = item.productionYear || '';
      const runtime = item.runtimeTicks ? `${Math.round(item.runtimeTicks / (10000 * 1000 * 60))}m` : '';
      document.getElementById('detail-meta').textContent = [year, runtime].filter(Boolean).join(' • ');
      document.getElementById('detail-genres').textContent = (item.genres || []).join(', ');
      document.getElementById('detail-overview').textContent = item.overview || 'No overview available.';

      // Episodes for TV Shows
      if (item.episodes && item.episodes.length > 0) {
        const epSec = document.getElementById('detail-episodes-section');
        const epList = document.getElementById('detail-episodes-list');
        epList.innerHTML = '';
        epSec.style.display = 'block';

        item.episodes.forEach(ep => {
          const row = document.createElement('div');
          row.className = 'list-item-row';
          const epNum = (ep.indexNumber !== undefined) ? `E${ep.indexNumber}` : '';
          const sNum = (ep.parentIndexNumber !== undefined) ? `S${ep.parentIndexNumber}:` : '';
          row.innerHTML = `
            <div class="list-item-title">${sNum}${epNum} ${ep.name}</div>
            <button class="btn-icon" title="Play">
              <svg viewBox="0 0 24 24"><path d="M8 5v14l11-7z"/></svg>
            </button>
          `;
          row.addEventListener('click', () => {
            playMedia(ep.id, 'now');
            closeModals();
          });
          epList.appendChild(row);
        });
      }

      // Tracks for Music Albums
      if (item.tracks && item.tracks.length > 0) {
        const trSec = document.getElementById('detail-tracks-section');
        const trList = document.getElementById('detail-tracks-list');
        trList.innerHTML = '';
        trSec.style.display = 'block';

        item.tracks.forEach(tr => {
          const row = document.createElement('div');
          row.className = 'list-item-row';
          const tNum = tr.indexNumber ? `${tr.indexNumber}. ` : '';
          row.innerHTML = `
            <div class="list-item-title">${tNum}${tr.name}</div>
            <span class="mono-label">${formatTime(tr.runtimeMs)}</span>
          `;
          row.addEventListener('click', () => {
            playMedia(tr.id, 'now');
            closeModals();
          });
          trList.appendChild(row);
        });
      }
    } catch (e) {
      console.error('Failed to load item details:', e);
    }
  }

  async function playMedia(itemId, mode = 'now') {
    try {
      if (navigator.vibrate) navigator.vibrate(40);
    } catch (e) {}
    await api('/api/play', { method: 'POST', body: { itemId, mode } });
    switchView('view-now-playing');
    setTimeout(fetchStatus, 300);
  }

  // ── Search ────────────────────────────────────────────────────────────────
  let searchDebounce = null;
  function onSearchInput(query) {
    const clearBtn = document.getElementById('btn-clear-search');
    clearBtn.style.display = query.length > 0 ? 'block' : 'none';

    if (searchDebounce) clearTimeout(searchDebounce);
    if (!query.trim()) {
      document.getElementById('search-empty-state').style.display = 'block';
      document.getElementById('search-items-grid').style.display = 'none';
      return;
    }

    searchDebounce = setTimeout(async () => {
      try {
        const res = await api(`/api/search?q=${encodeURIComponent(query.trim())}`);
        if (!res.ok) return;
        const items = await res.json();

        const grid = document.getElementById('search-items-grid');
        const empty = document.getElementById('search-empty-state');
        grid.innerHTML = '';

        if (!items || items.length === 0) {
          empty.textContent = 'No results found.';
          empty.style.display = 'block';
          grid.style.display = 'none';
        } else {
          empty.style.display = 'none';
          grid.style.display = 'grid';
          items.forEach(item => grid.appendChild(createMediaCard(item)));
        }
      } catch (e) {}
    }, 280);
  }

  // ── Stream Picker Modal ───────────────────────────────────────────────────
  function openStreamPicker(type) {
    const modal = document.getElementById('modal-picker');
    const title = document.getElementById('modal-picker-title');
    const list = document.getElementById('modal-picker-list');
    list.innerHTML = '';

    if (type === 'audio') {
      title.textContent = 'Select Audio Track';
      state.status.audioStreams.forEach(st => {
        const item = document.createElement('div');
        item.className = 'list-item-row' + (st.index === state.status.currentAudioIndex ? ' selected' : '');
        item.innerHTML = `
          <div class="list-item-title">${st.title || st.language || 'Audio Stream ' + st.index}</div>
          <div class="list-item-subtitle">${st.codec || ''} ${st.channels ? st.channels + 'ch' : ''}</div>
        `;
        item.addEventListener('click', async () => {
          await api('/api/stream', { method: 'POST', body: { type: 'audio', trackId: st.index } });
          closeModals();
          setTimeout(fetchStatus, 200);
        });
        list.appendChild(item);
      });
    } else {
      title.textContent = 'Select Subtitles';
      // "Off" option
      const offItem = document.createElement('div');
      offItem.className = 'list-item-row' + (state.status.currentSubtitleIndex === -1 ? ' selected' : '');
      offItem.innerHTML = `<div class="list-item-title">Off</div>`;
      offItem.addEventListener('click', async () => {
        await api('/api/stream', { method: 'POST', body: { type: 'subtitle', trackId: -1 } });
        closeModals();
        setTimeout(fetchStatus, 200);
      });
      list.appendChild(offItem);

      state.status.subtitleStreams.forEach(st => {
        const item = document.createElement('div');
        item.className = 'list-item-row' + (st.index === state.status.currentSubtitleIndex ? ' selected' : '');
        item.innerHTML = `
          <div class="list-item-title">${st.title || st.language || 'Subtitle Stream ' + st.index}</div>
          <div class="list-item-subtitle">${st.isDefault ? 'Default ' : ''}${st.isForced ? 'Forced' : ''}</div>
        `;
        item.addEventListener('click', async () => {
          await api('/api/stream', { method: 'POST', body: { type: 'subtitle', trackId: st.index } });
          closeModals();
          setTimeout(fetchStatus, 200);
        });
        list.appendChild(item);
      });
    }

    modal.style.display = 'block';
  }

  // ── Queue Viewer Modal ────────────────────────────────────────────────────
  function openQueueModal() {
    const modal = document.getElementById('modal-queue');
    const list = document.getElementById('modal-queue-list');
    list.innerHTML = '';

    if (!state.queue || state.queue.length === 0) {
      list.innerHTML = '<div class="empty-state">Queue is empty</div>';
    } else {
      state.queue.forEach((item, idx) => {
        const row = document.createElement('div');
        row.className = 'list-item-row' + (idx === state.status.queueIndex ? ' selected' : '');
        row.innerHTML = `
          <div class="list-item-title">${idx + 1}. ${item.name || item.title || 'Track'}</div>
          <div class="list-item-subtitle">${item.artist || item.series || ''}</div>
        `;
        row.addEventListener('click', async () => {
          await sendPlayback('jumpQueue', idx);
          closeModals();
        });
        list.appendChild(row);
      });
    }

    modal.style.display = 'block';
  }

  // ── Navigation & Modals ───────────────────────────────────────────────────
  function switchView(viewId) {
    state.currentView = viewId;
    document.querySelectorAll('.view').forEach(v => v.classList.remove('active'));
    document.getElementById(viewId).classList.add('active');

    document.querySelectorAll('.nav-tab').forEach(t => {
      t.classList.toggle('active', t.dataset.view === viewId);
    });

    updateUI();
    if (viewId === 'view-browse' && state.activeLibraryId === 'home') {
      loadHome();
      loadLibraries();
    }
  }

  function closeModals() {
    document.querySelectorAll('.bottom-sheet').forEach(m => m.style.display = 'none');
  }

  function showPinOverlay() {
    document.getElementById('pin-overlay').style.display = 'flex';
  }

  function hidePinOverlay() {
    document.getElementById('pin-overlay').style.display = 'none';
  }

  // ── Wiring Event Listeners ────────────────────────────────────────────────
  function initEvents() {
    // Bottom Nav
    document.querySelectorAll('.nav-tab').forEach(tab => {
      tab.addEventListener('click', () => switchView(tab.dataset.view));
    });

    // D-Pad
    document.querySelectorAll('.dpad-btn').forEach(btn => {
      btn.addEventListener('click', () => sendKey(btn.dataset.key));
    });

    // Quick Nav
    document.querySelectorAll('.nav-btn').forEach(btn => {
      btn.addEventListener('click', () => sendNav(btn.dataset.nav));
    });

    // Header buttons
    document.getElementById('btn-header-osd').addEventListener('click', () => sendNav('osd'));
    document.getElementById('btn-header-queue').addEventListener('click', openQueueModal);

    // Transport buttons
    const playPauseAction = () => sendPlayback('togglePause');
    document.getElementById('btn-play-pause').addEventListener('click', playPauseAction);
    document.querySelector('.np-transport-row [data-action="toggle-play"]').addEventListener('click', playPauseAction);
    document.getElementById('mini-btn-play').addEventListener('click', (e) => {
      e.stopPropagation();
      playPauseAction();
    });

    const prevAction = () => sendPlayback('previous');
    document.getElementById('btn-prev').addEventListener('click', prevAction);
    document.querySelector('.np-transport-row [data-action="prev"]').addEventListener('click', prevAction);

    const nextAction = () => sendPlayback('next');
    document.getElementById('btn-next').addEventListener('click', nextAction);
    document.querySelector('.np-transport-row [data-action="next"]').addEventListener('click', nextAction);

    const skipBackAction = () => sendPlayback('seekRelative', -10000);
    document.getElementById('btn-skip-back').addEventListener('click', skipBackAction);
    document.querySelector('.np-transport-row [data-action="skip-back"]').addEventListener('click', skipBackAction);

    const skipFwdAction = () => sendPlayback('seekRelative', 10000);
    document.getElementById('btn-skip-fwd').addEventListener('click', skipFwdAction);
    document.querySelector('.np-transport-row [data-action="skip-fwd"]').addEventListener('click', skipFwdAction);

    // Volume
    const volSlider = document.getElementById('volume-slider');
    volSlider.addEventListener('input', (e) => {
      const val = parseInt(e.target.value, 10);
      document.getElementById('volume-val').textContent = `${val}%`;
    });
    volSlider.addEventListener('change', (e) => {
      sendVolume('set', parseInt(e.target.value, 10));
    });
    document.getElementById('btn-mute').addEventListener('click', () => sendVolume('toggleMute'));

    // Scrubber
    const scrubber = document.getElementById('np-scrubber');
    scrubber.addEventListener('input', (e) => {
      state.isScrubbing = true;
      if (state.status.durationMs > 0) {
        const pos = (parseInt(e.target.value, 10) / 1000) * state.status.durationMs;
        document.getElementById('np-time-current').textContent = formatTime(pos);
      }
    });
    scrubber.addEventListener('change', (e) => {
      state.isScrubbing = false;
      if (state.status.durationMs > 0) {
        const targetMs = Math.floor((parseInt(e.target.value, 10) / 1000) * state.status.durationMs);
        sendPlayback('seekTo', targetMs);
      }
    });

    // Stream selectors
    document.getElementById('btn-audio-stream').addEventListener('click', () => openStreamPicker('audio'));
    document.getElementById('btn-subtitle-stream').addEventListener('click', () => openStreamPicker('subtitle'));

    // Mini-player tap expands Now Playing
    document.getElementById('mini-player-tap').addEventListener('click', () => switchView('view-now-playing'));

    // Modal backdrops close
    document.getElementById('modal-details-backdrop').addEventListener('click', closeModals);
    document.getElementById('modal-picker-backdrop').addEventListener('click', closeModals);
    document.getElementById('modal-queue-backdrop').addEventListener('click', closeModals);

    // Details actions
    document.getElementById('btn-play-now').addEventListener('click', () => {
      if (state.currentItemDetails) {
        playMedia(state.currentItemDetails.id, 'now');
        closeModals();
      }
    });
    document.getElementById('btn-play-next').addEventListener('click', () => {
      if (state.currentItemDetails) {
        playMedia(state.currentItemDetails.id, 'next');
        closeModals();
      }
    });
    document.getElementById('btn-add-queue').addEventListener('click', () => {
      if (state.currentItemDetails) {
        playMedia(state.currentItemDetails.id, 'queue');
        closeModals();
      }
    });

    // Search
    const searchInput = document.getElementById('search-input');
    searchInput.addEventListener('input', (e) => onSearchInput(e.target.value));
    document.getElementById('btn-clear-search').addEventListener('click', () => {
      searchInput.value = '';
      onSearchInput('');
    });

    // PIN Authentication
    document.getElementById('btn-submit-pin').addEventListener('click', submitPin);
    document.getElementById('pin-input').addEventListener('keyup', (e) => {
      if (e.key === 'Enter') submitPin();
    });
  }

  async function submitPin() {
    const pin = document.getElementById('pin-input').value.trim();
    const err = document.getElementById('pin-error');
    err.style.display = 'none';

    try {
      const res = await fetch('/api/auth/pin', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ pin })
      });
      if (res.ok) {
        const data = await res.json();
        state.token = data.token;
        localStorage.setItem('strmqt_token', data.token);
        hidePinOverlay();
        connectEvents();
        fetchStatus();
        if (state.currentView === 'view-browse') {
          if (state.activeLibraryId === 'home') {
            loadHome();
            loadLibraries();
          } else {
            loadLibraryItems(state.activeLibraryId);
          }
        }
      } else {
        err.style.display = 'block';
      }
    } catch (e) {
      err.style.display = 'block';
    }
  }

  // ── Init ──────────────────────────────────────────────────────────────────
  window.addEventListener('DOMContentLoaded', () => {
    initEvents();
    startClock();
    connectEvents();
    fetchStatus();
  });
})();
