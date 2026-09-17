// Browsing the server through StrmQt: home rails, libraries, drill-down detail
// pages and search. Every play verb goes to the TV.

import { get, post, imageUrl } from './api.js';
import {
  h, append, icon, iconButton, button, haptic, toast, run, clock, duration, endsAt, bitrate, fileSize, count,
  episodeCode, sectionHeading, emptyState, errorState, debounce, openSheet, choiceList,
} from './ui.js';

// ── Play verbs ─────────────────────────────────────────────────────────────

const PLAY_MESSAGES = {
  next: 'Playing next',
  queue: 'Added to the queue',
};

export async function play(ctx, body) {
  haptic();
  const ok = await run(post('/api/play', body));
  if (ok === undefined) return;
  if (PLAY_MESSAGES[body.mode]) {
    toast(PLAY_MESSAGES[body.mode]);
  } else {
    toast('Starting on the TV');
    ctx.openPlayer();
  }
}

function listPayload(items) {
  return items.map((item) => ({
    id: item.id, name: item.name, type: item.type, seriesName: item.seriesName, seriesId: item.seriesId,
    seasonId: item.seasonId, indexNumber: item.indexNumber, parentIndexNumber: item.parentIndexNumber,
    runtimeMs: item.runtimeMs, album: item.album, albumId: item.albumId,
  }));
}

// ── Cards and rows ─────────────────────────────────────────────────────────

const SHAPE_WIDTH = { poster: 130, square: 150, wide: 250, round: 130 };

function pickImage(item, shape) {
  const images = item.images || {};
  if (shape === 'wide') return images.thumb || images.backdrop || images.cover || null;
  if (item.type === 'Episode' && shape === 'poster') return images.seriesPoster || images.cover || null;
  return images.cover || images.thumb || null;
}

function initials(name) {
  return (name || '?').trim().split(/\s+/).slice(0, 2).map((w) => w[0]).join('').toUpperCase();
}

function artImage(ref, width) {
  if (!ref) return null;
  const img = h('img', { alt: '', loading: 'lazy', decoding: 'async', src: imageUrl(ref, width) });
  img.addEventListener('load', () => img.classList.add('loaded'));
  img.addEventListener('error', () => img.remove());
  return img;
}

function progressBar(item) {
  if (!item.playedPercentage || item.played) return null;
  return h('span', { class: 'progress' }, h('span', { class: 'progress-fill', style: { width: `${Math.min(100, item.playedPercentage)}%` } }));
}

function stateBadge(item) {
  if (item.played && item.type !== 'MusicAlbum' && item.type !== 'Audio') {
    return h('span', { class: 'badge badge-played', title: 'Played' }, icon('check'));
  }
  if (item.unplayedCount && (item.type === 'Series' || item.type === 'Season')) {
    return h('span', { class: 'badge badge-count mono', text: item.unplayedCount > 99 ? '99+' : String(item.unplayedCount) });
  }
  return null;
}

function cardText(item, shape, { wideEpisodeTitle = false } = {}) {
  if (item.type === 'Episode') {
    if (wideEpisodeTitle) return [item.seriesName || item.name, [episodeCode(item), item.name].filter(Boolean).join('  ')];
    return [item.name, [item.seriesName, episodeCode(item)].filter(Boolean).join(', ')];
  }
  if (item.type === 'MusicAlbum') return [item.name, item.albumArtist || (item.artists || [])[0] || String(item.year || '')];
  if (item.type === 'Audio') return [item.name, (item.artists || [])[0] || item.album || ''];
  if (item.type === 'Season') return [item.name, item.childCount ? count(item.childCount, 'episode', 'episodes') : ''];
  if (item.type === 'BoxSet' || item.type === 'Playlist' || item.type === 'Folder' || item.type === 'CollectionFolder') {
    return [item.name, item.childCount ? count(item.childCount, 'item', 'items') : ''];
  }
  if (item.type === 'MusicArtist' || shape === 'round') return [item.name, ''];
  if (item.type === 'Series') return [item.name, item.year ? String(item.year) : ''];
  return [item.name, item.year ? String(item.year) : ''];
}

export function card(item, ctx, shape = 'poster', options = {}) {
  const [title, sub] = cardText(item, shape, options);
  return h('button', {
    class: `card card-${shape}`,
    type: 'button',
    onClick: () => ctx.openItem(item),
  },
  h('span', { class: 'card-art' },
    h('span', { class: 'card-fallback', 'aria-hidden': 'true', text: initials(item.name) }),
    artImage(pickImage(item, shape), SHAPE_WIDTH[shape]),
    stateBadge(item),
    item.favorite ? h('span', { class: 'badge badge-fav', title: 'Favourite' }, icon('heart')) : null,
    progressBar(item)),
  h('span', { class: 'card-title', text: title }),
  sub ? h('span', { class: 'card-sub', text: sub }) : null);
}

function rail(title, items, ctx, shape, { onTitle, cardOptions } = {}) {
  const heading = onTitle
    ? h('button', { class: 'section-link', type: 'button', onClick: onTitle }, h('h2', { class: 'section-title', text: title }), icon('right'))
    : h('h2', { class: 'section-title', text: title });
  return h('section', { class: 'rail-section' },
    h('div', { class: 'section-head' }, heading),
    h('div', { class: `rail rail-${shape}` }, items.map((item) => card(item, ctx, shape, cardOptions))));
}

function shapeForItems(items) {
  const type = items[0]?.type;
  if (type === 'MusicAlbum' || type === 'Audio' || type === 'Playlist') return 'square';
  if (type === 'MusicArtist') return 'round';
  if (type === 'Episode' || type === 'Video' || type === 'MusicVideo') return 'wide';
  return 'poster';
}

function skeletonGrid(shape = 'poster', n = 9) {
  return h('div', { class: `grid grid-${shape} skeleton`, 'aria-hidden': 'true' },
    Array.from({ length: n }, () => h('span', { class: 'card' }, h('span', { class: 'card-art' }), h('span', { class: 'sk-line' }))));
}

function metaLine(parts) {
  return h('p', { class: 'meta' }, parts.filter(Boolean).map((part) => (part instanceof Node ? part : h('span', { text: part }))));
}

