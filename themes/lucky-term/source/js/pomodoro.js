/* 全站番茄钟。用截止时间计时，后台节流、刷新和切页后仍能恢复。 */
(() => {
  'use strict';
  const root = document.getElementById('pomodoro');
  if (!root) return;
  const $ = selector => root.querySelector(selector);
  const KEY = 'lt-pomodoro';
  const defaults = { focus: 25, short: 5, long: 15, sound: true };
  const limits = { focus: 180, short: 60, long: 120 };
  const labels = { focus: '专注', short: '短休息', long: '长休息' };
  const panel = $('#pomo-panel');
  const toggle = $('[data-pomo-toggle]');
  const form = $('#pomo-settings');
  const title = document.title;
  let snapshot;
  let audio;
  const fresh = () => ({ version: 1, settings: { ...defaults }, mode: 'focus', completed: 0,
    remaining: 25 * 60, endAt: 0, finished: false });
  let state = fresh();

  function decode(raw) {
    try {
      const value = JSON.parse(raw);
      if (!value || value.version !== 1 || !Object.hasOwn(labels, value.mode)) return fresh();
      const settings = value.settings;
      if (!settings || !Object.keys(limits).every(mode => Number.isInteger(settings[mode]) &&
        settings[mode] >= 1 && settings[mode] <= limits[mode])) return fresh();
      if (!Number.isInteger(value.completed) || value.completed < 0 || value.completed > 4 ||
        (value.mode === 'focus' && value.completed === 4) ||
        !Number.isFinite(value.remaining) || value.remaining < 0 || value.remaining > settings[value.mode] * 60 ||
        !Number.isFinite(value.endAt) || value.endAt < 0) return fresh();
      return { version: 1, settings: { focus: settings.focus, short: settings.short, long: settings.long,
        sound: settings.sound !== false }, mode: value.mode, completed: value.completed,
        remaining: value.remaining, endAt: value.endAt, finished: value.finished === true };
    } catch (_) { return fresh(); }
  }
  function sync() {
    try {
      const raw = localStorage.getItem(KEY);
      if (raw !== snapshot) { state = decode(raw); snapshot = raw; }
    } catch (_) { /* 无法使用本机存储时，当前页面仍可计时。 */ }
  }
  function save() {
    try {
      const raw = JSON.stringify(state);
      localStorage.setItem(KEY, raw);
      snapshot = raw;
    } catch (_) { /* 隐私模式或存储空间已满。 */ }
  }
  function secondsLeft() {
    return state.endAt ? Math.max(0, Math.ceil((state.endAt - Date.now()) / 1000)) : state.remaining;
  }
  function format(seconds) {
    return `${String(Math.floor(seconds / 60)).padStart(2, '0')}:${String(Math.ceil(seconds % 60)).padStart(2, '0')}`;
  }
  function prepareSound() {
    if (!state.settings.sound) return;
    try {
      const Audio = window.AudioContext || window.webkitAudioContext;
      if (!audio && Audio) audio = new Audio();
      if (audio?.state === 'suspended') audio.resume().catch(() => {});
    } catch (_) { /* 提示音不可用时继续显示完成状态。 */ }
  }
  function chime() {
    if (!state.settings.sound || audio?.state !== 'running' || document.hidden) return;
    try {
      const oscillator = audio.createOscillator();
      const gain = audio.createGain();
      oscillator.connect(gain);
      gain.connect(audio.destination);
      oscillator.frequency.setValueAtTime(660, audio.currentTime);
      oscillator.frequency.setValueAtTime(880, audio.currentTime + 0.18);
      gain.gain.setValueAtTime(0.12, audio.currentTime);
      gain.gain.exponentialRampToValueAtTime(0.001, audio.currentTime + 0.6);
      oscillator.start();
      oscillator.stop(audio.currentTime + 0.6);
    } catch (_) { /* 声音只是补充提醒。 */ }
  }
  function reset(mode) {
    state.mode = mode;
    state.remaining = state.settings[mode] * 60;
    state.endAt = 0;
    state.finished = false;
  }
  function complete() {
    if (state.mode === 'focus') {
      state.completed++;
      reset(state.completed === 4 ? 'long' : 'short');
    } else {
      if (state.completed === 4) state.completed = 0;
      reset('focus');
    }
    state.finished = true;
    save();
    chime();
  }
  function render() {
    const remaining = secondsLeft();
    const time = format(remaining);
    const running = state.endAt > 0;
    const paused = !running && remaining < state.settings[state.mode] * 60;
    const label = labels[state.mode];
    $('[data-pomo-time]').textContent = time;
    $('[data-pomo-time]').setAttribute('aria-label', `剩余 ${Math.floor(remaining / 60)} 分 ${Math.ceil(remaining % 60)} 秒`);
    $('[data-pomo-mini]').textContent = time;
    $('[data-pomo-label]').textContent = state.finished ? '计时完成' : (running || paused ? label : '番茄钟');
    $('[data-pomo-duration]').textContent = state.settings[state.mode];
    const status = running ? `${label}中` : (paused ? '已暂停' : (state.finished ?
      (state.mode === 'focus' ? '休息结束，准备专注' : '专注完成，准备休息') : '准备开始'));
    if ($('[data-pomo-status]').textContent !== status) $('[data-pomo-status]').textContent = status;
    $('[data-pomo-action]').textContent = running ? `暂停${label}` : (paused ? `继续${label}` : `开始${label}`);
    $('[data-pomo-play]').hidden = running;
    $('[data-pomo-pause]').hidden = !running;
    $('[data-pomo-caption]').textContent = state.mode === 'focus' ? '留一点时间，只做一件事' : '起身走走，让注意力慢慢恢复';
    $('[data-pomo-round]').textContent = `第 ${state.mode === 'focus' ? Math.min(4, state.completed + 1) : Math.max(1, state.completed)} / 4 轮`;
    $('[data-pomo-next]').textContent = state.mode === 'focus' ?
      `完成后休息 ${state.settings[state.completed === 3 ? 'long' : 'short']} 分钟` : '休息结束后继续专注';
    root.querySelectorAll('[data-pomo-mode]').forEach(button => {
      button.setAttribute('aria-pressed', String(button.dataset.pomoMode === state.mode));
    });
    root.querySelectorAll('.pomo-dots i').forEach((dot, index) => {
      dot.classList.toggle('is-done', index < state.completed);
      dot.classList.toggle('is-current', state.mode === 'focus' && index === state.completed);
    });
    root.classList.toggle('is-running', running);
    root.classList.toggle('is-finished', state.finished);
    toggle.setAttribute('aria-label', `${panel.hidden ? '展开' : '收起'}番茄钟，${status}，剩余 ${time}`);
    document.title = running ? `${time} ${label} · ${title}` : (state.finished ? `计时完成 · ${title}` : title);
  }
  function tick() {
    if (document.prerendering) return;
    sync();
    if (state.endAt && secondsLeft() === 0) complete();
    render();
  }
  function openSettings(open) {
    form.hidden = !open;
    $('[data-pomo-settings]').setAttribute('aria-expanded', String(open));
    if (open) {
      Object.keys(limits).forEach(mode => { form.elements[mode].value = state.settings[mode]; });
      form.elements.sound.checked = state.settings.sound;
      form.elements.focus.focus({ preventScroll: true });
    }
  }
  function setOpen(open, returnFocus = false) {
    panel.hidden = !open;
    toggle.setAttribute('aria-expanded', String(open));
    if (!open) openSettings(false);
    render();
    if (open) $('[data-pomo-start]').focus({ preventScroll: true });
    else if (returnFocus) toggle.focus({ preventScroll: true });
  }
  toggle.addEventListener('click', () => { tick(); setOpen(panel.hidden); });
  $('[data-pomo-close]').addEventListener('click', () => setOpen(false, true));
  $('[data-pomo-settings]').addEventListener('click', () => { tick(); openSettings(form.hidden); });
  $('[data-pomo-start]').addEventListener('click', () => {
    tick();
    prepareSound();
    if (state.endAt) {
      state.remaining = secondsLeft();
      state.endAt = 0;
    } else {
      state.endAt = Date.now() + state.remaining * 1000;
    }
    state.finished = false;
    save();
    render();
  });
  $('[data-pomo-reset]').addEventListener('click', () => {
    tick();
    reset(state.mode);
    save();
    render();
  });
  root.querySelectorAll('[data-pomo-mode]').forEach(button => {
    button.addEventListener('click', () => {
      tick();
      const mode = button.dataset.pomoMode;
      if (mode === state.mode) return;
      if (mode === 'focus' && state.completed === 4) state.completed = 0;
      reset(mode);
      save();
      render();
    });
  });
  form.addEventListener('submit', event => {
    event.preventDefault();
    if (!form.reportValidity()) return;
    tick();
    Object.keys(limits).forEach(mode => { state.settings[mode] = Number(form.elements[mode].value); });
    state.settings.sound = form.elements.sound.checked;
    reset(state.mode);
    save();
    openSettings(false);
    $('[data-pomo-settings]').focus({ preventScroll: true });
    render();
  });
  root.addEventListener('keydown', event => {
    if (event.key === 'Escape' && !panel.hidden) {
      event.stopPropagation();
      setOpen(false, true);
    }
  });
  document.addEventListener('pointerdown', event => {
    if (!panel.hidden && !root.contains(event.target)) setOpen(false);
  });
  root.addEventListener('focusout', event => {
    if (event.relatedTarget && !root.contains(event.relatedTarget)) setOpen(false);
  });
  window.addEventListener('storage', event => { if (event.key === KEY || event.key === null) tick(); });
  window.addEventListener('pageshow', tick);
  document.addEventListener('visibilitychange', tick);
  function activate() {
    tick();
    root.hidden = false;
    setInterval(tick, 250);
  }
  if (document.prerendering) document.addEventListener('prerenderingchange', activate, { once: true });
  else activate();
})();
