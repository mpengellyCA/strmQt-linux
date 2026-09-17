// Now playing: the strip above the tabs, the full player, and the sheets that
// change audio, subtitles, quality, speed, chapters and the queue.

import { post, imageUrl } from './api.js';
import { store, subscribe, playback, livePosition, patchPlayback } from './store.js';
import {
  h, icon, setIcon, iconButton, haptic, clock, bitrate, fileSize, endsAt, episodeCode, toast, run,
  openOverlay, closeTopOverlay, hasOverlay, openSheet, choiceList, stepper, segmented, emptyState,
} from './ui.js';

const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)');

// ── Commands ───────────────────────────────────────────────────────────────

export const command = {
  playback: (action, value) => run(post('/api/playback', value === undefined ? { action } : { action, value })),
  volume: (action, value) => run(post('/api/volume', value === undefined ? { action } : { action, value })),
  track: (type, trackId) => run(post('/api/stream', { type, trackId })),
  queue: (body) => run(post('/api/queue', body)),
  quality: (body) => run(post('/api/quality', body)),
  subtitleStyle: (body) => run(post('/api/subtitles/style', body)),
  navigate: (body) => run(post('/api/navigate', body)),
};

export function togglePause() {
  const p = playback();
  if (!p.active) return;
  haptic();
  patchPlayback({ paused: !p.paused, positionMs: livePosition() });
  command.playback('togglePause');
}

export function seekRelative(deltaMs) {
  const p = playback();
  if (!p.active) return;
  haptic();
  const target = Math.max(0, Math.min(p.durationMs || Infinity, livePosition() + deltaMs));
  patchPlayback({ positionMs: target });
  command.playback('seekRelative', deltaMs);
}

// ── Labels ─────────────────────────────────────────────────────────────────

const LANGUAGE_CODES = {
  eng: 'en', fre: 'fr', fra: 'fr', ger: 'de', deu: 'de', spa: 'es', ita: 'it', jpn: 'ja', kor: 'ko',
  chi: 'zh', zho: 'zh', por: 'pt', rus: 'ru', dut: 'nl', nld: 'nl', swe: 'sv', nor: 'no', nob: 'nb',
  dan: 'da', fin: 'fi', pol: 'pl', tur: 'tr', ara: 'ar', hin: 'hi', heb: 'he', cze: 'cs', ces: 'cs',
  hun: 'hu', gre: 'el', ell: 'el', tha: 'th', ukr: 'uk', vie: 'vi', ind: 'id', may: 'ms', msa: 'ms',
  rum: 'ro', ron: 'ro', bul: 'bg', hrv: 'hr', srp: 'sr', slv: 'sl', slo: 'sk', slk: 'sk', ice: 'is',
  isl: 'is', est: 'et', lav: 'lv', lit: 'lt', per: 'fa', fas: 'fa', tam: 'ta', tel: 'te', cat: 'ca',
};

let languageNames = null;
try {
  languageNames = new Intl.DisplayNames([navigator.language || 'en'], { type: 'language' });
} catch {
  languageNames = null;
}

export function languageName(code) {
  if (!code || code === 'und') return '';
  const normalised = LANGUAGE_CODES[code.toLowerCase()] || code;
  try {
    const name = languageNames?.of(normalised);
    return name && name.toLowerCase() !== normalised.toLowerCase() ? name : code.toUpperCase();
  } catch {
    return code.toUpperCase();
  }
}

function trackDetail(track, kind) {
  const parts = [];
  const language = languageName(track.language);
  if (language && !track.title?.toLowerCase().includes(language.toLowerCase())) parts.push(language);
  if (track.codec) parts.push(track.codec.toUpperCase());
  if (kind === 'audio' && track.channelLayout) parts.push(track.channelLayout);
  if (track.isDefault) parts.push('Default');
  if (track.isForced) parts.push('Forced');
  if (track.isExternal) parts.push('External file');
  return parts.join(', ');
}

function trackLabel(track, index) {
  return track.title || languageName(track.language) || `Track ${index + 1}`;
}

const METHOD_LABELS = {
  DirectPlay: 'Direct play',
  DirectStream: 'Direct stream',
  Transcode: 'Transcoding',
};

const BITRATES = [
  { value: 0, label: 'Automatic', detail: 'No cap. The server sends the original when it can.' },
  { value: 120000, label: '120 Mbps' },
  { value: 40000, label: '40 Mbps' },
  { value: 20000, label: '20 Mbps' },
  { value: 10000, label: '10 Mbps' },
  { value: 4000, label: '4 Mbps' },
  { value: 2000, label: '2 Mbps' },
];

const MODES = [
  { value: 'auto', label: 'Automatic' },
  { value: 'directPlay', label: 'Direct only' },
  { value: 'transcode', label: 'Transcode' },
];

const SPEEDS = [0.5, 0.75, 1, 1.25, 1.5, 1.75, 2, 3];

const SUB_COLOURS = [
  { value: '#FFFFFF', label: 'White' },
  { value: '#E8E4DC', label: 'Warm white' },
  { value: '#FFE066', label: 'Yellow' },
  { value: '#8AE0FF', label: 'Cyan' },
  { value: '#8CE08C', label: 'Green' },
  { value: '#BFBFBF', label: 'Grey' },
];

function speedLabel(speed) {
  return `${Number(speed).toString()}×`;
}