// Loads into a node with a skeleton, an error with retry, and a render step.
async function load(node, skeleton, fetcher, render) {
  node.replaceChildren(skeleton);
  try {
    const data = await fetcher();
    node.replaceChildren();
    render(data);
  } catch (error) {
    if (error.name === 'AbortError') return;
    node.replaceChildren(errorState(error, () => load(node, skeleton, fetcher, render)));
  }
}

// ── Home ───────────────────────────────────────────────────────────────────

export function homeView(node, params, ctx) {
  ctx.setTitle('Home');
  let loadedAt = 0;
  const refresh = () => {
    loadedAt = Date.now();
    load(node, h('div', { class: 'stack' }, skeletonGrid('wide', 2), skeletonGrid('poster', 6)), () => get('/api/home'), (home) => {
      const sections = [];
      if (home.resume?.length) {
        sections.push(rail('Continue watching', home.resume, ctx, 'wide', { cardOptions: { wideEpisodeTitle: true } }));
      }
      if (home.nextUp?.length) {
        sections.push(rail('Next up', home.nextUp, ctx, 'wide', { cardOptions: { wideEpisodeTitle: true } }));
      }
      for (const latest of home.latest || []) {
        const shape = latest.collectionType === 'music' ? 'square' : shapeForItems(latest.items);
        sections.push(rail(`New in ${latest.name}`, latest.items, ctx, shape === 'wide' ? 'wide' : shape, {
          onTitle: () => ctx.go('library', { id: latest.libraryId, name: latest.name, collectionType: latest.collectionType }),
        }));
      }
      if (!sections.length) {
        sections.push(emptyState('Nothing here yet', 'Once the server has media, what you’re watching and what’s new shows up here.',
          button('Browse the library', { onClick: () => ctx.go('libraries') })));
      }
      node.append(h('div', { class: 'stack' }, sections));
    });
  };
  refresh();
  return {
    onShow() {
      if (Date.now() - loadedAt > 5 * 60 * 1000) refresh();
    },
    refresh,
  };
}

// ── Libraries ──────────────────────────────────────────────────────────────

const LIBRARY_ICONS = { movies: 'library', tvshows: 'osd', music: 'audio', playlists: 'queue', boxsets: 'library' };

export function librariesView(node, params, ctx) {
  ctx.setTitle('Library');
  load(node, skeletonGrid('wide', 4), () => get('/api/libraries'), (libraries) => {
    if (!libraries.length) {
      node.append(emptyState('No libraries', 'This user can’t see any libraries on the server.'));
      return;
    }
    node.append(h('div', { class: 'libraries' }, libraries.map((lib) => h('button', {
      class: 'library-tile', type: 'button',
      onClick: () => ctx.go('library', { id: lib.id, name: lib.name, collectionType: lib.type }),
    },
    h('span', { class: 'library-art' },
      icon(LIBRARY_ICONS[lib.type] || 'library', 'library-glyph'),
      artImage(lib.image, 360)),
    h('span', { class: 'library-name', text: lib.name })))));
  });
}

// ── Library ────────────────────────────────────────────────────────────────

const SORTS = {
  video: [
    { value: 'SortName', label: 'Name', order: 'Ascending', letters: true },
    { value: 'DateCreated,SortName', label: 'Date added', order: 'Descending' },
    { value: 'PremiereDate,ProductionYear,SortName', label: 'Release date', order: 'Descending' },
    { value: 'CommunityRating,SortName', label: 'Rating', order: 'Descending' },
    { value: 'DatePlayed,SortName', label: 'Last played', order: 'Descending' },
    { value: 'Runtime,SortName', label: 'Runtime', order: 'Ascending' },
  ],
  series: [
    { value: 'SortName', label: 'Name', order: 'Ascending', letters: true },
    { value: 'DateCreated,SortName', label: 'Date added', order: 'Descending' },
    { value: 'DateLastContentAdded,SortName', label: 'New episodes', order: 'Descending' },
    { value: 'PremiereDate,ProductionYear,SortName', label: 'First aired', order: 'Descending' },
    { value: 'CommunityRating,SortName', label: 'Rating', order: 'Descending' },
  ],
  albums: [
    { value: 'SortName', label: 'Name', order: 'Ascending', letters: true },
    { value: 'AlbumArtist,SortName', label: 'Artist', order: 'Ascending' },
    { value: 'ProductionYear,PremiereDate,SortName', label: 'Year', order: 'Descending' },
    { value: 'DateCreated,SortName', label: 'Date added', order: 'Descending' },
  ],
  songs: [
    { value: 'SortName', label: 'Name', order: 'Ascending', letters: true },
    { value: 'Album,ParentIndexNumber,IndexNumber', label: 'Album', order: 'Ascending' },
    { value: 'AlbumArtist,Album,ParentIndexNumber,IndexNumber', label: 'Artist', order: 'Ascending' },
    { value: 'DateCreated,SortName', label: 'Date added', order: 'Descending' },
    { value: 'PlayCount,SortName', label: 'Most played', order: 'Descending' },
  ],
  names: [
    { value: 'SortName', label: 'Name', order: 'Ascending', letters: true },
  ],
  collections: [
    { value: 'SortName', label: 'Name', order: 'Ascending', letters: true },
    { value: 'DateCreated,SortName', label: 'Date added', order: 'Descending' },
  ],
};

const WATCH_FILTERS = [
  { value: 'IsUnplayed', label: 'Unwatched' },
  { value: 'IsResumable', label: 'In progress' },
  { value: 'IsFavorite', label: 'Favourites' },
];
const MUSIC_FILTERS = [{ value: 'IsFavorite', label: 'Favourites' }];

