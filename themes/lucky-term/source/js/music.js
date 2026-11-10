/* 全站音乐播放器。歌单来自 source/_data/music.yml；不自动播放、不依赖第三方服务。 */
(() => {
  'use strict';
  const root = document.getElementById('music');
  if (!root) return;
  const $ = selector => root.querySelector(selector);
  const audio = $('[data-music-audio]');
  const panel = $('#music-panel');
  const toggle = $('[data-music-toggle]');
  const seek = $('[data-music-seek]');
  const volume = $('[data-music-volume]');
  const play = $('[data-music-play]');
  const loop = $('[data-music-loop]');
  const mute = $('[data-music-mute]');
  const status = $('[data-music-status]');
  let tracks;
  try { tracks = JSON.parse(document.getElementById('music-data').textContent); }
  catch (_) { return; }
  let saved = {};
  try { saved = JSON.parse(localStorage.getItem('lt-music') || '{}') || {}; } catch (_) {}
  const previous = tracks.findIndex(t => t.src === saved.src);
  let index = Math.max(0, previous);
  let resume = previous >= 0 && Number.isFinite(saved.time) ? Math.max(0, saved.time) : 0;
  let single = saved.single === true;
  let loaded = false;
  let request = 0;
  let lastSave = 0;
  audio.volume = Number.isFinite(saved.volume) ? Math.min(1, Math.max(0, saved.volume)) : 0.65;
  audio.muted = saved.muted === true;

  const format = value => {
    const seconds = Number.isFinite(value) ? Math.max(0, Math.floor(value)) : 0;
    return `${Math.floor(seconds / 60)}:${String(seconds % 60).padStart(2, '0')}`;
  };
  function save() {
    try {
      localStorage.setItem('lt-music', JSON.stringify({ src: tracks[index]?.src,
        time: loaded ? audio.currentTime : resume, volume: audio.volume, muted: audio.muted, single }));
    } catch (_) { /* 隐私模式或存储空间已满仍可播放 */ }
    lastSave = Date.now();
  }
  function message(text = '') { status.textContent = text; status.hidden = !text; }
  function setOpen(open, returnFocus = false) {
    panel.hidden = !open;
    toggle.setAttribute('aria-expanded', String(open));
    toggle.setAttribute('aria-label', `${open ? '收起' : '展开'}音乐播放器`);
    if (open) $('[data-music-close]').focus({ preventScroll: true });
    else if (returnFocus) toggle.focus({ preventScroll: true });
  }
  function renderPlaying() {
    const playing = !audio.paused && !audio.ended;
    root.classList.toggle('is-playing', playing);
    play.setAttribute('aria-label', playing ? '暂停' : '播放');
    $('[data-music-play-icon]').setAttribute('d', playing ? 'M6 5h4v14H6zm8 0h4v14h-4z' : 'm9 5 11 7-11 7z');
  }
  function renderVolume() {
    volume.value = audio.volume;
    mute.setAttribute('aria-pressed', String(audio.muted));
    mute.setAttribute('aria-label', audio.muted ? '取消静音' : '静音');
  }
  function renderLoop() {
    loop.setAttribute('aria-pressed', String(single));
    loop.title = single ? '单曲循环' : '列表循环';
    loop.setAttribute('aria-label', single ? '切换为列表循环' : '切换为单曲循环');
  }
  function renderTime() {
    $('[data-music-time]').textContent = format(loaded ? audio.currentTime : resume);
    $('[data-music-duration]').textContent = format(audio.duration);
    seek.disabled = !Number.isFinite(audio.duration) || audio.duration <= 0;
    seek.value = seek.disabled ? 0 : Math.round(audio.currentTime / audio.duration * 1000);
    seek.setAttribute('aria-valuetext', `${format(audio.currentTime)} / ${format(audio.duration)}`);
  }
  function renderTrack() {
    const track = tracks[index];
    $('[data-music-title]').textContent = track.title;
    $('[data-music-artist]').textContent = track.artist || '未知歌手';
    $('[data-music-caption]').textContent = track.title;
    toggle.title = track.artist ? `${track.title} · ${track.artist}` : track.title;
    const credit = $('[data-music-credit]');
    if (credit) {
      const sourceLink = $('[data-music-source]');
      const licenseLink = $('[data-music-license]');
      credit.hidden = !track.source;
      sourceLink.href = track.source || '';
      licenseLink.hidden = !track.license || !track.licenseUrl;
      licenseLink.href = track.licenseUrl || '';
      licenseLink.textContent = track.license || '';
    }
    root.querySelectorAll('[data-music-art]').forEach(art => {
      const img = art.querySelector('img');
      img.hidden = true;
      if (track.cover) {
        img.onload = () => { img.hidden = false; };
        img.onerror = () => { img.hidden = true; };
        img.src = track.cover;
      } else { img.removeAttribute('src'); }
    });
    root.querySelectorAll('[data-track]').forEach((button, i) => {
      if (i === index) button.setAttribute('aria-current', 'true');
      else button.removeAttribute('aria-current');
    });
    renderTime();
  }
  async function start() {
    const ticket = ++request;
    message();
    if (!loaded || audio.error) {
      loaded = true;
      audio.src = tracks[index].src;
    }
    try { await audio.play(); }
    catch (error) {
      if (ticket !== request || error.name === 'AbortError') return;
      message(error.name === 'NotAllowedError' ? '请再点一次播放。' : '这首歌暂时无法播放，可以重试或切换下一首。');
    }
  }
  function select(next) {
    ++request;
    audio.pause();
    index = (next + tracks.length) % tracks.length;
    resume = 0;
    loaded = false;
    audio.removeAttribute('src');
    audio.load();
    renderTrack();
    save();
    start();
  }

  toggle.addEventListener('click', () => setOpen(panel.hidden));
  $('[data-music-close]').addEventListener('click', () => setOpen(false, true));
  document.addEventListener('pointerdown', event => {
    if (!panel.hidden && !root.contains(event.target)) setOpen(false);
  });
  root.addEventListener('keydown', event => {
    if (event.key === 'Escape' && !panel.hidden) {
      event.stopPropagation();
      setOpen(false, true);
    }
  });
  root.addEventListener('focusout', event => {
    if (event.relatedTarget && !root.contains(event.relatedTarget)) setOpen(false);
  });

  if (tracks.length) {
    root.querySelectorAll('button:disabled, [data-music-volume]').forEach(el => { el.disabled = false; });
    $('[data-music-prev]').disabled = $('[data-music-next]').disabled = tracks.length < 2;
    const list = $('[data-music-list]');
    tracks.forEach((track, i) => {
      const li = document.createElement('li');
      const button = document.createElement('button');
      button.type = 'button';
      button.dataset.track = i;
      const no = document.createElement('span');
      no.className = 'music-track-no';
      no.textContent = String(i + 1).padStart(2, '0');
      const title = document.createElement('span');
      title.className = 'music-track-title';
      title.textContent = track.title;
      const artist = document.createElement('span');
      artist.className = 'music-track-artist';
      artist.textContent = track.artist;
      button.append(no, title, artist);
      button.addEventListener('click', () => select(i));
      li.append(button);
      list.append(li);
    });
    play.addEventListener('click', () => {
      if (audio.paused) start();
      else { ++request; audio.pause(); }
    });
    $('[data-music-prev]').addEventListener('click', () => select(index - 1));
    $('[data-music-next]').addEventListener('click', () => select(index + 1));
    loop.addEventListener('click', () => { single = !single; renderLoop(); save(); });
    mute.addEventListener('click', () => { audio.muted = !audio.muted; });
    volume.addEventListener('input', () => { audio.volume = Number(volume.value); audio.muted = false; });
    seek.addEventListener('input', () => {
      if (Number.isFinite(audio.duration)) { audio.currentTime = Number(seek.value) / 1000 * audio.duration; resume = 0; save(); }
    });
    audio.addEventListener('loadedmetadata', () => {
      if (resume && Number.isFinite(audio.duration)) audio.currentTime = Math.min(resume, Math.max(0, audio.duration - 0.1));
      resume = 0;
      renderTime();
    });
    audio.addEventListener('timeupdate', () => { renderTime(); if (Date.now() - lastSave > 5000) save(); });
    audio.addEventListener('durationchange', renderTime);
    audio.addEventListener('play', renderPlaying);
    audio.addEventListener('playing', () => { message(); renderPlaying(); });
    audio.addEventListener('pause', () => { renderPlaying(); save(); });
    audio.addEventListener('waiting', () => message('正在缓冲…'));
    audio.addEventListener('error', () => message('这首歌暂时无法播放，可以重试或切换下一首。'));
    audio.addEventListener('volumechange', () => { renderVolume(); save(); });
    audio.addEventListener('ended', () => {
      if (single || tracks.length === 1) { audio.currentTime = 0; start(); }
      else select(index + 1);
    });
    window.addEventListener('pagehide', save);
    renderTrack();
  }
  renderVolume();
  renderLoop();
  root.hidden = false;
})();