function signedMs(ms) {
  if (!ms) return '0 ms';
  return `${ms > 0 ? '+' : '−'}${Math.abs(ms)} ms`;
}

function currentItem() {
  return playback().item || null;
}

function itemSubtitle(item, p) {
  if (!item) return p.title ? '' : '';
  if (item.type === 'Episode') {
    return [item.seriesName, episodeCode(item)].filter(Boolean).join(', ');
  }
  if (item.type === 'Audio') {
    const artist = (item.artists && item.artists[0]) || item.albumArtist || '';
    return [artist, item.album].filter(Boolean).join(', ');
  }
  return item.year ? String(item.year) : '';
}

function artRef(item, wide) {
  if (!item) return null;
  const images = item.images || {};
  if (wide) return images.thumb || images.backdrop || images.cover || null;
  return images.cover || images.seriesPoster || images.thumb || null;
}

function isWideArt(item, p) {
  if (!item) return !p.isAudio;
  return !p.isAudio && item.type !== 'Audio';
}

function currentTrackSummary(tracks, currentId, offLabel) {
  if (!tracks?.length) return 'None';
  if (currentId === undefined || currentId < 0) return offLabel;
  const index = tracks.findIndex((t) => t.id === currentId);
  if (index < 0) return offLabel;
  return trackLabel(tracks[index], index);
}

function setImage(img, url) {
  if (img.dataset.src === url) return;
  img.dataset.src = url;
  img.classList.remove('loaded');
  if (!url) {
    img.removeAttribute('src');
    return;
  }
  img.onload = () => img.classList.add('loaded');
  img.onerror = () => img.classList.remove('loaded');
  img.src = url;
}

// ── Mini player ────────────────────────────────────────────────────────────

let miniFrame = 0;

function initMini(openPlayer) {
  const mini = document.getElementById('mini');
  const art = document.getElementById('mini-art');
  const title = document.getElementById('mini-title');
  const sub = document.getElementById('mini-sub');
  const fill = document.getElementById('mini-fill');
  const toggle = document.getElementById('mini-toggle');
  const img = h('img', { alt: '' });
  art.append(img);

  document.getElementById('mini-open').addEventListener('click', openPlayer);
  toggle.addEventListener('click', togglePause);

  const tick = () => {
    const p = playback();
    fill.style.transform = `scaleX(${p.durationMs ? livePosition() / p.durationMs : 0})`;
    miniFrame = p.active && !p.paused && !mini.hidden ? requestAnimationFrame(tick) : 0;
  };

  const render = () => {
    const p = playback();
    const show = !!(p.active || p.busy);
    document.body.classList.toggle('has-mini', show);
    mini.hidden = !show;
    if (!show) return;
    const item = currentItem();
    title.textContent = item?.name || p.title || 'Starting';
    sub.textContent = p.errorMessage ? p.errorMessage
      : p.busy ? 'Loading on the TV'
      : p.buffering ? 'Buffering'
      : itemSubtitle(item, p) || (p.paused ? 'Paused' : '');
    setImage(img, imageUrl(artRef(item, false), 48));
    setIcon(toggle.querySelector('svg'), p.paused ? 'play' : 'pause');
    toggle.setAttribute('aria-label', p.paused ? 'Play' : 'Pause');
    cancelAnimationFrame(miniFrame);
    tick();
  };

  subscribe((kind) => {
    if (kind === 'status') render();
  });
  render();
}

// ── Full player ────────────────────────────────────────────────────────────