function libraryTabs(collectionType) {
  switch (collectionType) {
    case 'movies':
      return [{ key: 'films', label: 'Films', types: ['Movie'], shape: 'poster', recursive: true, unit: ['film', 'films'], sorts: SORTS.video, filters: WATCH_FILTERS, playable: ['Movie'] }];
    case 'tvshows':
      return [{ key: 'shows', label: 'Shows', types: ['Series'], shape: 'poster', recursive: true, unit: ['show', 'shows'], sorts: SORTS.series, filters: WATCH_FILTERS, playable: ['Episode'] }];
    case 'music':
      return [
        { key: 'albums', label: 'Albums', types: ['MusicAlbum'], shape: 'square', recursive: true, unit: ['album', 'albums'], sorts: SORTS.albums, filters: MUSIC_FILTERS, playable: ['Audio'] },
        { key: 'artists', label: 'Artists', endpoint: '/api/artists', shape: 'round', unit: ['artist', 'artists'], sorts: SORTS.names, filters: MUSIC_FILTERS, playable: ['Audio'] },
        { key: 'songs', label: 'Songs', types: ['Audio'], layout: 'rows', recursive: true, unit: ['song', 'songs'], sorts: SORTS.songs, filters: MUSIC_FILTERS, playable: ['Audio'], pageSize: 100 },
      ];
    case 'musicvideos':
      return [{ key: 'videos', label: 'Videos', types: ['MusicVideo'], shape: 'wide', recursive: true, unit: ['video', 'videos'], sorts: SORTS.video, filters: WATCH_FILTERS, playable: ['MusicVideo'] }];
    case 'homevideos':
      return [{ key: 'videos', label: 'Videos', types: ['Video'], shape: 'wide', recursive: true, unit: ['video', 'videos'], sorts: SORTS.video, filters: WATCH_FILTERS, playable: ['Video'] }];
    case 'boxsets':
      return [{ key: 'collections', label: 'Collections', types: ['BoxSet'], shape: 'poster', recursive: true, unit: ['collection', 'collections'], sorts: SORTS.collections, filters: [WATCH_FILTERS[2]], playable: [] }];
    case 'playlists':
      return [{ key: 'playlists', label: 'Playlists', types: ['Playlist'], shape: 'square', recursive: true, unit: ['playlist', 'playlists'], sorts: SORTS.collections, filters: [], playable: [] }];
    default:
      return [{ key: 'folder', label: 'Everything', types: [], shape: 'poster', recursive: false, unit: ['item', 'items'], sorts: SORTS.collections, filters: WATCH_FILTERS, playable: [] }];
  }
}

function readPrefs(key) {
  try {
    return JSON.parse(localStorage.getItem(`strmqt_lib_${key}`) || '{}');
  } catch {
    return {};
  }
}

function writePrefs(key, value) {
  try {
    localStorage.setItem(`strmqt_lib_${key}`, JSON.stringify(value));
  } catch {
    // Preferences just won't stick.
  }
}

const LETTERS = ['#', ...'ABCDEFGHIJKLMNOPQRSTUVWXYZ'];

// A paged grid or row list over one query, with its own infinite scroll.
function pagedCollection(host, { endpoint = '/api/items', query, shape, layout, pageSize = 60, ctx, onTotal, emptyTitle, emptyDetail }) {
  let startIndex = 0;
  let total = Infinity;
  let loading = false;
  let items = [];
  let generation = 0;
  let observer = null;

  const container = layout === 'rows' ? h('ol', { class: 'rows' }) : h('div', { class: `grid grid-${shape}` });
  const sentinel = h('div', { class: 'sentinel', 'aria-hidden': 'true' });
  const status = h('div', { class: 'page-status' });
  host.replaceChildren(container, status, sentinel);

  const fetchPage = async () => {
    if (loading || startIndex >= total) return;
    loading = true;
    const mine = generation;
    if (!items.length) status.replaceChildren(skeletonGrid(shape === 'wide' ? 'wide' : shape, layout === 'rows' ? 0 : 9));
    try {
      const page = await get(endpoint, { ...query, startIndex, limit: pageSize });
      if (mine !== generation) return;
      status.replaceChildren();
      total = page.total;
      onTotal?.(total);
      const fresh = page.items || [];
      const offset = items.length;
      items = items.concat(fresh);
      startIndex += fresh.length;
      if (!fresh.length) total = startIndex;
      if (layout === 'rows') {
        container.append(...fresh.map((item, i) => songRow(item, offset + i, () => items, ctx)));
      } else {
        container.append(...fresh.map((item) => card(item, ctx, shape)));
      }
      if (!items.length) {
        status.replaceChildren(emptyState(emptyTitle || 'Nothing matches', emptyDetail || 'Try another filter or letter.'));
      }
    } catch (error) {
      if (mine !== generation) return;
      status.replaceChildren(errorState(error, () => { loading = false; fetchPage(); }));
    } finally {
      if (mine === generation) loading = false;
    }
    // A short first page may not reach the sentinel; keep going until it does.
    requestAnimationFrame(() => {
      if (mine === generation && startIndex < total && sentinel.getBoundingClientRect().top < window.innerHeight + 900) fetchPage();
    });
  };

  observer = new IntersectionObserver((entries) => {
    if (entries.some((e) => e.isIntersecting)) fetchPage();
  }, { rootMargin: '900px 0px' });
  observer.observe(sentinel);
  fetchPage();

  return {
    dispose() {
      generation += 1;
      observer?.disconnect();
    },
    items: () => items,
  };
}

function songRow(item, index, allItems, ctx) {
  const artist = (item.artists || [])[0] || item.albumArtist || '';
  return h('li', { class: 'row' },
    h('button', {
      class: 'row-main', type: 'button',
      onClick: () => {
        const list = allItems();
        const start = Math.max(0, index - 100);
        play(ctx, { mode: 'list', items: listPayload(list.slice(start, start + 300)), startIndex: index - start });
      },
    },
    h('span', { class: 'row-art' }, h('span', { class: 'card-fallback', text: initials(item.name) }), artImage(pickImage(item, 'square'), 48)),
    h('span', { class: 'row-text' },
      h('span', { class: 'row-title', text: item.name }),
      h('span', { class: 'row-sub', text: [artist, item.album].filter(Boolean).join(', ') })),
    item.runtimeMs ? h('span', { class: 'row-time mono', text: clock(item.runtimeMs) }) : null),
    iconButton('more', `More for ${item.name}`, () => itemMenu(item, ctx)));
}

function itemMenu(item, ctx) {
  openSheet({
    title: item.name,
    subtitle: cardText(item, 'square')[1],
    build(body, sheet) {
      const act = (fn) => async () => {
        sheet.close();
        await fn();
      };
      const actions = [
        { iconName: 'play', label: 'Play', run: () => play(ctx, { mode: 'play', itemId: item.id }) },
        { iconName: 'play-next', label: 'Play next', run: () => play(ctx, { mode: 'next', itemId: item.id }) },
        { iconName: 'plus', label: 'Add to queue', run: () => play(ctx, { mode: 'queue', itemId: item.id }) },
      ];
      if (item.type === 'Audio' || item.type === 'MusicAlbum' || item.type === 'MusicArtist') {
        actions.push({ iconName: 'mix', label: 'Instant mix', run: () => play(ctx, { mode: 'instantMix', itemId: item.id }) });
      }
      if (item.albumId) actions.push({ iconName: 'library', label: 'Go to album', run: () => ctx.openItem({ id: item.albumId, name: item.album }) });
      if (item.seriesId) actions.push({ iconName: 'osd', label: 'Go to series', run: () => ctx.openItem({ id: item.seriesId, name: item.seriesName }) });
      actions.push({ iconName: 'right', label: 'Details', run: () => ctx.openItem(item) });
      body.append(h('div', { class: 'action-list' }, actions.map((a) => h('button', { class: 'action', type: 'button', onClick: act(a.run) },
        icon(a.iconName, 'action-icon'), h('span', { class: 'action-text' }, h('span', { class: 'action-label', text: a.label }))))));
    },
  });
}

export function libraryView(node, params, ctx) {
  const tabs = libraryTabs(params.collectionType);
  const prefs = readPrefs(params.id);
  let tab = tabs.find((t) => t.key === prefs.tab) || tabs[0];
  const state = {
    sort: prefs.sort?.[tab.key] || tab.sorts[0].value,
    filters: [],
    letter: '',
  };
  let collection = null;
  ctx.setTitle(params.name || 'Library');

  const title = h('h2', { class: 'page-title', text: params.name || 'Library' });
  const totalLine = h('p', { class: 'page-count' });
  const tabBar = tabs.length > 1 ? h('div', { class: 'segmented page-tabs', role: 'tablist' }) : null;
  const controls = h('div', { class: 'controls' });
  const letters = h('div', { class: 'letters', role: 'group', 'aria-label': 'Jump to letter' });
  const results = h('div', { class: 'results' });

  const sortOption = () => tab.sorts.find((s) => s.value === state.sort) || tab.sorts[0];

  const savePrefs = () => {
    const sorts = { ...(readPrefs(params.id).sort || {}), [tab.key]: state.sort };
    writePrefs(params.id, { tab: tab.key, sort: sorts });
  };

  const buildQuery = () => {
    const sort = sortOption();
    const query = {
      parentId: params.id,
      sortBy: sort.value,
      sortOrder: sort.order,
      filters: state.filters,
      letter: sort.letters ? state.letter : '',
    };
    if (tab.types?.length) query.types = tab.types;
    if (tab.recursive) query.recursive = 'true';
    return query;
  };

  const renderTabs = () => {
    if (!tabBar) return;
    tabBar.replaceChildren(...tabs.map((t) => h('button', {
      class: `segment${t === tab ? ' selected' : ''}`, type: 'button', role: 'tab', 'aria-selected': t === tab ? 'true' : 'false', text: t.label,
      onClick: () => {
        if (t === tab) return;
        tab = t;
        state.sort = readPrefs(params.id).sort?.[tab.key] || tab.sorts[0].value;
        state.filters = state.filters.filter((f) => tab.filters.some((tf) => tf.value === f));
        state.letter = '';
        savePrefs();
        refresh();
      },
    })));
  };

  const renderControls = () => {
    const sort = sortOption();
    controls.replaceChildren(
      tab.sorts.length > 1 ? h('button', {
        class: 'chip', type: 'button', 'aria-haspopup': 'dialog',
        onClick: () => openSheet({
          title: 'Sort by',
          build(body, sheet) {
            body.append(choiceList(tab.sorts.map((s) => ({ value: s.value, label: s.label })), {
              current: state.sort,
              onPick: (value) => {
                state.sort = value;
                if (!tab.sorts.find((s) => s.value === value)?.letters) state.letter = '';
                savePrefs();
                sheet.close();
                refresh();
              },
            }));
          },
        }),
      }, icon('quality'), h('span', { text: sort.label })) : null,
      ...tab.filters.map((f) => {
        const on = state.filters.includes(f.value);
        return h('button', {
          class: `chip${on ? ' on' : ''}`, type: 'button', 'aria-pressed': on ? 'true' : 'false', text: f.label,
          onClick: () => {
            haptic();
            state.filters = on ? state.filters.filter((x) => x !== f.value) : [...state.filters, f.value];
            refresh();
          },
        });
      }),
      tab.playable.length || tab.key === 'folder' ? h('button', {
        class: 'chip chip-accent', type: 'button',
        onClick: () => shuffleLibrary(),
      }, icon('shuffle'), h('span', { text: 'Shuffle' })) : null);

    letters.hidden = !sort.letters;
    letters.replaceChildren(...LETTERS.map((letter) => h('button', {
      class: `letter${state.letter === letter ? ' on' : ''}`, type: 'button', text: letter,
      'aria-pressed': state.letter === letter ? 'true' : 'false',
      'aria-label': letter === '#' ? 'Numbers and symbols' : `Starts with ${letter}`,
      onClick: () => {
        haptic(5);
        state.letter = state.letter === letter ? '' : letter;
        refresh();
      },
    })));
  };

  const shuffleLibrary = () => {
    const narrowed = state.filters.length || state.letter;
    if (!narrowed) {
      play(ctx, { mode: 'shuffle', itemId: params.id, collectionType: params.collectionType || '' });
      return;
    }
    const query = { parentId: params.id, recursive: true, filters: state.filters.join(','), letter: state.letter };
    if (tab.playable.length) query.types = tab.playable.join(',');
    play(ctx, { mode: 'shuffleQuery', query });
  };

  const refresh = () => {
    collection?.dispose();
    renderTabs();
    renderControls();
    const narrowed = state.filters.length || state.letter;
    totalLine.textContent = '';
    collection = pagedCollection(results, {
      endpoint: tab.endpoint || '/api/items',
      query: buildQuery(),
      shape: tab.shape,
      layout: tab.layout,
      pageSize: tab.pageSize,
      ctx,
      onTotal: (total) => {
        totalLine.textContent = `${count(total, tab.unit[0], tab.unit[1])}${narrowed ? ' match' : ''}`;
      },
      emptyTitle: narrowed ? 'Nothing matches' : 'This library is empty',
      emptyDetail: narrowed ? 'Turn off a filter or pick another letter.' : '',
    });
  };

  append(node, [
    h('div', { class: 'page-head' }, title, totalLine),
    tabBar,
    h('div', { class: 'toolbar' }, controls, letters),
    results]);
  refresh();

  return { dispose: () => collection?.dispose() };
}