export function initPlayer({ openItem }) {
  const root = document.getElementById('player');
  let frame = 0;
  let scrubbing = null; // { ms }

  const bgImg = h('img', { alt: '' });
  const artImg = h('img', { alt: '' });
  const art = h('div', { class: 'player-art' }, artImg);

  const context = h('p', { class: 'player-context' });
  const title = h('h2', { class: 'player-title' });
  const subtitleLink = h('button', { class: 'player-sub', type: 'button' });

  const upNextText = h('span', { class: 'upnext-text' });
  const upNext = h('div', { class: 'upnext', hidden: true },
    upNextText,
    h('div', { class: 'upnext-actions' },
      h('button', { class: 'text-btn', type: 'button', text: 'Cancel', onClick: () => command.playback('cancelUpNext') }),
      h('button', { class: 'btn btn-primary btn-small', type: 'button', text: 'Play now', onClick: () => command.playback('next') })));

  const errorLine = h('p', { class: 'player-error', role: 'alert', hidden: true });

  const counter = h('div', { class: 'counter mono', 'aria-hidden': 'true' });
  const chapterName = h('p', { class: 'counter-chapter' });

  const bufferBar = h('span', { class: 'scrub-buffer' });
  const fillBar = h('span', { class: 'scrub-fill' });
  const ticks = h('span', { class: 'scrub-ticks' });
  const thumb = h('span', { class: 'scrub-thumb' });
  const track = h('span', { class: 'scrub-track' }, bufferBar, ticks, fillBar, thumb);
  const scrub = h('div', {
    class: 'scrub', role: 'slider', tabindex: '0', 'aria-label': 'Position', 'aria-valuemin': '0',
  }, track);
  const remaining = h('span', { class: 'mono' });
  const ends = h('span');
  const scrubMeta = h('div', { class: 'scrub-meta' }, ends, remaining);

  const playIcon = icon('pause');
  const playButton = h('button', { class: 'transport-play', type: 'button', 'aria-label': 'Pause', onClick: togglePause }, playIcon);
  const prevButton = iconButton('prev', 'Previous', () => { haptic(); command.playback('previous'); }, 'transport-btn');
  const nextButton = iconButton('next', 'Next', () => { haptic(); command.playback('next'); }, 'transport-btn');
  const transport = h('div', { class: 'transport' },
    prevButton,
    iconButton('back10', 'Back 10 seconds', () => seekRelative(-10000), 'transport-btn'),
    playButton,
    iconButton('fwd30', 'Forward 30 seconds', () => seekRelative(30000), 'transport-btn'),
    nextButton);

  const volumeIcon = icon('volume');
  const muteButton = h('button', { class: 'icon-btn', type: 'button', 'aria-label': 'Mute', onClick: () => {
    haptic();
    patchPlayback({ muted: !playback().muted });
    command.volume('toggleMute');
  } }, volumeIcon);
  const volumeRange = h('input', { class: 'range', type: 'range', min: '0', max: '100', step: '1', 'aria-label': 'Volume' });
  const volumeValue = h('span', { class: 'volume-value mono' });
  let volumeDragging = false;
  const sendVolume = throttle((value) => command.volume('set', value), 120);
  volumeRange.addEventListener('pointerdown', () => { volumeDragging = true; });
  volumeRange.addEventListener('input', () => {
    const value = Number(volumeRange.value);
    volumeValue.textContent = String(value);
    paintRange(volumeRange);
    sendVolume(value);
  });
  volumeRange.addEventListener('change', () => {
    volumeDragging = false;
    command.volume('set', Number(volumeRange.value));
  });
  const volume = h('div', { class: 'volume' }, muteButton, volumeRange, volumeValue);

  const rows = {};
  const settingRow = (key, iconName, label, onClick) => {
    const value = h('span', { class: 'setting-value' });
    const row = h('button', { class: 'setting', type: 'button', onClick },
      icon(iconName, 'setting-icon'),
      h('span', { class: 'setting-label', text: label }),
      value,
      icon('right', 'setting-chevron'));
    rows[key] = { row, value };
    return row;
  };
  const settings = h('div', { class: 'settings' },
    settingRow('audio', 'audio', 'Audio', openAudioSheet),
    settingRow('subtitles', 'subtitles', 'Subtitles', openSubtitleSheet),
    settingRow('quality', 'quality', 'Quality', openQualitySheet),
    settingRow('speed', 'speed', 'Speed and sync', openSpeedSheet),
    settingRow('chapters', 'chapters', 'Chapters', openChapterSheet),
    settingRow('queue', 'queue', 'Queue', openQueueSheet));

  const streamLine = h('p', { class: 'stream-line mono' });

  const idle = emptyState('Nothing is playing', 'Pick something from Home or the library and it will start on the TV.');
  idle.classList.add('player-idle');

  const body = h('div', { class: 'player-body' },
    art,
    h('div', { class: 'player-titles' }, title, subtitleLink),
    upNext,
    errorLine,
    h('div', { class: 'counter-block' }, counter, chapterName),
    scrub,
    scrubMeta,
    transport,
    volume,
    settings,
    streamLine);

  root.append(
    h('div', { class: 'player-bg', 'aria-hidden': 'true' }, bgImg),
    h('header', { class: 'player-head' },
      iconButton('down', 'Close now playing', () => closeTopOverlay()),
      context,
      iconButton('more', 'More playback options', openMoreSheet)),
    h('div', { class: 'player-scroll' }, body, idle));

  // Scrubbing: drag anywhere on the bar; the counter shows where you will land.
  const ratioAt = (event) => {
    const rect = track.getBoundingClientRect();
    return Math.min(1, Math.max(0, (event.clientX - rect.left) / rect.width));
  };
  scrub.addEventListener('pointerdown', (event) => {
    const p = playback();
    if (!p.active || !p.durationMs) return;
    scrub.setPointerCapture(event.pointerId);
    scrubbing = { ms: ratioAt(event) * p.durationMs };
    root.classList.add('scrubbing');
    haptic(5);
    paint();
  });
  scrub.addEventListener('pointermove', (event) => {
    if (!scrubbing) return;
    const p = playback();
    const ms = ratioAt(event) * p.durationMs;
    const before = chapterIndexAt(scrubbing.ms);
    scrubbing.ms = ms;
    if (chapterIndexAt(ms) !== before) haptic(4);
    paint();
  });
  const endScrub = (commit) => {
    if (!scrubbing) return;
    const ms = Math.round(scrubbing.ms);
    scrubbing = null;
    root.classList.remove('scrubbing');
    if (commit) {
      patchPlayback({ positionMs: ms });
      command.playback('seekTo', ms);
    }
    paint();
  };
  scrub.addEventListener('pointerup', () => endScrub(true));
  scrub.addEventListener('pointercancel', () => endScrub(false));
  scrub.addEventListener('keydown', (event) => {
    const steps = { ArrowLeft: -10000, ArrowRight: 30000, ArrowDown: -60000, ArrowUp: 60000 };
    if (event.key in steps) {
      event.preventDefault();
      seekRelative(steps[event.key]);
    }
  });

  root.addEventListener('keydown', (event) => {
    if (event.target.closest('input, .scrub')) return;
    if (event.key === ' ' || event.key === 'k') {
      event.preventDefault();
      togglePause();
    } else if (event.key === 'Escape' && !document.querySelector('.sheet-wrap')) {
      closeTopOverlay();
    }
  });

  function chapterIndexAt(ms) {
    const chapters = playback().chapters || [];
    let index = -1;
    for (let i = 0; i < chapters.length; i += 1) {
      if (chapters[i].startMs <= ms) index = i;
    }
    return index;
  }

  function paint() {
    const p = playback();
    const duration = p.durationMs || 0;
    const position = scrubbing ? scrubbing.ms : livePosition();
    const hours = duration >= 3600000;
    counter.textContent = clock(position, hours);
    const ratio = duration ? position / duration : 0;
    fillBar.style.transform = `scaleX(${ratio})`;
    thumb.style.left = `${ratio * 100}%`;
    bufferBar.style.transform = `scaleX(${duration ? Math.min(1, (p.bufferedEndMs || 0) / duration) : 0})`;
    remaining.textContent = duration ? `−${clock(duration - position, hours)} of ${clock(duration, hours)}` : '';
    ends.textContent = duration && !p.isAudio ? `Ends ${endsAt((duration - position) / (p.speed || 1))}` : '';
    const chapters = p.chapters || [];
    const index = chapterIndexAt(position);
    chapterName.textContent = index >= 0 && chapters[index].name ? chapters[index].name : '';
    scrub.setAttribute('aria-valuemax', String(Math.round(duration / 1000)));
    scrub.setAttribute('aria-valuenow', String(Math.round(position / 1000)));
    scrub.setAttribute('aria-valuetext', `${clock(position, hours)} of ${clock(duration, hours)}`);
  }

  const loop = () => {
    paint();
    const p = playback();
    frame = !root.hidden && (scrubbing || (p.active && !p.paused)) ? requestAnimationFrame(loop) : 0;
  };

  let ticksKey = '';
  function render() {
    if (root.hidden) return;
    const p = playback();
    const item = currentItem();
    const active = !!(p.active || p.busy);
    body.hidden = !active;
    idle.hidden = active;

    const wide = isWideArt(item, p);
    art.classList.toggle('wide', wide);
    setImage(artImg, imageUrl(artRef(item, wide), 420));
    setImage(bgImg, imageUrl(artRef(item, wide), 260));

    context.textContent = p.queue?.contextLabel
      ? p.queue.contextLabel
      : p.queue?.count > 1 ? `${p.queue.index + 1} of ${p.queue.count}` : 'Now playing';
    title.textContent = item?.name || p.title || '';
    const sub = itemSubtitle(item, p);
    subtitleLink.textContent = sub;
    subtitleLink.hidden = !sub;
    const target = item?.type === 'Episode' ? item.seriesId : item?.type === 'Audio' ? item.albumId : '';
    subtitleLink.disabled = !target;
    subtitleLink.onclick = target ? () => openItem({ id: target }) : null;

    const upNextData = p.upNext || {};
    upNext.hidden = !upNextData.visible;
    if (upNextData.visible) {
      upNextText.textContent = `Up next in ${upNextData.seconds} s: ${upNextData.item?.name || 'next item'}`;
    }

    errorLine.hidden = !p.errorMessage;
    errorLine.textContent = p.errorMessage || '';
    root.classList.toggle('is-busy', !!(p.busy || p.buffering));

    setIcon(playIcon, p.paused ? 'play' : 'pause');
    playButton.setAttribute('aria-label', p.paused ? 'Play' : 'Pause');
    prevButton.disabled = !p.queue?.hasPrevious && !(livePosition() > 3000);
    nextButton.disabled = !p.queue?.hasNext;

    if (!volumeDragging) {
      volumeRange.max = String(p.maxVolume || 100);
      volumeRange.value = String(p.volume ?? 100);
      volumeValue.textContent = String(p.volume ?? 100);
      paintRange(volumeRange);
    }
    setIcon(volumeIcon, p.muted ? 'mute' : 'volume');
    muteButton.setAttribute('aria-label', p.muted ? 'Unmute' : 'Mute');
    volume.classList.toggle('muted', !!p.muted);

    const chapters = p.chapters || [];
    const key = `${p.durationMs}:${chapters.map((c) => c.startMs).join(',')}`;
    if (key !== ticksKey) {
      ticksKey = key;
      ticks.replaceChildren(...chapters
        .filter((c) => c.startMs > 0 && p.durationMs && c.startMs < p.durationMs)
        .map((c) => h('span', { class: 'scrub-tick', style: { left: `${(c.startMs / p.durationMs) * 100}%` } })));
    }

    rows.audio.value.textContent = currentTrackSummary(p.audioTracks, p.currentAudioTrackId, 'Off');
    rows.subtitles.value.textContent = currentTrackSummary(p.subtitleTracks, p.currentSubtitleTrackId, 'Off');
    const quality = store.status?.quality || {};
    rows.quality.value.textContent = [
      METHOD_LABELS[p.streamMethod] || '',
      quality.maxBitrateKbps ? `max ${quality.maxBitrateKbps / 1000} Mbps` : '',
    ].filter(Boolean).join(', ') || 'Automatic';
    const syncChanged = p.audioDelayMs || p.subtitleDelayMs;
    rows.speed.value.textContent = `${speedLabel(p.speed || 1)}${syncChanged ? ', sync adjusted' : ''}`;
    rows.chapters.row.hidden = !chapters.length;
    rows.chapters.value.textContent = chapters.length ? `${Math.max(0, p.currentChapter) + 1} of ${chapters.length}` : '';
    rows.queue.value.textContent = p.queue?.count ? `${p.queue.index + 1} of ${p.queue.count}` : 'Empty';
    rows.audio.row.hidden = !!p.isAudio && (p.audioTracks?.length || 0) <= 1;
    rows.subtitles.row.hidden = !!p.isAudio;

    const source = p.currentSource || {};
    const video = source.videoStream || {};
    streamLine.textContent = [
      video.codec ? video.codec.toUpperCase() : '',
      source.resolutionLabel || '',
      source.isHdr ? (video.videoRange || 'HDR') : '',
      bitrate(source.bitrate),
    ].filter(Boolean).join('  ');

    cancelAnimationFrame(frame);
    loop();
  }

  function open() {
    if (hasOverlay('player')) return;
    root.hidden = false;
    document.body.classList.add('player-open');
    requestAnimationFrame(() => root.classList.add('open'));
    render();
    // Focus the panel itself: screen readers land inside it, and no ring appears on a tap.
    root.focus({ preventScroll: true });
    openOverlay('player', () => {
      root.classList.remove('open');
      document.body.classList.remove('player-open');
      cancelAnimationFrame(frame);
      const hide = () => { root.hidden = true; };
      if (reducedMotion.matches) hide();
      else setTimeout(hide, 300);
      document.getElementById('mini-open')?.focus({ preventScroll: true });
    });
  }

  // Swipe the header down to close.
  let swipeStart = null;
  const head = root.querySelector('.player-head');
  head.addEventListener('pointerdown', (event) => {
    if (event.target.closest('button')) return;
    swipeStart = event.clientY;
  });
  head.addEventListener('pointerup', (event) => {
    if (swipeStart !== null && event.clientY - swipeStart > 60) closeTopOverlay();
    swipeStart = null;
  });

  subscribe((kind) => {
    if (kind === 'status') render();
  });
  document.addEventListener('visibilitychange', () => {
    if (!document.hidden) render();
  });

  initMini(open);
  return { open };
}