// A grid over any query: a genre, a person, an artist's songs.
export function listView(node, params, ctx) {
  ctx.setTitle(params.title || '');
  const query = params.query || {};
  const totalLine = h('p', { class: 'page-count' });
  const results = h('div', { class: 'results' });
  const shuffleTypes = params.playable || ['Movie', 'Episode', 'Audio', 'MusicVideo', 'Video'];
  node.append(
    h('div', { class: 'page-head' },
      params.eyebrow ? h('p', { class: 'page-kicker', text: params.eyebrow }) : null,
      h('h2', { class: 'page-title', text: params.title || '' }),
      totalLine),
    h('div', { class: 'toolbar' }, h('div', { class: 'controls' },
      h('button', {
        class: 'chip chip-accent', type: 'button',
        onClick: () => play(ctx, { mode: 'shuffleQuery', query: { ...query, recursive: true, types: shuffleTypes.join(',') } }),
      }, icon('shuffle'), h('span', { text: 'Shuffle' })))),
    results);
  const collection = pagedCollection(results, {
    query: { recursive: 'true', sortBy: 'SortName', ...query },
    shape: params.shape || 'poster',
    layout: params.layout,
    ctx,
    onTotal: (total) => { totalLine.textContent = count(total, 'title', 'titles'); },
  });
  return { dispose: () => collection.dispose() };
}

// ── Details ────────────────────────────────────────────────────────────────

const VIDEO_TYPES = new Set(['Movie', 'Episode', 'Video', 'MusicVideo', 'Trailer']);

export function itemView(node, params, ctx) {
  ctx.setTitle(params.name || '');
  const render = (item) => {
    ctx.setTitle(item.name);
    node.append(detailPage(item, ctx));
  };
  load(node, h('div', { class: 'detail-skeleton skeleton' }, h('span', { class: 'sk-hero' }), h('span', { class: 'sk-line wide' }), h('span', { class: 'sk-line' })),
    () => get(`/api/item/${encodeURIComponent(params.id)}`), render);
}

function detailPage(item, ctx) {
  const square = ['MusicAlbum', 'MusicArtist', 'Playlist', 'Audio'].includes(item.type);
  const images = item.images || {};
  const heroRef = square ? images.cover : images.backdrop || images.thumb || images.cover;
  const hero = h('div', { class: `hero${square ? ' hero-square' : ''}` },
    square ? null : h('span', { class: 'hero-fade' }),
    heroRef ? artImage(heroRef, square ? 280 : 480) : h('span', { class: 'card-fallback', text: initials(item.name) }));

  const page = h('article', { class: `detail detail-${item.type}${square ? ' detail-square' : ''}` }, hero);
  const titleBlock = h('div', { class: 'detail-head' });

  if (item.type === 'Episode' && item.seriesName) {
    titleBlock.append(h('button', {
      class: 'kicker-link', type: 'button', onClick: () => ctx.openItem({ id: item.seriesId, name: item.seriesName }),
    }, h('span', { text: item.seriesName }), icon('right')));
  }
  if (item.type === 'Season' && item.seriesName) {
    titleBlock.append(h('button', {
      class: 'kicker-link', type: 'button', onClick: () => ctx.openItem({ id: item.seriesId, name: item.seriesName }),
    }, h('span', { text: item.seriesName }), icon('right')));
  }
  if ((item.type === 'MusicAlbum' || item.type === 'Audio') && (item.albumArtist || item.artists?.length)) {
    const artistId = item.artistIds?.[0];
    const artistName = item.albumArtist || item.artists[0];
    titleBlock.append(artistId
      ? h('button', { class: 'kicker-link', type: 'button', onClick: () => ctx.openItem({ id: artistId, name: artistName }) }, h('span', { text: artistName }), icon('right'))
      : h('p', { class: 'page-kicker', text: artistName }));
  }

  titleBlock.append(h('h2', { class: 'detail-title', text: item.name }));
  if (item.tagline) titleBlock.append(h('p', { class: 'tagline', text: item.tagline }));

  const rating = item.officialRating ? h('span', { class: 'rating-box', text: item.officialRating }) : null;
  const metaParts = [];
  if (item.type === 'Episode') metaParts.push(episodeCode(item));
  if (item.year) metaParts.push(String(item.year));
  metaParts.push(rating);
  if (item.runtimeMs && VIDEO_TYPES.has(item.type)) metaParts.push(duration(item.runtimeMs));
  if (item.communityRating) metaParts.push(`${item.communityRating.toFixed(1)} rating`);
  if (item.type === 'Series' && item.status === 'Continuing') metaParts.push('Still airing');
  if (item.childCount && (item.type === 'MusicAlbum' || item.type === 'Playlist' || item.type === 'BoxSet')) {
    metaParts.push(count(item.childCount, item.type === 'MusicAlbum' ? 'track' : 'item', item.type === 'MusicAlbum' ? 'tracks' : 'items'));
  }
  titleBlock.append(metaLine(metaParts));
  if (item.runtimeMs && VIDEO_TYPES.has(item.type) && !item.played) {
    const left = item.runtimeMs - (item.positionMs || 0);
    titleBlock.append(h('p', { class: 'meta-quiet', text: item.positionMs ? `${duration(left)} left, ends ${endsAt(left)}` : `Ends ${endsAt(left)}` }));
  }
  if (item.positionMs && !item.played) titleBlock.append(progressBar({ playedPercentage: item.playedPercentage || (item.positionMs / item.runtimeMs) * 100 }));
  page.append(titleBlock, detailActions(item, ctx));

  if (item.overview) page.append(overview(item.overview));

  const children = h('div', { class: 'detail-children' });
  page.append(children);
  switch (item.type) {
    case 'Series':
      seasonsSection(children, item, ctx);
      break;
    case 'Season':
      episodesSection(children, item.seriesId, item.id, ctx);
      break;
    case 'MusicAlbum':
      tracksSection(children, item, ctx);
      break;
    case 'MusicArtist':
      artistSection(children, item, ctx);
      break;
    case 'Playlist':
      playlistSection(children, item, ctx);
      break;
    case 'BoxSet':
    case 'Folder':
    case 'CollectionFolder':
      childrenSection(children, item, ctx);
      break;
    default:
      break;
  }

  if (item.genres?.length) {
    page.append(h('section', { class: 'detail-section' },
      sectionHeading('Genres'),
      h('div', { class: 'chips-wrap' }, item.genres.map((g) => h('button', {
        class: 'chip', type: 'button', text: g.name,
        onClick: () => ctx.go('list', {
          title: g.name, eyebrow: 'Genre',
          query: { genreIds: g.id, types: square ? 'MusicAlbum' : 'Movie,Series' },
          shape: square ? 'square' : 'poster',
          playable: square ? ['Audio'] : ['Movie', 'Episode'],
        }),
      })))));
  }

  if (item.mediaSources?.length && VIDEO_TYPES.has(item.type)) {
    page.append(h('section', { class: 'detail-section' },
      sectionHeading(item.mediaSources.length > 1 ? 'Versions' : 'File'),
      h('ul', { class: 'versions' }, item.mediaSources.map((s) => h('li', { class: 'version' },
        h('span', { class: 'version-name', text: s.name || `Version ${s.index + 1}` }),
        h('span', { class: 'version-facts mono', text: [s.resolution, s.isHdr ? 'HDR' : '', s.container?.toUpperCase(), bitrate(s.bitrate), fileSize(s.size)].filter(Boolean).join('  ') }),
        h('span', { class: 'version-tracks', text: [s.audioCount ? count(s.audioCount, 'audio track', 'audio tracks') : '', s.subtitleCount ? count(s.subtitleCount, 'subtitle', 'subtitles') : ''].filter(Boolean).join(', ') }))))));
  }

  const people = (item.people || []).filter((p) => p.name);
  if (people.length) {
    page.append(h('section', { class: 'detail-section' },
      sectionHeading('Cast and crew'),
      h('div', { class: 'rail rail-people' }, people.map((person) => h('button', {
        class: 'person', type: 'button',
        onClick: () => ctx.go('list', {
          title: person.name, eyebrow: person.type === 'Actor' ? 'Appears in' : person.type || 'Person',
          query: { personIds: person.id, types: 'Movie,Series' },
          playable: ['Movie', 'Episode'],
        }),
      },
      h('span', { class: 'person-art' },
        h('span', { class: 'card-fallback', text: initials(person.name) }),
        person.primaryImageTag ? artImage({ itemId: person.id, type: 'Primary', tag: person.primaryImageTag }, 96) : null),
      h('span', { class: 'person-name', text: person.name }),
      person.role || person.type ? h('span', { class: 'person-role', text: person.role || person.type }) : null)))));
  }

  return page;
}

function overview(text) {
  const body = h('p', { class: 'overview clamped', text });
  const more = h('button', { class: 'text-btn', type: 'button', text: 'More', hidden: true });
  more.addEventListener('click', () => {
    const clamped = body.classList.toggle('clamped');
    more.textContent = clamped ? 'More' : 'Less';
  });
  requestAnimationFrame(() => {
    more.hidden = body.scrollHeight <= body.clientHeight + 2;
  });
  return h('div', { class: 'overview-block' }, body, more);
}

function detailActions(item, ctx) {
  const leaf = VIDEO_TYPES.has(item.type) || item.type === 'Audio';
  const primary = [];
  if (leaf && item.resumable) {
    primary.push(button(`Resume from ${clock(item.positionMs, item.positionMs >= 3600000)}`, { iconName: 'play', kind: 'primary', onClick: () => play(ctx, { mode: 'resume', itemId: item.id }) }));
    primary.push(button('From the start', { iconName: 'reload', onClick: () => play(ctx, { mode: 'fromStart', itemId: item.id }) }));
  } else {
    primary.push(button('Play', { iconName: 'play', kind: 'primary', onClick: () => play(ctx, { mode: 'play', itemId: item.id }) }));
  }

  if (item.type === 'Series') {
    primary.push(button('Shuffle', { iconName: 'shuffle', onClick: () => play(ctx, { mode: 'shuffle', itemId: item.id, type: 'Series' }) }));
  } else if (item.type === 'Season' || item.type === 'BoxSet' || item.type === 'Playlist' || item.type === 'MusicAlbum' || item.type === 'Folder') {
    primary.push(button('Shuffle', { iconName: 'shuffle', onClick: () => play(ctx, { mode: 'shuffle', itemId: item.id, collectionType: item.type === 'Season' ? 'tvshows' : item.type === 'MusicAlbum' ? 'music' : '' }) }));
  } else if (item.type === 'MusicArtist') {
    primary.push(button('Shuffle', { iconName: 'shuffle', onClick: () => play(ctx, { mode: 'shuffleQuery', query: { artistIds: item.id, types: 'Audio', recursive: true } }) }));
  }

  const secondary = [];
  if (item.type === 'MusicAlbum' || item.type === 'MusicArtist' || item.type === 'Audio') {
    secondary.push(toolButton('mix', 'Instant mix', () => play(ctx, { mode: 'instantMix', itemId: item.id })));
  }
  if (item.type !== 'MusicArtist') {
    secondary.push(toolButton('play-next', 'Play next', () => play(ctx, { mode: 'next', itemId: item.id })));
    secondary.push(toolButton('plus', 'Add to queue', () => play(ctx, { mode: 'queue', itemId: item.id })));
  }
  secondary.push(toggleButton(item, 'favorite', 'heart', 'Favourite', 'Favourite'));
  if (item.type !== 'MusicArtist' && item.type !== 'Playlist' && item.type !== 'Audio' && item.type !== 'MusicAlbum') {
    secondary.push(toggleButton(item, 'played', 'played', item.type === 'Series' || item.type === 'Season' ? 'Mark watched' : 'Watched', 'Watched'));
  }

  return h('div', { class: 'detail-actions' },
    h('div', { class: 'primary-actions' }, primary),
    h('div', { class: 'tool-actions' }, secondary));
}

function toolButton(iconName, label, onClick) {
  return h('button', { class: 'tool', type: 'button', onClick }, icon(iconName), h('span', { text: label }));
}