function paintRange(range) {
  const max = Number(range.max) || 100;
  range.style.setProperty('--fill', `${(Number(range.value) / max) * 100}%`);
}

function throttle(fn, wait) {
  let last = 0;
  let timer = 0;
  return (...args) => {
    const now = Date.now();
    clearTimeout(timer);
    if (now - last >= wait) {
      last = now;
      fn(...args);
    } else {
      timer = setTimeout(() => {
        last = Date.now();
        fn(...args);
      }, wait - (now - last));
    }
  };
}

// Re-render a sheet's live part whenever the TV pushes, but only when what it
// shows actually changed, so a list doesn't jump under a finger.
function liveSection(select, render) {
  const node = h('div');
  let key = null;
  const update = () => {
    const value = select();
    const next = JSON.stringify(value);
    if (next === key) return;
    key = next;
    node.replaceChildren(...[render(value)].flat().filter(Boolean));
  };
  update();
  const unsubscribe = subscribe((kind) => {
    if (kind === 'status' || kind === 'queue') update();
  });
  return { node, unsubscribe };
}

function nothingPlaying() {
  return emptyState('Nothing is playing', 'Start something on the TV first.');
}

// ── Sheets ─────────────────────────────────────────────────────────────────

function openAudioSheet() {
  openSheet({
    title: 'Audio',
    build(body) {
      const live = liveSection(
        () => ({ tracks: playback().audioTracks || [], current: playback().currentAudioTrackId, active: playback().active }),
        ({ tracks, current, active }) => {
          if (!active) return nothingPlaying();
          if (!tracks.length) return emptyState('No audio tracks', 'The TV hasn’t reported any audio for this stream yet.');
          return choiceList(tracks.map((t, i) => ({ value: t.id, label: trackLabel(t, i), detail: trackDetail(t, 'audio') })), {
            current,
            onPick: (id) => command.track('audio', id),
          });
        });
      body.append(live.node);
      return live.unsubscribe;
    },
  });
}