function toggleButton(item, field, iconName, offLabel, onLabel) {
  let value = !!item[field];
  const labelNode = h('span');
  const node = h('button', { class: 'tool', type: 'button' }, icon(iconName), labelNode);
  const paint = () => {
    node.classList.toggle('on', value);
    node.setAttribute('aria-pressed', value ? 'true' : 'false');
    labelNode.textContent = value ? onLabel : offLabel;
  };
  paint();
  node.addEventListener('click', async () => {
    haptic();
    value = !value;
    paint();
    const ok = await run(post(`/api/item/${encodeURIComponent(item.id)}/${field}`, { value }));
    if (ok === undefined) {
      value = !value;
      paint();
    }
  });
  return node;
}

function seasonsSection(host, series, ctx) {
  const section = h('section', { class: 'detail-section' }, sectionHeading('Seasons'));
  const body = h('div');
  section.append(body);
  host.append(section);
  load(body, skeletonGrid('poster', 3), () => get(`/api/item/${encodeURIComponent(series.id)}/seasons`), (page) => {
    const seasons = page.items || [];
    if (!seasons.length) {
      body.append(emptyState('No seasons', 'The server has no episodes for this show.'));
      return;
    }
    if (seasons.length === 1) {
      section.querySelector('.section-title').textContent = seasons[0].name;
      episodesSection(body, series.id, seasons[0].id, ctx, { embedded: true });
      return;
    }
    body.append(h('div', { class: 'rail rail-poster' }, seasons.map((season) => card(season, ctx, 'poster'))));
  });
}

function episodesSection(host, seriesId, seasonId, ctx, { embedded = false } = {}) {
  const body = h('div');
  host.append(embedded ? body : h('section', { class: 'detail-section' }, sectionHeading('Episodes'), body));
  load(body, skeletonGrid('wide', 2), () => get(`/api/item/${encodeURIComponent(seriesId)}/episodes`, { seasonId }), (page) => {
    const episodes = page.items || [];
    if (!episodes.length) {
      body.append(emptyState('No episodes', 'This season is empty on the server.'));
      return;
    }
    body.append(h('ol', { class: 'episodes' }, episodes.map((episode, index) => h('li', { class: `episode${episode.played ? ' played' : ''}` },
      h('button', { class: 'episode-main', type: 'button', onClick: () => ctx.openItem(episode) },
        h('span', { class: 'episode-art' },
          h('span', { class: 'card-fallback', text: episode.indexNumber !== undefined ? String(episode.indexNumber) : '' }),
          artImage(pickImage(episode, 'wide'), 150),
          progressBar(episode),
          episode.played ? h('span', { class: 'badge badge-played' }, icon('check')) : null),
        h('span', { class: 'episode-text' },
          h('span', { class: 'episode-title', text: episode.indexNumber !== undefined ? `${episode.indexNumber}. ${episode.name}` : episode.name }),
          h('span', { class: 'episode-sub', text: [episode.runtimeMs ? duration(episode.runtimeMs) : '', episode.premiereDate ? new Date(episode.premiereDate).toLocaleDateString([], { year: 'numeric', month: 'short', day: 'numeric' }) : ''].filter(Boolean).join(', ') }))),
      h('button', {
        class: 'icon-btn episode-play', type: 'button', 'aria-label': `Play ${episode.name}`,
        onClick: () => play(ctx, { mode: 'list', items: listPayload(episodes), startIndex: index }),
      }, icon('play'))))));
  });
}

function tracksSection(host, album, ctx) {
  const body = h('div');
  host.append(h('section', { class: 'detail-section' }, body));
  load(body, h('div', { class: 'skeleton rows-skeleton' }), () => get('/api/items', {
    parentId: album.id, types: 'Audio', recursive: 'true', sortBy: 'ParentIndexNumber,IndexNumber,SortName', limit: 200,
  }), (page) => {
    const tracks = page.items || [];
    if (!tracks.length) {
      body.append(emptyState('No tracks', 'This album is empty on the server.'));
      return;
    }
    const discs = new Set(tracks.map((t) => t.parentIndexNumber ?? 1));
    const list = h('ol', { class: 'tracks' });
    let lastDisc = null;
    tracks.forEach((track, index) => {
      const disc = track.parentIndexNumber ?? 1;
      if (discs.size > 1 && disc !== lastDisc) {
        list.append(h('li', { class: 'disc', text: `Disc ${disc}` }));
        lastDisc = disc;
      }
      const artist = (track.artists || []).join(', ');
      list.append(h('li', { class: 'row' },
        h('button', { class: 'row-main', type: 'button', onClick: () => play(ctx, { mode: 'list', items: listPayload(tracks), startIndex: index }) },
          h('span', { class: 'track-no mono', text: track.indexNumber !== undefined ? String(track.indexNumber) : '' }),
          h('span', { class: 'row-text' },
            h('span', { class: 'row-title', text: track.name }),
            artist && artist !== album.albumArtist ? h('span', { class: 'row-sub', text: artist }) : null),
          track.runtimeMs ? h('span', { class: 'row-time mono', text: clock(track.runtimeMs) }) : null),
        iconButton('more', `More for ${track.name}`, () => itemMenu(track, ctx))));
    });
    body.append(list);
  });
}

function artistSection(host, artist, ctx) {
  const albums = h('div');
  host.append(h('section', { class: 'detail-section' },
    h('div', { class: 'section-head' },
      h('h2', { class: 'section-title', text: 'Albums' }),
      h('button', {
        class: 'text-btn', type: 'button', text: 'All songs',
        onClick: () => ctx.go('list', {
          title: artist.name, eyebrow: 'Songs by',
          query: { artistIds: artist.id, types: 'Audio', sortBy: 'Album,ParentIndexNumber,IndexNumber' },
          layout: 'rows', shape: 'square', playable: ['Audio'],
        }),
      })),
    albums));
  load(albums, skeletonGrid('square', 4), () => get('/api/items', {
    albumArtistIds: artist.id, types: 'MusicAlbum', recursive: 'true', sortBy: 'ProductionYear,SortName', sortOrder: 'Descending', limit: 200,
  }), (page) => {
    const items = page.items || [];
    albums.append(items.length
      ? h('div', { class: 'grid grid-square' }, items.map((album) => card(album, ctx, 'square')))
      : emptyState('No albums', 'This artist only appears on other people’s records.'));
  });
}