function openSubtitleSheet() {
  openSheet({
    title: 'Subtitles',
    tall: true,
    build(body) {
      const live = liveSection(
        () => ({ tracks: playback().subtitleTracks || [], current: playback().currentSubtitleTrackId, active: playback().active }),
        ({ tracks, current, active }) => {
          if (!active) return nothingPlaying();
          const options = [{ value: -1, label: 'Off' }]
            .concat(tracks.map((t, i) => ({ value: t.id, label: trackLabel(t, i), detail: trackDetail(t, 'subtitle') })));
          return choiceList(options, {
            current: current === undefined || current < 0 ? -1 : current,
            onPick: (id) => command.track('subtitle', id),
          });
        });

      const p = playback();
      const delay = stepper({
        label: 'Offset',
        value: p.subtitleDelayMs || 0,
        step: 100,
        min: -60000,
        max: 60000,
        resetTo: 0,
        format: signedMs,
        onChange: debounceCommand((ms) => command.playback('setSubtitleDelay', ms)),
      });

      const style = store.status?.subtitleStyle || { scale: 100, color: '#FFFFFF', background: 0, position: 100 };
      const size = segmented(
        [{ value: 75, label: 'Small' }, { value: 100, label: 'Normal' }, { value: 130, label: 'Large' }, { value: 170, label: 'Huge' }],
        { current: nearest([75, 100, 130, 170], style.scale), label: 'Subtitle size', onPick: (scale) => command.subtitleStyle({ scale }) });

      const swatches = h('div', { class: 'swatches', role: 'radiogroup', 'aria-label': 'Subtitle colour' });
      const renderSwatches = (current) => {
        swatches.replaceChildren(...SUB_COLOURS.map((c) => h('button', {
          class: `swatch${c.value.toUpperCase() === String(current).toUpperCase() ? ' selected' : ''}`,
          type: 'button',
          role: 'radio',
          'aria-checked': c.value.toUpperCase() === String(current).toUpperCase() ? 'true' : 'false',
          'aria-label': c.label,
          title: c.label,
          style: { '--swatch': c.value },
          onClick: () => {
            haptic();
            renderSwatches(c.value);
            command.subtitleStyle({ color: c.value });
          },
        })));
      };
      renderSwatches(style.color);

      const background = rangeRow('Background', style.background, 0, 100, 5, (v) => `${v}%`,
        (value) => command.subtitleStyle({ background: value }));
      const position = rangeRow('Height', 100 - Math.min(100, style.position), 0, 60, 2, (v) => (v ? `+${v}` : 'Bottom'),
        (value) => command.subtitleStyle({ position: 100 - value }));

      body.append(
        live.node,
        h('h3', { class: 'sheet-section', text: 'Timing' }),
        h('p', { class: 'sheet-note', text: 'Positive shows subtitles later, negative earlier.' }),
        delay.node,
        h('h3', { class: 'sheet-section', text: 'Look' }),
        h('div', { class: 'field' }, h('span', { class: 'field-label', text: 'Size' }), size.node),
        h('div', { class: 'field' }, h('span', { class: 'field-label', text: 'Colour' }), swatches),
        background,
        position);
      const unsubscribeDelay = subscribe((kind) => {
        if (kind === 'status') delay.set(playback().subtitleDelayMs || 0);
      });
      return () => {
        live.unsubscribe();
        unsubscribeDelay();
      };
    },
  });
}

function rangeRow(label, value, min, max, step, format, onCommit) {
  const readout = h('span', { class: 'field-value mono', text: format(value) });
  const input = h('input', { class: 'range', type: 'range', min: String(min), max: String(max), step: String(step), value: String(value), 'aria-label': label });
  paintRange(input);
  input.addEventListener('input', () => {
    readout.textContent = format(Number(input.value));
    paintRange(input);
  });
  input.addEventListener('change', () => onCommit(Number(input.value)));
  return h('div', { class: 'field' },
    h('div', { class: 'field-row' }, h('span', { class: 'field-label', text: label }), readout),
    input);
}

function nearest(values, target) {
  return values.reduce((best, v) => (Math.abs(v - target) < Math.abs(best - target) ? v : best), values[0]);
}

function debounceCommand(fn) {
  let timer = 0;
  return (value) => {
    clearTimeout(timer);
    timer = setTimeout(() => fn(value), 250);
  };
}

function openQualitySheet() {
  openSheet({
    title: 'Quality',
    tall: true,
    build(body) {
      const stream = liveSection(
        () => {
          const p = playback();
          return { active: p.active, method: p.streamMethod, source: p.currentSource, sources: p.sources, sourceIndex: p.sourceIndex };
        },
        ({ active, method, source, sources, sourceIndex }) => {
          if (!active) return null;
          const parts = [];
          const video = source?.videoStream || {};
          const facts = [
            ['Delivery', METHOD_LABELS[method] || 'Starting'],
            ['Video', [video.codec?.toUpperCase(), video.profile, source?.resolutionLabel, source?.isHdr ? (video.videoRange || 'HDR') : '']
              .filter(Boolean).join(' ')],
            ['Bitrate', bitrate(source?.bitrate)],
            ['Container', source?.container?.toUpperCase()],
            ['Size', fileSize(source?.size)],
          ].filter(([, v]) => v);
          const reasons = Array.isArray(source?.transcodeReasons) ? source.transcodeReasons : [];
          parts.push(h('dl', { class: 'facts' }, ...facts.map(([k, v]) => [h('dt', { text: k }), h('dd', { class: 'mono', text: v })])));
          if (method === 'Transcode' && reasons.length) {
            parts.push(h('p', { class: 'sheet-note', text: `Transcoding because: ${reasons.map(humanReason).join(', ')}.` }));
          }
          if ((sources || []).length > 1) {
            parts.push(h('h3', { class: 'sheet-section', text: 'Version' }));
            parts.push(choiceList(sources.map((s) => ({
              value: s.index,
              label: s.displayName || s.name || `Version ${s.index + 1}`,
              detail: [s.resolutionLabel, s.isHdr ? 'HDR' : '', bitrate(s.bitrate), fileSize(s.size)].filter(Boolean).join('  '),
              mono: true,
              disabled: s.playable === false,
            })), {
              current: sourceIndex,
              onPick: (index) => command.playback('setSource', index).then(() => toast('Switching version')),
            }));
          }
          return parts;
        });

      const quality = store.status?.quality || { maxBitrateKbps: 0, playbackMode: 'auto' };
      let pending = false;
      const apply = h('button', {
        class: 'btn btn-primary btn-block', type: 'button', disabled: true,
        onClick: async () => {
          if (!playback().active) return;
          haptic();
          apply.disabled = true;
          const ok = await command.quality({ apply: true });
          if (ok) toast('Restarting the stream at the new quality');
          pending = false;
        },
      }, icon('reload'), h('span', { text: 'Apply to what’s playing' }));
      const markPending = () => {
        pending = true;
        apply.disabled = !playback().active;
      };

      const ladder = h('div');
      const renderLadder = (current) => {
        ladder.replaceChildren(choiceList(BITRATES, {
          current,
          onPick: async (value) => {
            renderLadder(value);
            if (await command.quality({ maxBitrateKbps: value })) markPending();
          },
        }));
      };
      renderLadder(quality.maxBitrateKbps);

      const mode = segmented(MODES, {
        current: quality.playbackMode,
        label: 'Playback mode',
        onPick: async (value) => {
          if (await command.quality({ playbackMode: value })) markPending();
        },
      });

      body.append(
        stream.node,
        h('h3', { class: 'sheet-section', text: 'Playback mode' }),
        mode.node,
        h('h3', { class: 'sheet-section', text: 'Maximum bitrate' }),
        h('p', { class: 'sheet-note', text: 'A cap makes the server transcode anything above it. Changes apply to the next thing you play.' }),
        ladder,
        h('div', { class: 'sheet-footer' }, apply));
      return () => {
        stream.unsubscribe();
        if (pending && playback().active) toast('New quality applies to the next stream');
      };
    },
  });
}