function playlistSection(host, playlist, ctx) {
  const body = h('div');
  host.append(h('section', { class: 'detail-section' }, body));
  load(body, h('div', { class: 'skeleton rows-skeleton' }), () => get(`/api/item/${encodeURIComponent(playlist.id)}/playlist`), (page) => {
    const items = page.items || [];
    if (!items.length) {
      body.append(emptyState('This playlist is empty', 'Add to it from the desktop app.'));
      return;
    }
    body.append(h('ol', { class: 'rows' }, items.map((item, index) => h('li', { class: 'row' },
      h('button', { class: 'row-main', type: 'button', onClick: () => play(ctx, { mode: 'list', items: listPayload(items), startIndex: index }) },
        h('span', { class: 'row-art' }, h('span', { class: 'card-fallback', text: initials(item.name) }), artImage(pickImage(item, item.type === 'Audio' ? 'square' : 'wide'), 48)),
        h('span', { class: 'row-text' },
          h('span', { class: 'row-title', text: item.name }),
          h('span', { class: 'row-sub', text: cardText(item, 'square')[1] })),
        item.runtimeMs ? h('span', { class: 'row-time mono', text: clock(item.runtimeMs, item.runtimeMs >= 3600000) }) : null),
      iconButton('more', `More for ${item.name}`, () => itemMenu(item, ctx))))));
  });
}

function childrenSection(host, parent, ctx) {
  const body = h('div');
  host.append(h('section', { class: 'detail-section' }, sectionHeading(parent.type === 'BoxSet' ? 'In this collection' : 'Contents'), body));
  const collection = pagedCollection(body, {
    query: { parentId: parent.id, sortBy: parent.type === 'BoxSet' ? 'PremiereDate,ProductionYear,SortName' : 'IsFolder,SortName' },
    shape: 'poster',
    ctx,
    emptyTitle: 'Nothing in here',
  });
  return collection;
}

// ── Search ─────────────────────────────────────────────────────────────────

const SEARCH_GROUPS = [
  { types: ['Movie'], title: 'Films', shape: 'poster' },
  { types: ['Series'], title: 'Shows', shape: 'poster' },
  { types: ['Episode'], title: 'Episodes', shape: 'wide' },
  { types: ['MusicArtist'], title: 'Artists', shape: 'round' },
  { types: ['MusicAlbum'], title: 'Albums', shape: 'square' },
  { types: ['Audio'], title: 'Songs', layout: 'rows' },
  { types: ['BoxSet'], title: 'Collections', shape: 'poster' },
  { types: ['Playlist'], title: 'Playlists', shape: 'square' },
  { types: ['Video', 'MusicVideo'], title: 'Videos', shape: 'wide' },
];

function recentSearches() {
  try {
    return JSON.parse(localStorage.getItem('strmqt_recent_searches') || '[]');
  } catch {
    return [];
  }
}

function rememberSearch(term) {
  const next = [term, ...recentSearches().filter((t) => t.toLowerCase() !== term.toLowerCase())].slice(0, 8);
  try {
    localStorage.setItem('strmqt_recent_searches', JSON.stringify(next));
  } catch {
    // Not remembered.
  }
}

export function searchView(node, params, ctx) {
  ctx.setTitle('Search');
  const input = h('input', {
    class: 'search-input', type: 'search', placeholder: 'Films, shows, music, people', 'aria-label': 'Search the library',
    enterkeyhint: 'search', autocomplete: 'off', autocapitalize: 'off', spellcheck: 'false',
  });
  const clear = iconButton('close', 'Clear search', () => {
    input.value = '';
    input.focus();
    update();
  }, 'search-clear');
  const results = h('div', { class: 'search-results', 'aria-live': 'polite' });
  let controller = null;
  let lastTerm = null;

  const showRecent = () => {
    const recent = recentSearches();
    results.replaceChildren(recent.length
      ? h('section', { class: 'detail-section' }, sectionHeading('Recent'),
        h('div', { class: 'chips-wrap' }, recent.map((term) => h('button', {
          class: 'chip', type: 'button', text: term,
          onClick: () => {
            input.value = term;
            update();
          },
        }))))
      : emptyState('Search your server', 'Results are grouped into films, shows, episodes and music as you type.'));
  };

  const update = async () => {
    const term = input.value.trim();
    clear.hidden = !input.value;
    if (term === lastTerm) return;
    lastTerm = term;
    controller?.abort();
    if (term.length < 2) {
      showRecent();
      return;
    }
    controller = new AbortController();
    const signal = controller.signal;
    results.classList.add('loading');
    try {
      const page = await get('/api/search', { q: term }, signal);
      if (signal.aborted) return;
      const items = page.items || [];
      const groups = SEARCH_GROUPS
        .map((group) => ({ ...group, items: items.filter((item) => group.types.includes(item.type)) }))
        .filter((group) => group.items.length);
      if (!groups.length) {
        results.replaceChildren(emptyState(`Nothing called “${term}”`, 'Check the spelling, or try part of the name.'));
        return;
      }
      rememberSearch(term);
      results.replaceChildren(...groups.map((group) => {
        if (group.layout === 'rows') {
          return h('section', { class: 'detail-section' }, sectionHeading(group.title),
            h('ol', { class: 'rows' }, group.items.map((item, index) => songRow(item, index, () => group.items, ctx))));
        }
        if (group.shape === 'wide') {
          return rail(group.title, group.items, ctx, 'wide', { cardOptions: { wideEpisodeTitle: true } });
        }
        return rail(group.title, group.items, ctx, group.shape);
      }));
    } catch (error) {
      if (error.name === 'AbortError') return;
      results.replaceChildren(errorState(error, () => { lastTerm = null; update(); }));
    } finally {
      results.classList.remove('loading');
    }
  };

  const debounced = debounce(update, 280);
  input.addEventListener('input', () => {
    clear.hidden = !input.value;
    debounced();
  });
  input.addEventListener('keydown', (event) => {
    if (event.key === 'Enter') {
      input.blur();
      update();
    }
  });

  node.append(h('div', { class: 'search-bar' }, icon('search', 'search-icon'), input, clear), results);
  clear.hidden = true;
  if (params.q) input.value = params.q;
  update();

  return {
    onShow() {
      if (!input.value) requestAnimationFrame(() => input.focus({ preventScroll: true }));
    },
  };
}