function humanReason(reason) {
  return String(reason)
    .replace(/([a-z])([A-Z])/g, '$1 $2')
    .toLowerCase();
}

function openSpeedSheet() {
  openSheet({
    title: 'Speed and sync',
    build(body) {
      const p = playback();
      const speed = segmented(SPEEDS.map((s) => ({ value: s, label: speedLabel(s) })), {
        current: nearest(SPEEDS, p.speed || 1),
        label: 'Playback speed',
        onPick: (value) => command.playback('setSpeed', value),
      });
      speed.node.classList.add('segmented-grid');
      const audioDelay = stepper({
        label: 'Audio delay',
        value: p.audioDelayMs || 0,
        step: 50,
        min: -10000,
        max: 10000,
        resetTo: 0,
        format: signedMs,
        onChange: debounceCommand((ms) => command.playback('setAudioDelay', ms)),
      });
      const subDelay = stepper({
        label: 'Subtitle delay',
        value: p.subtitleDelayMs || 0,
        step: 100,
        min: -60000,
        max: 60000,
        resetTo: 0,
        format: signedMs,
        onChange: debounceCommand((ms) => command.playback('setSubtitleDelay', ms)),
      });
      body.append(
        h('h3', { class: 'sheet-section first', text: 'Speed' }),
        speed.node,
        h('h3', { class: 'sheet-section', text: 'Sync' }),
        h('p', { class: 'sheet-note', text: 'If voices come before lips move, add audio delay.' }),
        audioDelay.node,
        subDelay.node);
      return subscribe((kind) => {
        if (kind !== 'status') return;
        const now = playback();
        speed.set(nearest(SPEEDS, now.speed || 1));
        audioDelay.set(now.audioDelayMs || 0);
        subDelay.set(now.subtitleDelayMs || 0);
      });
    },
  });
}

function openChapterSheet() {
  openSheet({
    title: 'Chapters',
    tall: true,
    build(body) {
      const live = liveSection(
        () => ({ chapters: playback().chapters || [], current: playback().currentChapter }),
        ({ chapters, current }) => {
          if (!chapters.length) return emptyState('No chapters', 'This title has no chapter marks.');
          return h('ol', { class: 'chapter-list' }, chapters.map((c, i) => h('li', null,
            h('button', {
              class: `chapter${i === current ? ' current' : ''}`,
              type: 'button',
              'aria-current': i === current ? 'true' : null,
              onClick: () => {
                haptic();
                patchPlayback({ positionMs: c.startMs });
                command.playback('seekChapter', i);
              },
            },
            h('span', { class: 'chapter-time mono', text: clock(c.startMs, (playback().durationMs || 0) >= 3600000) }),
            h('span', { class: 'chapter-name', text: c.name || `Chapter ${i + 1}` })))));
        });
      body.append(live.node);
      requestAnimationFrame(() => body.querySelector('.chapter.current')?.scrollIntoView({ block: 'center' }));
      return live.unsubscribe;
    },
  });
}

const REPEAT_NEXT = { off: 'all', all: 'one', one: 'off' };
const REPEAT_LABEL = { off: 'Repeat off', all: 'Repeat all', one: 'Repeat one' };

function openQueueSheet() {
  openSheet({
    title: 'Queue',
    tall: true,
    build(body, sheet) {
      const controls = liveSection(
        () => ({ shuffled: playback().queue?.shuffled, repeat: playback().queue?.repeat || 'off', count: store.queue.length }),
        ({ shuffled, repeat, count }) => h('div', { class: 'queue-controls' },
          h('button', {
            class: `chip${shuffled ? ' on' : ''}`, type: 'button', 'aria-pressed': shuffled ? 'true' : 'false',
            onClick: () => { haptic(); command.playback('setShuffle', !shuffled); },
          }, icon('shuffle'), h('span', { text: 'Shuffle' })),
          h('button', {
            class: `chip${repeat !== 'off' ? ' on' : ''}`, type: 'button',
            onClick: () => { haptic(); command.playback('setRepeat', REPEAT_NEXT[repeat]); },
          }, icon(repeat === 'one' ? 'repeat-one' : 'repeat'), h('span', { text: REPEAT_LABEL[repeat] })),
          count ? h('button', {
            class: 'chip chip-danger', type: 'button',
            onClick: () => confirmClear(),
          }, icon('trash'), h('span', { text: 'Clear' })) : null));

      let armed = 0;
      const confirmClear = () => {
        if (Date.now() - armed < 3000) {
          command.queue({ action: 'clear' });
          armed = 0;
          return;
        }
        armed = Date.now();
        toast('Tap Clear again to empty the queue');
      };

      let openRow = -1;
      const listHost = h('div');
      let listKey = null;
      const renderList = (force = false) => {
        const queue = store.queue;
        const key = JSON.stringify([openRow, queue.map((q) => [q.id, q.label || q.name, q.current])]);
        if (!force && key === listKey) return;
        listKey = key;
        sheet.setSubtitle(queue.length ? `${queue.length} ${queue.length === 1 ? 'item' : 'items'}` : '');
        if (!queue.length) {
          listHost.replaceChildren(emptyState('The queue is empty', 'Use Play next or Add to queue on anything in your library.'));
          return;
        }
        const toggleRow = (index) => {
          openRow = openRow === index ? -1 : index;
          renderList(true);
        };
        const move = (index, to) => {
          haptic();
          openRow = to;
          command.queue({ action: 'move', index, to });
        };
        listHost.replaceChildren(h('ol', { class: 'queue-list' }, queue.map((item, index) => {
          const expanded = index === openRow;
          return h('li', { class: `queue-row${item.current ? ' current' : ''}${expanded ? ' expanded' : ''}` },
            h('button', {
              class: 'queue-main', type: 'button', 'aria-current': item.current ? 'true' : null,
              onClick: () => { haptic(); command.queue({ action: 'jump', index }); },
            },
            h('span', { class: 'queue-index mono' }, item.current ? icon('audio', 'eq') : String(index + 1)),
            h('span', { class: 'queue-text' },
              h('span', { class: 'queue-title', text: item.type === 'Episode' || item.type === 'Audio' ? item.name : (item.label || item.name) }),
              h('span', { class: 'queue-sub', text: queueSubtitle(item) }))),
            h('button', {
              class: 'icon-btn', type: 'button', 'aria-expanded': expanded ? 'true' : 'false',
              'aria-label': `Options for ${item.name}`, onClick: () => toggleRow(index),
            }, icon('more')),
            expanded ? h('div', { class: 'queue-actions' },
              index > 0 ? h('button', { class: 'chip', type: 'button', onClick: () => move(index, index - 1) }, icon('move-up'), h('span', { text: 'Move up' })) : null,
              index < queue.length - 1 ? h('button', { class: 'chip', type: 'button', onClick: () => move(index, index + 1) }, icon('move-down'), h('span', { text: 'Move down' })) : null,
              h('button', { class: 'chip chip-danger', type: 'button', onClick: () => { openRow = -1; command.queue({ action: 'remove', index }); } }, icon('trash'), h('span', { text: 'Remove' })))
              : null);
        })));
      };
      renderList(true);
      const unsubscribeList = subscribe((kind) => {
        if (kind === 'queue' || kind === 'status') renderList();
      });

      body.append(controls.node, listHost);
      requestAnimationFrame(() => body.querySelector('.queue-row.current')?.scrollIntoView({ block: 'center' }));
      return () => {
        controls.unsubscribe();
        unsubscribeList();
      };
    },
  });
}

function queueSubtitle(item) {
  if (item.type === 'Episode') return [item.seriesName, episodeCode(item)].filter(Boolean).join(', ');
  if (item.type === 'Audio') return [(item.artists || [])[0], item.album].filter(Boolean).join(', ');
  return item.runtimeMs ? clock(item.runtimeMs, item.runtimeMs >= 3600000) : '';
}

function openMoreSheet() {
  openSheet({
    title: 'Playback',
    build(body, sheet) {
      const p = playback();
      const actions = [
        { iconName: 'osd', label: 'Show controls on the TV', run: () => command.navigate({ destination: 'osd' }) },
        { iconName: 'reload', label: 'Reload the stream', detail: 'Fixes a stalled or broken picture without losing your place.', run: () => command.playback('reloadStream'), needsActive: true },
        { iconName: 'camera', label: 'Take a screenshot', detail: 'Saved on the TV’s computer.', run: () => command.playback('screenshot'), needsActive: true, video: true },
        { iconName: 'stop', label: 'Stop playback', danger: true, run: () => command.playback('stop'), needsActive: true },
      ];
      body.append(h('div', { class: 'action-list' }, actions
        .filter((a) => !(a.video && p.isAudio))
        .map((a) => h('button', {
          class: `action${a.danger ? ' danger' : ''}`, type: 'button', disabled: a.needsActive && !p.active,
          onClick: async () => {
            haptic();
            sheet.close();
            await a.run();
          },
        },
        icon(a.iconName, 'action-icon'),
        h('span', { class: 'action-text' },
          h('span', { class: 'action-label', text: a.label }),
          a.detail ? h('span', { class: 'action-detail', text: a.detail }) : null)))));
    },
  });
}
