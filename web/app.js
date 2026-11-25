'use strict';

(function () {
  const $$ = (selector, root) => Array.from((root || document).querySelectorAll(selector));
  const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
  const has = (v) => typeof v === 'number' && Number.isFinite(v);

  const STATUS_TEXT = {
    off: 'ไม่ได้ต่อ',
    connecting: 'กำลังต่อ',
    connected: 'เชื่อมต่อแล้ว',
    demo: 'เดโม'
  };

  const MODE_TEXT = { free: 'อิสระ', cycle: '30:2', test: '2 นาที' };
  const MODE_KEYS = ['free', 'cycle', 'test'];
  const PROFILE_TEXT = { adult: 'ผู้ใหญ่', child: 'เด็ก' };

  const MODE_SUB = {
    free: 'ไม่จับเวลา',
    cycle: 'กด 30 ให้ลม 2',
    test: 'จับเวลา 2 นาที'
  };

  const MODE_LABEL = {
    free: 'จำนวน',
    cycle: 'รอบ',
    test: 'เวลา'
  };

  const MODE_ICON = {
    free: 'repeat',
    cycle: 'repeat',
    test: 'timer'
  };

  const TONE_ICON = {
    idle: 'info',
    ok: 'circle-check',
    warn: 'triangle-alert',
    bad: 'triangle-alert',
    accent: 'wind'
  };

  const HANDS_OFF_MS = 2000;

  const TH_DEFAULTS = {
    good: 6.0,
    deep: 12.0,
    over: 18.0,
    contact: 0.5,
    recoil: 0.2,
    filt: 1.0
  };
  const TH_STEP = { good: 0.5, deep: 0.5, over: 0.5, contact: 0.1, recoil: 0.1 };
  const TH_PRESETS = [0.4, 0.7, 1.0];
  const TH_EPS = 1e-6;

  const state = {
    transport: null,
    transportKind: '',
    status: 'off',
    demoTitle: '',
    cfg: null,
    mode: 'free',
    profile: 'child',
    theme: 'dark',
    frame: null,
    history: [],
    compressions: [],
    lastCompression: null,
    lastSum: null,
    lastSumAt: null,
    lastDiag: null,
    sumSeq: 0,
    lastPeak: null,
    lastPeakKg: false,
    pauseActive: false,
    pauseStart: null,
    overload: false,
    overloadUntil: 0,
    lowSps: false,
    sessionActive: false,
    metronome: false,
    rateLevel: 0,
    count: 0,
    cc: 0,
    cy: 0,
    elapsed: 0,
    targetDepth: 0,
    targetRate: 60,
    display: { depth: 0, rate: 60 },
    graphDirty: true,
    sheet: null,
    lastFocus: null,
    opts: {},
    cal: null,
    dotsKey: '',
    toastTimer: null,
    pulseTimer: null,
    stopTimer: null,
    sheetTimer: null,
    colorCache: null,
    infoKey: 'depth',
    rulerWidth: 0,
    scoreAnim: null,
    metricTimers: [],
    bannerTimers: new Map(),
    staggerSeq: 0,
    swapTimer: null,
    th: null
  };

  const els = {};

  function cacheEls() {
    const ids = [
      'connPill', 'connText', 'metroBtn', 'appNav', 'modeSwitch', 'heroCard',
      'tiles', 'depthValue', 'depthUnit', 'depthPill', 'depthPillIcon', 'depthPillText',
      'depthHint', 'ruler', 'rulerSvg', 'rulerMarker', 'rulerCaret', 'ctaBtn',
      'ctaTitle', 'ctaSub', 'ctaIconUse', 'rateTile', 'rateValue', 'ratePill',
      'ratePillIcon', 'ratePillText', 'rateTrack', 'rateBand', 'rateDot',
      'recoilTile', 'recoilValue', 'recoilPill', 'recoilPillIcon',
      'recoilPillText', 'recoilDots', 'handTile', 'handValue', 'handTarget',
      'countTile', 'countLabel', 'countIconUse', 'countValue', 'countGood',
      'countRing', 'countRingNum', 'countCycle', 'countTestRing', 'countTestNum',
      'depthGraph', 'graphWrap', 'graphEmpty', 'pauseBanner', 'pauseText',
      'breathBanner', 'warnSps', 'warnHx', 'warnOverload', 'toast', 'sheetLayer',
      'sheetConnect', 'sheetSummary', 'sheetCalib', 'sheetSettings', 'sheetInfo',
      'sheetTh', 'thBody', 'thRow', 'openThBtn', 'thLiveKg', 'thLiveLast',
      'thBar', 'thZones', 'thLegend', 'thMarker', 'thSps', 'thSpsText',
      'thValGood', 'thValDeep', 'thValOver', 'thValContact', 'thValRecoil',
      'thResetBtn',
      'connectWs', 'connectSerial', 'serialHint', 'connectDemo', 'demoSelect',
      'connectDisconnectRow', 'connectDisconnectBtn',
      'summaryContent', 'summaryEmpty', 'summaryFoot', 'scoreRing', 'scoreValue',
      'gradeWord', 'summaryMeta', 'metricDepth', 'metricRate', 'metricRecoil',
      'metricHand', 'metricCcf', 'mDepth', 'mRate', 'mRecoil', 'mHand', 'mCcf',
      'mDepthRing', 'mRateRing', 'mRecoilRing', 'mHandRing', 'mCcfRing',
      'summaryFacts', 'csvBtn', 'againBtn', 'goPracticeBtn', 'settingsConnText',
      'settingsConnectBtn', 'settingsConnIconUse', 'metroSwitch', 'themeIconUse',
      'profileIconUse', 'openCalibBtn', 'calibStepper', 'calibPanel',
      'calibStatus', 'calibActions', 'sheetInfoTitle', 'sheetInfoBody',
      'infoHeadUse'
    ];
    ids.forEach((id) => {
      els[id] = document.getElementById(id);
    });
    els.modeButtons = $$('[data-mode]');
    els.profileButtons = $$('[data-profile]');
    els.themeButtons = $$('[data-theme-set]');
    els.countBlocks = $$('[data-count]');
    els.navButtons = $$('[data-nav]');
    els.thStepButtons = $$('[data-th-step]');
    els.thFiltButtons = $$('[data-th-filt]');
    els.sheetMap = {
      connect: els.sheetConnect,
      summary: els.sheetSummary,
      settings: els.sheetSettings,
      calib: els.sheetCalib,
      th: els.sheetTh,
      info: els.sheetInfo
    };
  }

  function setText(el, text) {
    if (el && el.textContent !== text) el.textContent = text;
  }

  function setUse(el, name) {
    if (!el) return;
    const href = '#i-' + name;
    if (el.getAttribute('href') !== href) el.setAttribute('href', href);
  }

  function instantMotion() {
    return state.opts.motion === 'freeze' ||
      window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  }

  function setPill(pill, textEl, iconEl, text, tone, icon) {
    if (!pill) return;
    if (pill.dataset.tone !== tone) {
      pill.dataset.tone = tone;
      if (!instantMotion()) {
        pill.classList.remove('is-swap');
        void pill.offsetWidth;
        pill.classList.add('is-swap');
      }
    }
    setText(textEl, text);
    setUse(iconEl, icon || TONE_ICON[tone] || TONE_ICON.idle);
  }

  function setState(el, tone) {
    if (el && el.dataset.state !== tone) el.dataset.state = tone;
  }

  function toneForLevel(level) {
    if (level === 1) return 'warn';
    if (level === 2) return 'ok';
    if (level === 3) return 'bad';
    return 'idle';
  }

  function levelForDepth(value, info) {
    if (!has(value) || value <= 0) return 0;
    if (value < info.good) return 1;
    if (value >= info.deep) return 3;
    return 2;
  }

  function heroPeak() {
    if (!has(state.lastPeak)) return 0;
    if (state.lastPeakKg !== unitInfo().kg) return 0;
    return state.lastPeak;
  }

  function tickNum(value) {
    if (!has(value)) return '0';
    const rounded = Math.round(value * 10) / 10;
    return Number.isInteger(rounded) ? String(rounded) : rounded.toFixed(1);
  }

  function fmtDuration(totalSeconds) {
    const sec = Math.max(0, Math.round(has(totalSeconds) ? totalSeconds : 0));
    const m = Math.floor(sec / 60);
    const s = sec % 60;
    return m + ':' + String(s).padStart(2, '0');
  }

  function pct(part, whole) {
    if (!whole || whole <= 0) return 0;
    return Math.round((Number(part) || 0) * 100 / whole);
  }

  function unitInfo() {
    const cfg = state.cfg;
    const kg = !!(cfg && cfg.unit === 'kg');
    if (kg) {
      const good = has(cfg.gkg) ? cfg.gkg : 6;
      const deep = has(cfg.dkg) ? cfg.dkg : 12;
      const overload = has(cfg.ovkg) ? cfg.ovkg : 18;
      return {
        kg: true,
        unit: 'กก.',
        good,
        deep,
        overload,
        scaleMax: Math.max(deep + 2, overload)
      };
    }
    const goodMm = has(cfg && cfg.gmin) ? cfg.gmin : 50;
    const deepMm = has(cfg && cfg.dmin) ? cfg.dmin : 60;
    const good = goodMm / 10;
    const deep = deepMm / 10;
    return {
      kg: false,
      unit: 'ซม.',
      good,
      deep,
      overload: 0,
      scaleMax: Math.max(8, Math.ceil(deep * 1.2))
    };
  }

  function rateRange() {
    const cfg = state.cfg;
    return {
      min: has(cfg && cfg.rmin) ? cfg.rmin : 100,
      max: has(cfg && cfg.rmax) ? cfg.rmax : 120
    };
  }

  function toDisplay(value) {
    if (!has(value)) return 0;
    return unitInfo().kg ? value : value / 10;
  }

  function depthText(value) {
    return has(value) ? Number(value).toFixed(1) : '—';
  }

  function roundTh(value) {
    return Math.round(Number(value) * 10) / 10;
  }

  function thFromCfg(cfg) {
    const pick = (key) => has(cfg && cfg[key]) ? roundTh(cfg[key]) : TH_DEFAULTS[key];
    return {
      good: pick('gkg'),
      deep: pick('dkg'),
      over: pick('ovkg'),
      contact: pick('ckg'),
      recoil: pick('rkg'),
      filt: has(cfg && cfg.flt) ? cfg.flt : TH_DEFAULTS.filt
    };
  }

  function thValid(t) {
    if (!t) return false;
    if (t.contact < 0.1 - TH_EPS || t.contact > 10 + TH_EPS) return false;
    if (t.good < 0.5 - TH_EPS) return false;
    if (t.over > 60 + TH_EPS) return false;
    if (t.recoil < -TH_EPS || t.recoil >= t.contact - TH_EPS) return false;
    if (t.contact >= t.good - TH_EPS) return false;
    if (t.good >= t.deep - TH_EPS) return false;
    if (t.deep >= t.over - TH_EPS) return false;
    if (t.filt < 0.2 - TH_EPS || t.filt > 1 + TH_EPS) return false;
    return true;
  }

  function thStepValid(t, field, dir) {
    const step = TH_STEP[field];
    if (!step) return false;
    const candidate = Object.assign({}, t);
    candidate[field] = roundTh(t[field] + dir * step);
    return thValid(candidate);
  }

  function thShake() {
    const body = els.thBody;
    if (!body) return;
    body.classList.remove('is-shake');
    void body.offsetWidth;
    body.classList.add('is-shake');
    setTimeout(() => body.classList.remove('is-shake'), 330);
  }

  function thBuildBar(values) {
    if (!els.thZones || !els.thLegend) return;
    const max = Math.max(1, values.over + 2);
    const zones = [
      { to: values.contact, cls: 'is-recoil', label: 'คืนตัว' },
      { to: values.good, cls: 'is-contact', label: 'เริ่มกด' },
      { to: values.deep, cls: 'is-good', label: 'ผ่าน' },
      { to: values.over, cls: 'is-deep', label: 'ลึกไป' },
      { to: max, cls: 'is-over', label: 'เกินแรง' }
    ];
    let zonesHtml = '';
    let legendHtml = '';
    let from = 0;
    zones.forEach((zone) => {
      const to = clamp(zone.to, 0, max);
      const width = clamp((to - from) / max, 0, 1) * 100;
      zonesHtml += '<span class="th-zone ' + zone.cls + '" style="width:' +
        width.toFixed(2) + '%"></span>';
      legendHtml += '<span><i class="' + zone.cls + '"></i>' + zone.label + '</span>';
      from = Math.max(from, to);
    });
    els.thZones.innerHTML = zonesHtml;
    els.thLegend.innerHTML = legendHtml;
  }

  function thRender() {
    const th = state.th;
    if (!th) return;
    const v = th.values;
    setText(els.thValGood, v.good.toFixed(1));
    setText(els.thValDeep, v.deep.toFixed(1));
    setText(els.thValOver, v.over.toFixed(1));
    setText(els.thValContact, v.contact.toFixed(1));
    setText(els.thValRecoil, v.recoil.toFixed(1));

    els.thStepButtons.forEach((button) => {
      const field = button.dataset.thStep;
      const dir = Number(button.dataset.thDir);
      const allowed = thStepValid(v, field, dir);
      button.disabled = !allowed;
      button.setAttribute('aria-disabled', allowed ? 'false' : 'true');
    });

    let nearest = TH_PRESETS[0];
    let nearestDist = Infinity;
    TH_PRESETS.forEach((preset) => {
      const dist = Math.abs(v.filt - preset);
      if (dist < nearestDist) {
        nearestDist = dist;
        nearest = preset;
      }
    });
    els.thFiltButtons.forEach((button) => {
      const active = Math.abs(parseFloat(button.dataset.thFilt) - nearest) < TH_EPS;
      button.classList.toggle('is-active', active);
      button.setAttribute('aria-pressed', active ? 'true' : 'false');
    });

    thBuildBar(v);
    thUpdateLive();
  }

  function thUpdateLive() {
    const th = state.th;
    if (!th) return;
    const frame = state.frame;
    const kg = frame && has(frame.kg) ? frame.kg : null;
    setText(els.thLiveKg, kg == null ? '—' : Number(kg).toFixed(1));

    const last = state.lastCompression;
    setText(els.thLiveLast, 'ล่าสุด ' +
      (last && has(last.pk) ? toDisplay(last.pk).toFixed(1) : '—'));

    const max = Math.max(1, th.values.over + 2);
    const liveKg = kg == null ? 0 : kg;
    if (els.thBar && els.thMarker) {
      const width = els.thBar.clientWidth;
      const travel = Math.max(0, width - 4);
      const fraction = clamp(liveKg / max, 0, 1);
      els.thMarker.style.transform = 'translateX(' + (fraction * travel).toFixed(1) + 'px)';
    }

    const sps = frame && has(frame.sps) ? frame.sps : 0;
    const showSps = sps >= 1 && sps <= 20;
    if (els.thSps) els.thSps.hidden = !showSps;
    if (showSps) setText(els.thSpsText, 'อ่าน ' + sps + ' ครั้ง/วิ');
  }

  function thSchedule() {
    const th = state.th;
    if (!th) return;
    th.dirty = true;
    clearTimeout(th.timer);
    th.timer = setTimeout(thFlush, 400);
  }

  function thFlush() {
    const th = state.th;
    if (!th || !th.dirty) return;
    clearTimeout(th.timer);
    th.timer = null;
    if (!state.transport || state.status === 'off') {
      th.dirty = false;
      showToast('ยังไม่ได้เชื่อมต่อหุ่น', 'bad');
      return;
    }
    const v = th.values;
    th.dirty = false;
    th.awaiting = true;
    state.transport.send('set th ' + v.good.toFixed(1) + ' ' + v.deep.toFixed(1) +
      ' ' + v.over.toFixed(1) + ' ' + v.contact.toFixed(1) + ' ' +
      v.recoil.toFixed(1) + ' ' + v.filt.toFixed(2));
  }

  function thReset() {
    if (!state.th) return;
    clearTimeout(state.th.timer);
    state.th.timer = null;
    state.th.values = Object.assign({}, TH_DEFAULTS);
    state.th.dirty = false;
    thRender();
    if (!state.transport || state.status === 'off') {
      showToast('ยังไม่ได้เชื่อมต่อหุ่น', 'bad');
      return;
    }
    state.th.awaiting = true;
    state.transport.send('set reset');
  }

  function thHandleAck(msg) {
    const th = state.th;
    if (!th || !msg || msg.cmd !== 'set') return false;
    if (state.sheet !== 'th' && !th.awaiting && !th.dirty) return false;
    if (msg.ok) {
      th.awaiting = false;
      showToast('บันทึกแล้ว', 'ok');
    } else {
      th.awaiting = false;
      th.dirty = false;
      clearTimeout(th.timer);
      th.timer = null;
      if (th.lastCfg) th.values = Object.assign({}, th.lastCfg);
      thRender();
      showToast('บันทึกไม่ได้', 'bad');
      thShake();
    }
    return true;
  }

  function refreshThConfig() {
    const th = state.th;
    if (!th) return;
    const snapshot = thFromCfg(state.cfg);
    th.lastCfg = snapshot;
    if (th.awaiting) {
      th.awaiting = false;
      if (th.dirty) return;
    } else if (th.dirty) {
      return;
    }
    th.values = Object.assign({}, snapshot);
    thRender();
  }

  function openTh() {
    clearTimeout(state.th && state.th.timer);
    state.th = {
      values: thFromCfg(state.cfg),
      lastCfg: thFromCfg(state.cfg),
      dirty: false,
      awaiting: false,
      timer: null
    };
    openSheet('th');
    thRender();
  }

  function readColors() {
    const cs = getComputedStyle(document.documentElement);
    const get = (name) => cs.getPropertyValue(name).trim();
    return {
      theme: state.theme,
      accent: get('--accent'),
      ok: get('--ok'),
      warn: get('--warn'),
      bad: get('--bad'),
      text: get('--text'),
      text2: get('--text-2'),
      text3: get('--text-3'),
      surface: get('--surface'),
      surface2: get('--surface-2'),
      surface3: get('--surface-3'),
      okTint: get('--ok-tint')
    };
  }

  function colors() {
    if (!state.colorCache || state.colorCache.theme !== state.theme) {
      state.colorCache = readColors();
    }
    return state.colorCache;
  }

  function esc(text) {
    return String(text == null ? '' : text)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#39;');
  }

  function parseUrl() {
    const q = new URLSearchParams(location.search);
    const speedRaw = parseFloat(q.get('speed'));
    const seekRaw = parseFloat(q.get('seek'));
    const mockRaw = q.get('mock') || '';
    return {
      demo: q.get('demo') || '',
      speed: has(speedRaw) && speedRaw > 0 ? clamp(speedRaw, .1, 20) : 1,
      seek: has(seekRaw) && seekRaw > 0 ? seekRaw : 0,
      theme: q.get('theme') || '',
      sheet: q.get('sheet') || '',
      info: q.get('info') || '',
      motion: q.get('motion') || '',
      mock: mockRaw.split(',').map((item) => item.trim()).filter(Boolean),
      ws: q.get('ws') === '1'
    };
  }

  function mockEnabled(name) {
    return Array.isArray(state.opts.mock) && state.opts.mock.indexOf(name) !== -1;
  }

  async function fetchManifest() {
    try {
      const response = await fetch('demo/manifest.json', { cache: 'no-store' });
      if (!response.ok) throw new Error('manifest');
      const list = await response.json();
      if (Array.isArray(list) && list.length) return list;
    } catch (err) {
      /* use the built-in entry below */
    }
    return [{ id: 'good', title: 'กดถูกต้อง', file: 'good.jsonl' }];
  }

  function parseRecording(text) {
    const messages = [];
    let lastT = 0;
    text.split(/\r?\n/).forEach((line) => {
      if (!line || line.charAt(0) !== '{') return;
      let obj;
      try {
        obj = JSON.parse(line);
      } catch (err) {
        return;
      }
      if (!obj || typeof obj !== 'object') return;
      let at;
      if (obj.ty === 'cfg') {
        at = lastT;
      } else if (has(obj.t)) {
        at = obj.t;
        lastT = obj.t;
      } else if (obj.ty === 'sum') {
        at = lastT + 1;
      } else {
        at = lastT;
      }
      messages.push({ at, o: obj });
    });
    messages.sort((a, b) => a.at - b.at);
    return messages;
  }

  function canonicalCommand(line) {
    const words = String(line || '').trim().toLowerCase().split(/\s+/);
    if (!words[0]) return 'unknown';
    if (words[0] === 'cal') return words[1] ? 'cal ' + words[1] : 'cal';
    if (words[0] === 'wifi') return 'wifi pass';
    return words[0];
  }

  class DemoSocket {
    constructor(opts) {
      this.opts = opts || {};
      this.id = this.opts.id || 'good';
      this.speed = this.opts.speed || 1;
      this.seek = this.opts.seek || 0;
      this.mock = Array.isArray(this.opts.mock) ? this.opts.mock : [];
      this.messages = [];
      this.index = 0;
      this.offsetAt = 0;
      this.startedAt = 0;
      this.loopLen = 1;
      this.sampleSum = null;
      this.cfg = null;
      this.nextDiagAt = 0;
      this.timer = null;
      this.stopped = true;
      this.peakCount = 0;
      this.onMessage = null;
      this.onStatus = null;
      this.onReset = null;
      this.onInstantDone = null;
      this.onError = null;
    }

    async connect() {
      this.stopped = false;
      this.onStatus && this.onStatus('connecting');
      try {
        const list = await fetchManifest();
        let chosen = list.find((item) => item.id === this.id);
        if (!chosen) chosen = list[0] || { id: this.id, file: this.id + '.jsonl', title: this.id };
        this.id = chosen.id;
        const response = await fetch('demo/' + chosen.file, { cache: 'no-store' });
        if (!response.ok) throw new Error('โหลดเดโมไม่ได้');
        const text = await response.text();
        this.messages = parseRecording(text);
        if (!this.messages.length) throw new Error('ไฟล์เดโมว่าง');
        this.loopLen = this.messages[this.messages.length - 1].at + 1;
        this.sampleSum = null;
        for (const item of this.messages) {
          if (item.o.ty === 'sum') this.sampleSum = item.o;
        }
        this.cfg = null;
        for (const item of this.messages) {
          if (item.o.ty === 'cfg') {
            this.cfg = Object.assign({}, item.o);
            break;
          }
        }
        this.onStatus && this.onStatus('demo', { title: chosen.title || chosen.id, id: chosen.id });
        this.start();
      } catch (err) {
        this.stopped = true;
        this.onStatus && this.onStatus('off', { error: err.message });
        this.onError && this.onError(err.message || 'เปิดเดโมไม่ได้');
      }
    }

    deliver(obj, meta) {
      if (obj.ty === 'cfg') this.cfg = Object.assign({}, obj);
      if (obj.ty === 'sum') this.sampleSum = Object.assign({}, obj);
      this.onMessage && this.onMessage(obj, meta);
    }

    start() {
      const seekMs = Math.max(0, this.seek * 1000);
      this.onReset && this.onReset();
      let remaining = seekMs;
      let guard = 0;
      while (remaining >= this.loopLen && guard < 4000) {
        for (const item of this.messages) {
          this.deliver(item.o, { instant: true, at: item.at });
        }
        remaining -= this.loopLen;
        this.onReset && this.onReset();
        guard += 1;
      }
      let index = 0;
      while (index < this.messages.length && this.messages[index].at <= remaining) {
        const item = this.messages[index];
        this.deliver(item.o, { instant: true, at: item.at });
        index += 1;
      }
      this.index = index;
      this.offsetAt = remaining;
      this.startedAt = performance.now();
      this.nextDiagAt = 0;
      this.onInstantDone && this.onInstantDone();
      this.pump();
    }

    restart() {
      if (this.stopped) return;
      this.onReset && this.onReset();
      this.index = 0;
      this.offsetAt = 0;
      this.startedAt = performance.now();
      this.nextDiagAt = 0;
      this.pump();
    }

    pump() {
      if (this.stopped) return;
      const now = performance.now();
      const deviceNow = this.offsetAt + (now - this.startedAt) * this.speed;
      this.maybeDiag(deviceNow);
      let guard = 0;
      while (this.index < this.messages.length &&
             this.messages[this.index].at <= deviceNow &&
             guard < 500) {
        const item = this.messages[this.index];
        this.deliver(item.o, { instant: false, at: item.at });
        this.index += 1;
        guard += 1;
      }
      if (this.index >= this.messages.length) {
        const waitMs = Math.max(20, (this.loopLen - deviceNow) / this.speed);
        this.timer = setTimeout(() => this.loop(), Math.min(220, waitMs) + 10);
        return;
      }
      const delay = Math.max(0, (this.messages[this.index].at - deviceNow) / this.speed);
      this.timer = setTimeout(() => this.pump(), Math.min(Math.max(delay, 1), 20));
    }

    loop() {
      if (this.stopped) return;
      this.onReset && this.onReset();
      this.index = 0;
      this.offsetAt = 0;
      this.startedAt = performance.now();
      this.nextDiagAt = 0;
      this.pump();
    }

    maybeDiag(deviceNow) {
      if (this.mock.indexOf('diag') === -1) return;
      if (deviceNow < this.nextDiagAt) return;
      this.nextDiagAt = deviceNow + 5000;
      this.onMessage && this.onMessage({
        ty: 'diag',
        up: Math.max(0, Math.floor(deviceNow / 1000)),
        rx: 0,
        rxBad: 0,
        qDrop: 0,
        sSkip: 0,
        fDrop: 0,
        sps: 10,
        heap: 180000,
        ser: 0,
        hx: 1
      }, { instant: false, at: deviceNow });
    }

    emitAck(cmd, line) {
      let msg = 'ok';
      if (cmd === 'set') {
        const text = String(line || '');
        let ack = null;
        if (/\bset\s+reset\b/i.test(text)) {
          this.cfg = Object.assign({}, this.cfg || {}, {
            gkg: TH_DEFAULTS.good,
            dkg: TH_DEFAULTS.deep,
            ovkg: TH_DEFAULTS.over,
            ckg: TH_DEFAULTS.contact,
            rkg: TH_DEFAULTS.recoil,
            flt: TH_DEFAULTS.filt
          });
          setTimeout(() => this.emitCfg(), 30);
          ack = { ty: 'ack', cmd: 'set', ok: 1, msg: 'ok' };
        } else {
          const match = text.match(/\bset\s+th\b(.*)/i);
          const nums = match ? (match[1].match(/-?\d+(?:\.\d+)?/g) || []).map(parseFloat) : [];
          if (match && nums.length >= 6 && this.cfg) {
            this.cfg = Object.assign({}, this.cfg, {
              gkg: nums[0],
              dkg: nums[1],
              ovkg: nums[2],
              ckg: nums[3],
              rkg: nums[4],
              flt: nums[5]
            });
            setTimeout(() => this.emitCfg(), 30);
            ack = { ty: 'ack', cmd: 'set', ok: 1, msg: 'ok' };
          } else {
            ack = { ty: 'ack', cmd: 'set', ok: 0, msg: 'bad syntax' };
          }
        }
        this.onMessage && this.onMessage(ack, { instant: false, at: null });
        return;
      }
      if (cmd === 'metro') msg = /\bon\b/i.test(line) ? 'on' : 'off';
      if (cmd === 'config') {
        this.emitCfg();
        msg = 'ok';
      }
      if (cmd === 'profile') {
        const prof = /child/i.test(line) ? 1 : 0;
        if (this.cfg) {
          this.cfg = Object.assign({}, this.cfg, { prof });
          setTimeout(() => this.emitCfg(), 30);
        }
      }
      if (cmd === 'cal bands') {
        const nums = String(line).match(/-?\d+(?:\.\d+)?/g) || [];
        if (nums.length >= 2 && this.cfg) {
          this.cfg = Object.assign({}, this.cfg, {
            gkg: parseFloat(nums[0]),
            dkg: parseFloat(nums[1]),
            src: 0,
            unit: 'kg',
            tof: 0
          });
          setTimeout(() => this.emitCfg(), 30);
        }
      }
      if (cmd === 'cal finish' || cmd === 'cal save') {
        if (this.cfg) {
          this.cfg = Object.assign({}, this.cfg, { map: Math.max(2, this.cfg.map || 0) });
          setTimeout(() => this.emitCfg(), 30);
        }
      }
      if (cmd === 'tare' || cmd === 'cal weight' || cmd === 'cal reset') {
        setTimeout(() => this.emitCfg(), 30);
      }
      if (cmd === 'cal peak') {
        this.peakCount += 1;
        const value = this.peakCount % 2 === 1 ? '7.1' : '13.0';
        this.onMessage && this.onMessage({ ty: 'ack', cmd: 'cal peak', ok: 1, msg: value }, { instant: false, at: null });
        return;
      }
      this.onMessage && this.onMessage({ ty: 'ack', cmd, ok: 1, msg }, { instant: false, at: null });
    }

    emitCfg() {
      if (!this.cfg) return;
      this.onMessage && this.onMessage(Object.assign({}, this.cfg), { instant: false, at: null });
    }

    emitSum() {
      if (!this.sampleSum) return;
      this.onMessage && this.onMessage(Object.assign({}, this.sampleSum), { instant: false, at: null });
    }

    send(line) {
      if (this.stopped) return;
      const cmd = canonicalCommand(line);
      setTimeout(() => {
        if (!this.stopped) this.emitAck(cmd, line);
      }, 100);
      if (cmd === 'stop') {
        setTimeout(() => {
          if (!this.stopped) this.emitSum();
        }, 320);
      }
      if (cmd === 'start') {
        setTimeout(() => {
          if (!this.stopped) this.restart();
        }, 140);
      }
    }

    disconnect() {
      this.stopped = true;
      if (this.timer) clearTimeout(this.timer);
      this.timer = null;
      this.onStatus && this.onStatus('off');
    }
  }

  class WsTransport {
    constructor() {
      this.socket = null;
      this.stopped = true;
      this.attempt = 0;
      this.timer = null;
      this.onMessage = null;
      this.onStatus = null;
      this.onError = null;
    }

    connect() {
      this.stopped = false;
      this.attempt = 0;
      this.open();
    }

    open() {
      if (this.stopped) return;
      this.onStatus && this.onStatus('connecting');
      let socket;
      try {
        const proto = location.protocol === 'https:' ? 'wss://' : 'ws://';
        socket = new WebSocket(proto + location.host + '/ws');
      } catch (err) {
        this.onStatus && this.onStatus('off', { error: err.message });
        return;
      }
      this.socket = socket;
      socket.onopen = () => {
        if (this.stopped) {
          socket.close();
          return;
        }
        this.attempt = 0;
        this.onStatus && this.onStatus('connected');
      };
      socket.onmessage = (event) => {
        const text = String(event.data == null ? '' : event.data);
        text.split(/\r?\n/).forEach((line) => {
          if (line.charAt(0) !== '{') return;
          let obj;
          try {
            obj = JSON.parse(line);
          } catch (err) {
            return;
          }
          this.onMessage && this.onMessage(obj, { instant: false, at: has(obj.t) ? obj.t : null });
        });
      };
      socket.onerror = () => {
        try { socket.close(); } catch (err) { /* ignore */ }
      };
      socket.onclose = () => {
        if (this.stopped) return;
        this.onStatus && this.onStatus('connecting');
        this.attempt += 1;
        const delay = Math.min(5000, 1000 * this.attempt);
        this.timer = setTimeout(() => this.open(), delay);
      };
    }

    send(line) {
      if (this.socket && this.socket.readyState === 1) {
        this.socket.send(line + '\n');
      }
    }

    disconnect() {
      this.stopped = true;
      if (this.timer) clearTimeout(this.timer);
      this.timer = null;
      if (this.socket) {
        try { this.socket.close(); } catch (err) { /* ignore */ }
      }
      this.socket = null;
      this.onStatus && this.onStatus('off');
    }
  }

  class SerialTransport {
    constructor() {
      this.port = null;
      this.reader = null;
      this.writer = null;
      this.stopped = true;
      this.received = false;
      this.configTimer = null;
      this.onMessage = null;
      this.onStatus = null;
      this.onError = null;
    }

    async connect() {
      if (!('serial' in navigator)) {
        this.onStatus && this.onStatus('off');
        this.onError && this.onError('เบราว์เซอร์นี้ไม่รองรับ Web Serial');
        return;
      }
      this.stopped = false;
      this.received = false;
      this.onStatus && this.onStatus('connecting');
      try {
        this.port = await navigator.serial.requestPort();
        await this.port.open({ baudRate: 115200 });
        const decoder = new TextDecoderStream();
        this.port.readable.pipeTo(decoder.writable).catch(() => {});
        this.reader = decoder.readable.getReader();
        this.writer = this.port.writable.getWriter();
        this.onStatus && this.onStatus('connected');
        this.readLoop();
        // Ask for config immediately; on ESP32 this also turns on USB streaming.
        // Retry once after 2 s in case opening the port reset a Nano bootloader.
        this.send('config');
        this.configTimer = setTimeout(() => {
          this.configTimer = null;
          if (!this.stopped && !this.received) this.send('config');
        }, 2000);
      } catch (err) {
        this.stopped = true;
        this.onStatus && this.onStatus('off', { error: err.message });
        this.onError && this.onError('ต่อสาย USB ไม่ได้');
        try { if (this.port) await this.port.close(); } catch (e) { /* ignore */ }
        this.port = null;
      }
    }

    async readLoop() {
      let buffer = '';
      try {
        while (!this.stopped) {
          const result = await this.reader.read();
          if (result.done) break;
          if (!result.value) continue;
          buffer += result.value;
          let index;
          while ((index = buffer.indexOf('\n')) >= 0) {
            const line = buffer.slice(0, index).replace(/\r$/, '');
            buffer = buffer.slice(index + 1);
            if (line.charAt(0) !== '{') continue;
            let obj;
            try {
              obj = JSON.parse(line);
            } catch (err) {
              continue;
            }
            this.received = true;
            if (this.configTimer) {
              clearTimeout(this.configTimer);
              this.configTimer = null;
            }
            this.onMessage && this.onMessage(obj, { instant: false, at: has(obj.t) ? obj.t : null });
          }
          if (buffer.length > 8192) buffer = '';
        }
      } catch (err) {
        /* port closed or unplugged */
      }
      if (!this.stopped) {
        this.stopped = true;
        this.onStatus && this.onStatus('off');
      }
    }

    async send(line) {
      if (!this.writer) return;
      try {
        await this.writer.write(new TextEncoder().encode(line + '\n'));
      } catch (err) {
        /* ignore a closed writer */
      }
    }

    async disconnect() {
      this.stopped = true;
      if (this.configTimer) {
        clearTimeout(this.configTimer);
        this.configTimer = null;
      }
      try { if (this.reader) await this.reader.cancel(); } catch (err) { /* ignore */ }
      try { if (this.writer) this.writer.releaseLock(); } catch (err) { /* ignore */ }
      try { if (this.port) await this.port.close(); } catch (err) { /* ignore */ }
      this.reader = null;
      this.writer = null;
      this.port = null;
      this.onStatus && this.onStatus('off');
    }
  }

  function setStatus(status, detail) {
    const previous = state.status;
    state.status = status;
    if (detail && detail.title) state.demoTitle = detail.title;

    if (els.connPill) {
      els.connPill.dataset.state = status === 'connected' ? 'ok'
        : status === 'demo' ? 'demo'
          : status === 'connecting' ? 'connecting' : 'off';
      els.connPill.setAttribute('aria-label', 'สถานะการเชื่อมต่อ: ' + (STATUS_TEXT[status] || STATUS_TEXT.off));
    }
    setText(els.connText, STATUS_TEXT[status] || STATUS_TEXT.off);

    if ((status === 'connected' || status === 'demo') && state.sheet === 'connect') {
      closeSheet();
    }
    if (status === 'off' && previous !== 'off') {
      resetSession(true);
    }
    refreshInfoPanels();
  }

  function attachTransport(transport, kind) {
    if (state.transport && state.transport !== transport) {
      try { state.transport.disconnect(); } catch (err) { /* ignore */ }
    }
    state.transport = transport;
    state.transportKind = kind || '';
    transport.onStatus = (status, detail) => setStatus(status, detail);
    transport.onMessage = (obj, meta) => handleMessage(obj, meta || {});
    transport.onReset = () => resetSession(true);
    transport.onInstantDone = () => completeInstant();
    transport.onError = (message) => showToast(message, 'bad');
    Promise.resolve(transport.connect()).catch((err) => {
      showToast(err && err.message ? err.message : 'ต่อไม่ได้', 'bad');
    });
  }

  function resetSession(keepConfig) {
    state.frame = null;
    state.history = [];
    state.compressions = [];
    state.lastCompression = null;
    state.pauseActive = false;
    state.pauseStart = null;
    state.overload = false;
    state.overloadUntil = 0;
    state.lowSps = false;
    state.sessionActive = false;
    state.rateLevel = 0;
    state.count = 0;
    state.cc = 0;
    state.cy = 0;
    state.elapsed = 0;
    state.targetDepth = 0;
    state.targetRate = 60;
    state.dotsKey = '';
    state.graphDirty = true;
    if (!keepConfig) state.cfg = null;
    renderRecoilDots();
    refreshConfigUI();
  }

  function applyCfg(msg) {
    const patched = Object.assign({}, msg);
    if (mockEnabled('nohx')) patched.hx = 0;
    if (mockEnabled('nohand')) patched.hand = 0;
    state.cfg = patched;
    state.profile = patched.prof === 1 ? 'child' : 'adult';
    refreshProfileButtons();
    refreshConfigUI();
    refreshThConfig();
  }

  function applyFrame(msg, meta) {
    const prev = state.frame;
    if (prev && (msg.t < prev.t - 500 || (has(msg.n) && msg.n < prev.n))) {
      resetSession(true);
    }

    const info = unitInfo();
    const value = has(msg.v) ? msg.v : 0;
    const displayValue = info.kg ? value : value / 10;

    state.frame = msg;
    state.targetDepth = displayValue;
    state.targetRate = has(msg.bpm) && msg.bpm > 0 ? msg.bpm : 60;
    state.rateLevel = msg.rl || 0;
    state.count = has(msg.n) ? msg.n : 0;
    state.cc = has(msg.cc) ? msg.cc : 0;
    state.cy = has(msg.cy) ? msg.cy : 0;
    state.elapsed = has(msg.el) ? msg.el : 0;
    state.lowSps = !!msg.lsps;
    state.overload = !!msg.ov;
    state.sessionActive = !!msg.act;
    if (msg.ov) state.overloadUntil = msg.t + 2200;

    if (msg.ps && !state.pauseActive) {
      state.pauseActive = true;
      if (state.pauseStart == null) state.pauseStart = msg.t - HANDS_OFF_MS;
    } else if (!msg.ps) {
      state.pauseActive = false;
      state.pauseStart = null;
    }

    if (has(msg.t)) {
      state.history.push({ t: msg.t, v: displayValue, ph: msg.ph || 0, ld: msg.ld || 0 });
      const cutoff = msg.t - 10500;
      while (state.history.length && state.history[0].t < cutoff) state.history.shift();
      if (state.history.length > 900) state.history.splice(0, state.history.length - 900);
    }

    state.graphDirty = true;
    if (!meta.instant) {
      refreshInfoPanels();
    }
  }

  function applyCompression(msg, meta) {
    const last = state.compressions[state.compressions.length - 1];
    if (last && last.i === msg.i && last.t === msg.t) return;

    state.compressions.push({
      i: msg.i,
      t: msg.t,
      pk: msg.pk,
      tp: has(msg.tp) ? msg.tp : null,
      src: msg.src,
      dl: msg.dl,
      rl: msg.rl,
      bpm: msg.bpm,
      rc: !!msg.rc,
      hd: !!msg.hd,
      ok: !!msg.ok
    });
    if (state.compressions.length > 500) state.compressions.shift();

    state.lastCompression = msg;
    state.lastPeak = toDisplay(msg.pk);
    state.lastPeakKg = unitInfo().kg;
    state.dotsKey = '';
    state.graphDirty = true;

    if (els.depthValue) setText(els.depthValue, heroPeak().toFixed(1));
    if (!meta.instant) {
      pulseCounter();
      restartHeroFlip();
    }

    if (state.cal && state.cal.active && state.cal.tof) {
      state.cal.presses += 1;
      const counter = document.getElementById('calPairCount');
      if (counter) setText(counter, String(state.cal.presses));
    }
  }

  function restartHeroFlip() {
    const box = els.depthValue && els.depthValue.parentElement;
    if (!box || instantMotion()) return;
    box.classList.remove('is-flip');
    void box.offsetWidth;
    box.classList.add('is-flip');
  }

  function handleMessage(msg, meta) {
    if (!msg || typeof msg !== 'object') return;
    switch (msg.ty) {
      case 'cfg':
        applyCfg(msg);
        break;
      case 'f':
        applyFrame(msg, meta);
        break;
      case 'c':
        applyCompression(msg, meta);
        break;
      case 'ps':
        state.pauseActive = true;
        state.pauseStart = has(msg.t) ? msg.t - HANDS_OFF_MS : state.pauseStart;
        break;
      case 'pe':
        state.pauseActive = false;
        state.pauseStart = null;
        break;
      case 'cy':
        state.cy = has(msg.cy) ? msg.cy : state.cy;
        break;
      case 'ov':
        state.overload = true;
        state.overloadUntil = (has(msg.t) ? msg.t : (state.frame ? state.frame.t : 0)) + 2200;
        break;
      case 'end':
        state.sessionActive = false;
        state.pauseActive = false;
        state.pauseStart = null;
        break;
      case 'sum':
        state.lastSum = Object.assign({}, msg);
        state.lastSumAt = has(meta.at) ? meta.at
          : (state.frame && has(state.frame.t) ? state.frame.t : 0);
        state.sumSeq += 1;
        if (!meta.instant && state.sheet !== 'calib') openSummary();
        break;
      case 'ack':
        handleAck(msg);
        break;
      case 'diag':
        state.lastDiag = Object.assign({}, msg);
        if (state.sheet === 'info' && state.infoKey === 'about') populateInfo();
        break;
      default:
        break;
    }
  }

  function completeInstant() {
    const sheet = state.opts.sheet;
    if (sheet === 'summary') {
      openSummary();
      return;
    }
    if (sheet === 'calib') {
      openCalib();
      return;
    }
    if (sheet === 'settings') {
      openSettings();
      return;
    }
    if (sheet === 'th') {
      openTh();
      return;
    }
    if (sheet === 'connect') {
      openConnect();
      return;
    }
    if (sheet === 'info') {
      openInfo(state.opts.info || 'depth');
      return;
    }
    if (state.lastSum && state.opts.seek > 0 && has(state.lastSumAt) &&
        state.lastSumAt <= state.opts.seek * 1000) {
      openSummary();
    }
  }

  function handleAck(msg) {
    if (thHandleAck(msg)) return;
    if (calHandleAck(msg)) return;
    if (!msg.ok) {
      showToast(msg.msg || 'สั่งหุ่นไม่ได้', 'bad');
    }
  }

  function refreshModeButtons() {
    const index = MODE_KEYS.indexOf(state.mode);
    if (els.modeSwitch) els.modeSwitch.style.setProperty('--mode-i', index < 0 ? 0 : index);
    els.modeButtons.forEach((button) => {
      const active = button.dataset.mode === state.mode;
      button.classList.toggle('is-active', active);
      button.setAttribute('aria-pressed', active ? 'true' : 'false');
    });
    setText(els.countLabel, MODE_LABEL[state.mode] || MODE_LABEL.free);
    setUse(els.countIconUse, MODE_ICON[state.mode] || MODE_ICON.free);
  }

  function refreshProfileButtons() {
    els.profileButtons.forEach((button) => {
      const active = button.dataset.profile === state.profile;
      button.classList.toggle('is-active', active);
      button.setAttribute('aria-pressed', active ? 'true' : 'false');
    });
    setUse(els.profileIconUse, state.profile === 'child' ? 'baby' : 'user');
  }

  function refreshThemeButtons() {
    els.themeButtons.forEach((button) => {
      const active = button.dataset.themeSet === state.theme;
      button.classList.toggle('is-active', active);
      button.setAttribute('aria-pressed', active ? 'true' : 'false');
    });
    setUse(els.themeIconUse, state.theme === 'light' ? 'sun' : 'moon');
  }

  function refreshConfigUI() {
    const info = unitInfo();
    if (els.depthUnit) setText(els.depthUnit, info.unit);
    if (els.rateBand) {
      const range = rateRange();
      const start = clamp((range.min - 60) / 100, 0, 1);
      const width = clamp((range.max - range.min) / 100, 0, 1);
      els.rateBand.style.left = (start * 100).toFixed(2) + '%';
      els.rateBand.style.width = (width * 100).toFixed(2) + '%';
    }
    refreshHandVisibility();
    refreshThVisibility();
    buildRuler(info);
    refreshInfoPanels();
  }

  function refreshThVisibility() {
    if (!els.thRow) return;
    els.thRow.hidden = !!(state.cfg && state.cfg.board === 'nano');
  }

  function refreshHandVisibility() {
    const noHand = !!(state.cfg && state.cfg.hand === 0);
    if (els.handTile) els.handTile.hidden = noHand;
    if (els.metricHand) els.metricHand.hidden = noHand;
    if (els.tiles) els.tiles.classList.toggle('is-no-hand', noHand);
  }

  function buildRuler(info) {
    const ruler = els.ruler;
    const svg = els.rulerSvg;
    if (!ruler || !svg) return;
    const width = Math.max(120, Math.round(ruler.clientWidth || 300));
    const height = 56;
    const max = Math.max(.5, info.scaleMax || 8);
    const step = info.kg ? 1 : .5;
    const labelEvery = info.kg ? 2 : 1;
    const baseY = 38;
    const bandTop = 30;
    const bandH = 14;
    const labelY = 16;
    const xOf = (value) => clamp(value / max, 0, 1) * width;

    const x1 = xOf(info.good);
    const x2 = xOf(info.deep);
    let markup = '';
    markup += '<rect class="ruler-band" x="' + x1.toFixed(1) + '" y="' + bandTop +
      '" width="' + Math.max(0, x2 - x1).toFixed(1) + '" height="' + bandH +
      '" rx="' + (bandH / 2) + '"/>';
    markup += '<line class="ruler-band-edge" x1="' + x1.toFixed(1) + '" y1="' + bandTop +
      '" x2="' + x2.toFixed(1) + '" y2="' + bandTop + '"/>';
    markup += '<line class="ruler-tick" x1="0" y1="' + baseY + '" x2="' + width +
      '" y2="' + baseY + '"/>';

    const count = Math.round(max / step);
    for (let i = 0; i <= count; i += 1) {
      const value = i * step;
      const x = xOf(value);
      const rounded = Math.round(value);
      const labelled = Math.abs(value - rounded) < 1e-6 && rounded % labelEvery === 0;
      const y2 = baseY - (labelled ? 12 : 6);
      markup += '<line class="ruler-tick' + (labelled ? ' major' : '') + '" x1="' +
        x.toFixed(1) + '" y1="' + baseY + '" x2="' + x.toFixed(1) + '" y2="' +
        y2 + '"/>';
      if (labelled) {
        const anchor = value <= 0.001 ? 'start' : (Math.abs(value - max) < .001 ? 'end' : 'middle');
        markup += '<text class="ruler-label" x="' + x.toFixed(1) + '" y="' + labelY +
          '" text-anchor="' + anchor + '">' + tickNum(value) + '</text>';
      }
    }

    svg.setAttribute('viewBox', '0 0 ' + width + ' ' + height);
    svg.innerHTML = markup;
    state.rulerWidth = width;
  }

  function refreshInfoPanels() {
    const statusText = STATUS_TEXT[state.status] || STATUS_TEXT.off;
    setText(els.settingsConnText, state.status === 'demo' && state.demoTitle
      ? 'เดโม · ' + state.demoTitle : statusText);
    setUse(els.settingsConnIconUse, state.transportKind === 'serial' ? 'usb'
      : state.transportKind === 'demo' ? 'monitor-play' : 'wifi');
    if (els.connectDisconnectRow) els.connectDisconnectRow.hidden = state.status === 'off';
  }

  function setTheme(theme, persist) {
    state.theme = theme === 'light' ? 'light' : 'dark';
    document.documentElement.dataset.theme = state.theme;
    if (persist) {
      try { localStorage.setItem('cpr-theme', state.theme); } catch (err) { /* ignore */ }
    }
    const meta = document.querySelector('meta[name="theme-color"]');
    if (meta) meta.setAttribute('content', state.theme === 'light' ? '#F2F3F6' : '#0E0F13');
    state.colorCache = null;
    state.graphDirty = true;
    refreshThemeButtons();
  }

  function setMode(mode, send) {
    if (!MODE_TEXT[mode]) return;
    const changed = state.mode !== mode;
    state.mode = mode;
    refreshModeButtons();
    if (changed) {
      staggerTiles();
      swapTiles();
    }
    if (send && state.transport) state.transport.send('mode ' + mode);
    if (state.sheet === 'summary') closeSheet();
  }

  function setProfile(profile, send) {
    if (!PROFILE_TEXT[profile]) return;
    state.profile = profile;
    if (state.cfg) state.cfg.prof = profile === 'child' ? 1 : 0;
    refreshProfileButtons();
    if (send && state.transport) state.transport.send('profile ' + profile);
  }

  function setMetronome(on, send) {
    state.metronome = !!on;
    if (els.metroBtn) {
      els.metroBtn.setAttribute('aria-pressed', state.metronome ? 'true' : 'false');
      els.metroBtn.setAttribute('aria-label', state.metronome ? 'ปิดเสียงจังหวะ' : 'เปิดเสียงจังหวะ');
    }
    if (els.metroSwitch) els.metroSwitch.setAttribute('aria-checked', state.metronome ? 'true' : 'false');
    if (send && state.transport) state.transport.send('metro ' + (state.metronome ? 'on' : 'off'));
  }

  function startSession() {
    if (!state.transport) {
      openConnect();
      showToast('เลือกวิธีเชื่อมต่อก่อนเริ่ม', 'warn');
      return;
    }
    if (state.sheet) closeSheet();
    state.lastSum = null;
    state.lastSumAt = null;
    resetSession(true);
    state.transport.send('start');
    showToast('เริ่มรอบใหม่', 'ok');
  }

  function stopSession() {
    if (!state.transport) {
      openConnect();
      return;
    }
    if (state.sheet === 'summary') closeSheet();
    const seq = state.sumSeq;
    state.transport.send('stop');
    clearTimeout(state.stopTimer);
    state.stopTimer = setTimeout(() => {
      if (state.sumSeq === seq) showToast('หุ่นยังไม่ส่งผลมา', 'warn');
    }, 1800);
  }

  function pulseCounter() {
    if (instantMotion()) return;
    const targets = [els.countValue, els.countRingNum, els.countTestNum];
    targets.forEach((el) => {
      if (el) el.classList.remove('pulse');
    });
    void (els.countValue && els.countValue.offsetWidth);
    targets.forEach((el) => {
      if (el) el.classList.add('pulse');
    });
    clearTimeout(state.pulseTimer);
    state.pulseTimer = setTimeout(() => {
      targets.forEach((el) => { if (el) el.classList.remove('pulse'); });
    }, 230);
  }

  function runStagger(targets) {
    if (!targets || !targets.length) return;
    const list = targets.slice(0, 8);
    state.staggerSeq += 1;
    const token = String(state.staggerSeq);
    list.forEach((el, index) => {
      el.classList.remove('is-enter');
      el.style.setProperty('--enter-i', String(index));
      el.dataset.stagger = token;
    });
    if (instantMotion()) {
      list.forEach((el) => {
        el.classList.remove('is-enter');
        el.style.removeProperty('--enter-i');
        delete el.dataset.stagger;
      });
      return;
    }
    void list[0].offsetWidth;
    list.forEach((el) => el.classList.add('is-enter'));
    setTimeout(() => {
      list.forEach((el) => {
        if (el.dataset.stagger !== token) return;
        el.classList.remove('is-enter');
        el.style.removeProperty('--enter-i');
        delete el.dataset.stagger;
      });
    }, 320 + list.length * 40 + 80);
  }

  function staggerCards() {
    return $$('.screen .card').slice(0, 8);
  }

  function staggerTiles() {
    if (!els.tiles) return;
    runStagger($$('.tile', els.tiles));
  }

  function swapTiles() {
    if (!els.tiles || instantMotion()) return;
    const tiles = $$('.tile', els.tiles);
    if (!tiles.length) return;
    tiles.forEach((el) => el.classList.remove('is-swap'));
    void tiles[0].offsetWidth;
    tiles.forEach((el) => el.classList.add('is-swap'));
    clearTimeout(state.swapTimer);
    state.swapTimer = setTimeout(() => {
      tiles.forEach((el) => el.classList.remove('is-swap'));
    }, 220);
  }

  function updateCta() {
    const active = state.sessionActive;
    setText(els.ctaTitle, active ? 'จบรอบ' : 'เริ่มฝึก');
    setText(els.ctaSub, MODE_SUB[state.mode] || MODE_SUB.free);
    setUse(els.ctaIconUse, active ? 'square' : 'play');
  }

  function updateHero() {
    const info = unitInfo();
    const value = heroPeak();
    setText(els.depthValue, value.toFixed(1));

    const level = levelForDepth(value, info);
    const tone = toneForLevel(level);
    let text = 'พร้อม';
    let hint = 'เริ่มกดได้เลย';
    if (level === 1) {
      text = 'ตื้นไป';
      hint = 'กดลึกอีกนิด';
    } else if (level === 2) {
      text = 'พอดี';
      hint = 'ดีมาก';
    } else if (level === 3) {
      text = 'ลึกไป';
      hint = 'เบาลงหน่อย';
    }
    setPill(els.depthPill, els.depthPillText, els.depthPillIcon, text, tone);
    setText(els.depthHint, hint);
    setState(els.heroCard, tone);
    updateRuler(tone, state.display.depth, info);
  }

  function updateRuler(tone, value, info) {
    if (!els.ruler) return;
    if (els.ruler.dataset.tone !== tone) els.ruler.dataset.tone = tone;
    const width = state.rulerWidth || els.ruler.clientWidth || 300;
    const fraction = clamp(value / Math.max(.5, info.scaleMax), 0, 1);
    if (els.rulerMarker) {
      els.rulerMarker.style.transform = 'translateX(' + (fraction * width - 2).toFixed(1) + 'px)';
    }
    if (els.rulerCaret) {
      const peakValue = heroPeak();
      if (peakValue > 0) {
        const peak = clamp(peakValue / Math.max(.5, info.scaleMax), 0, 1);
        els.rulerCaret.style.opacity = '1';
        els.rulerCaret.style.transform = 'translateX(' + (peak * width).toFixed(1) + 'px) translateX(-50%)';
      } else {
        els.rulerCaret.style.opacity = '0';
      }
    }
  }

  function updateRate() {
    const bpm = state.frame && has(state.frame.bpm) ? state.frame.bpm : 0;
    setText(els.rateValue, bpm > 0 ? String(Math.round(bpm)) : '—');

    let tone = 'idle';
    let text = 'รอ';
    if (bpm > 0) {
      if (state.rateLevel === 1) {
        tone = 'warn';
        text = 'ช้าไป';
      } else if (state.rateLevel === 2) {
        tone = 'ok';
        text = 'พอดี';
      } else if (state.rateLevel === 3) {
        tone = 'bad';
        text = 'เร็วไป';
      }
    }
    setPill(els.ratePill, els.ratePillText, els.ratePillIcon, text, tone);
    setState(els.rateTile, tone);

    if (els.rateDot) {
      const shown = clamp(state.display.rate, 60, 160);
      const fraction = (shown - 60) / 100;
      const trackWidth = els.rateTrack ? els.rateTrack.clientWidth : 0;
      const travel = Math.max(0, trackWidth - 10);
      els.rateDot.style.transform = 'translate(' + (fraction * travel).toFixed(1) + 'px, -50%)';
      els.rateDot.style.opacity = bpm > 0 ? '1' : '0';
      const dotTone = state.rateLevel === 1 ? 'is-warn'
        : state.rateLevel === 2 ? 'is-ok'
          : state.rateLevel === 3 ? 'is-bad' : '';
      const next = 'mini-dot' + (dotTone ? ' ' + dotTone : '');
      if (els.rateDot.className !== next) els.rateDot.className = next;
    }
  }

  function renderRecoilDots() {
    if (!els.recoilDots) return;
    const last = state.compressions.slice(-10);
    const key = last.map((c) => c.i + ':' + (c.rc ? 1 : 0)).join('|') + '#' + state.compressions.length;
    if (key === state.dotsKey) return;
    state.dotsKey = key;
    let html = '';
    for (let i = last.length; i < 10; i += 1) html += '<span class="recoil-dot"></span>';
    last.forEach((comp, index) => {
      const cls = comp.rc ? 'ok' : 'warn';
      const isNew = index === last.length - 1 ? ' is-new' : '';
      html += '<span class="recoil-dot ' + cls + isNew + '"></span>';
    });
    els.recoilDots.innerHTML = html;
  }

  function updateRecoil() {
    const last = state.lastCompression;
    const recent = state.compressions.slice(-10);
    const good = recent.reduce((sum, comp) => sum + (comp.rc ? 1 : 0), 0);
    if (recent.length) setText(els.recoilValue, good + '/' + recent.length);
    else setText(els.recoilValue, '—');

    let tone = 'idle';
    let text = 'รอ';
    if (last) {
      tone = last.rc ? 'ok' : 'warn';
      text = last.rc ? 'คืนสุด' : 'ยังค้าง';
    }
    setPill(els.recoilPill, els.recoilPillText, els.recoilPillIcon, text, tone);
    setState(els.recoilTile, tone);
    renderRecoilDots();
  }

  function updateHand() {
    let tone = 'idle';
    let text = '—';
    let ok = false;
    if (state.frame) {
      ok = !!state.frame.hand;
      tone = ok ? 'ok' : 'bad';
      text = ok ? 'ถูกจุด' : 'ผิดจุด';
    }
    setText(els.handValue, text);
    setState(els.handTile, tone);
    if (els.handTarget) {
      els.handTarget.classList.toggle('is-ok', ok);
      els.handTarget.classList.toggle('is-bad', !!state.frame && !ok);
    }
  }

  function updateCount() {
    els.countBlocks.forEach((block) => {
      block.hidden = block.dataset.count !== state.mode;
    });

    const frame = state.frame;
    if (state.mode === 'free') {
      let good = 0;
      state.compressions.forEach((comp) => { if (comp.ok) good += 1; });
      setText(els.countValue, String(state.count));
      setText(els.countGood, 'ถูก ' + good);
    } else if (state.mode === 'cycle') {
      const cc = clamp(state.cc, 0, 30);
      const fraction = cc / 30;
      setText(els.countRingNum, String(cc));
      if (els.countRing) {
        els.countRing.style.strokeDashoffset = (100 - fraction * 100).toFixed(2);
        els.countRing.style.stroke = cc >= 30 ? 'var(--ok)' : 'var(--accent)';
      }
      setText(els.countCycle, 'รอบที่ ' + (state.cy + 1));
    } else {
      const remaining = clamp(120 - state.elapsed, 0, 120);
      const fraction = remaining / 120;
      setText(els.countTestNum, fmtDuration(remaining));
      if (els.countTestRing) {
        els.countTestRing.style.strokeDashoffset = (100 - fraction * 100).toFixed(2);
      }
    }
    setState(els.countTile, frame && state.sessionActive ? 'ok' : 'idle');
  }

  function toggleBanner(el, show) {
    if (!el) return;
    const key = el.id || el;
    const timer = state.bannerTimers.get(key);
    if (timer) {
      clearTimeout(timer);
      state.bannerTimers.delete(key);
    }
    if (show) {
      if (el.hidden) {
        el.hidden = false;
        void el.offsetWidth;
      }
      el.classList.add('is-show');
    } else {
      el.classList.remove('is-show');
      const hide = setTimeout(() => {
        el.hidden = true;
        state.bannerTimers.delete(key);
      }, 170);
      state.bannerTimers.set(key, hide);
    }
  }

  function updateBanners() {
    const frame = state.frame;
    if (state.pauseActive && frame) {
      const start = has(state.pauseStart) ? state.pauseStart : frame.t - HANDS_OFF_MS;
      const elapsed = Math.max(0, frame.t - start);
      const tooLong = elapsed >= 10000;
      if (els.pauseBanner) els.pauseBanner.classList.toggle('is-long', tooLong);
      setText(els.pauseText, tooLong
        ? 'หยุดนานเกิน 10 วิ'
        : 'หยุดกด ' + Math.floor(elapsed / 1000) + ' วิ');
      toggleBanner(els.pauseBanner, true);
    } else {
      toggleBanner(els.pauseBanner, false);
    }

    const breathing = state.mode === 'cycle' && state.sessionActive && state.cc >= 30;
    toggleBanner(els.breathBanner, breathing);
    toggleBanner(els.warnSps, state.lowSps);
    toggleBanner(els.warnHx, !!(state.cfg && state.cfg.hx === 0));

    if (!state.overload && frame && has(frame.t) && frame.t > state.overloadUntil) {
      state.overload = false;
    }
    toggleBanner(els.warnOverload, state.overload);
  }

  function drawGraph() {
    const canvas = els.depthGraph;
    const wrap = els.graphWrap;
    if (!canvas || !wrap) return;
    const rect = wrap.getBoundingClientRect();
    if (rect.width < 8 || rect.height < 8) return;

    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const pixelWidth = Math.round(rect.width * dpr);
    const pixelHeight = Math.round(rect.height * dpr);
    if (canvas.width !== pixelWidth || canvas.height !== pixelHeight) {
      canvas.width = pixelWidth;
      canvas.height = pixelHeight;
    }
    const ctx = canvas.getContext('2d');
    if (!ctx) return;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, rect.width, rect.height);

    const history = state.history;
    if (history.length < 2) {
      if (els.graphEmpty) els.graphEmpty.hidden = false;
      return;
    }
    if (els.graphEmpty) els.graphEmpty.hidden = true;

    const c = colors();
    const info = unitInfo();
    const max = info.scaleMax;
    const pad = { l: 38, r: 10, t: 8, b: 22 };
    const width = Math.max(10, rect.width - pad.l - pad.r);
    const height = Math.max(10, rect.height - pad.t - pad.b);
    const nowT = history[history.length - 1].t;
    const startT = nowT - 10000;
    const xOf = (t) => pad.l + clamp((t - startT) / 10000, 0, 1) * width;
    const yOf = (v) => pad.t + clamp(v / max, 0, 1) * height;

    const bandTop = yOf(info.good);
    const bandBottom = yOf(info.deep);
    ctx.fillStyle = c.okTint;
    ctx.fillRect(pad.l, bandTop, width, Math.max(1, bandBottom - bandTop));
    ctx.strokeStyle = c.ok;
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(pad.l, bandTop);
    ctx.lineTo(pad.l + width, bandTop);
    ctx.stroke();

    ctx.font = '12px "Anuphan", "Noto Sans Thai", sans-serif';
    ctx.textBaseline = 'middle';
    ctx.textAlign = 'right';
    const ticks = height < 55 ? 2 : 4;
    for (let i = 0; i <= ticks; i += 1) {
      const y = pad.t + height * i / ticks;
      ctx.strokeStyle = c.surface3;
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(pad.l, y);
      ctx.lineTo(pad.l + width, y);
      ctx.stroke();
      ctx.fillStyle = c.text3;
      ctx.fillText(tickNum(max * i / ticks), pad.l - 6, y);
    }

    ctx.strokeStyle = c.surface3;
    for (let i = 0; i <= 2; i += 1) {
      const x = pad.l + width * i / 2;
      ctx.beginPath();
      ctx.moveTo(x, pad.t);
      ctx.lineTo(x, pad.t + height);
      ctx.stroke();
    }
    ctx.textBaseline = 'top';
    ctx.fillStyle = c.text3;
    ctx.textAlign = 'left';
    ctx.fillText('-10 วิ', pad.l, pad.t + height + 5);
    ctx.textAlign = 'center';
    ctx.fillText('-5 วิ', pad.l + width / 2, pad.t + height + 5);
    ctx.textAlign = 'right';
    ctx.fillText('ตอนนี้', pad.l + width, pad.t + height + 5);

    const first = history[0];
    const last = history[history.length - 1];
    ctx.beginPath();
    ctx.moveTo(xOf(first.t), pad.t + height);
    history.forEach((point) => {
      ctx.lineTo(xOf(point.t), yOf(point.v));
    });
    ctx.lineTo(xOf(last.t), pad.t + height);
    ctx.closePath();
    ctx.globalAlpha = .08;
    ctx.fillStyle = c.accent;
    ctx.fill();
    ctx.globalAlpha = 1;

    ctx.beginPath();
    history.forEach((point, index) => {
      const x = xOf(point.t);
      const y = yOf(point.v);
      if (index === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    });
    ctx.strokeStyle = c.accent;
    ctx.lineWidth = 2.5;
    ctx.lineJoin = 'round';
    ctx.lineCap = 'round';
    ctx.stroke();

    state.compressions.forEach((comp) => {
      const dotT = has(comp.tp) ? comp.tp : comp.t;
      if (!has(dotT) || dotT < startT - 500 || dotT > nowT + 500) return;
      const level = comp.dl || 0;
      const fill = level === 1 ? c.warn : level === 3 ? c.bad : level === 2 ? c.ok : c.accent;
      const x = xOf(dotT);
      const y = yOf(toDisplay(comp.pk));
      ctx.beginPath();
      ctx.arc(x, y, 4.5, 0, Math.PI * 2);
      ctx.fillStyle = fill;
      ctx.fill();
      ctx.strokeStyle = c.surface;
      ctx.lineWidth = 2;
      ctx.stroke();
    });

    if (last) {
      ctx.beginPath();
      ctx.arc(xOf(last.t), yOf(last.v), 2.8, 0, Math.PI * 2);
      ctx.fillStyle = c.accent;
      ctx.fill();
    }
  }

  let lastTick = 0;
  function tick(now) {
    const dt = lastTick ? Math.min(80, now - lastTick) : 16;
    lastTick = now;
    const reduce = instantMotion();
    const k = reduce ? 1 : 1 - Math.exp(-dt / 60);
    state.display.depth += (state.targetDepth - state.display.depth) * k;
    state.display.rate += (state.targetRate - state.display.rate) * k;
    if (reduce || Math.abs(state.targetDepth - state.display.depth) < .005) {
      state.display.depth = state.targetDepth;
    }
    if (reduce || Math.abs(state.targetRate - state.display.rate) < .05) {
      state.display.rate = state.targetRate;
    }

    updateHero();
    updateRate();
    updateRecoil();
    updateHand();
    updateCount();
    updateBanners();
    updateCta();
    if (state.sheet === 'th') thUpdateLive();
    if (state.graphDirty) {
      drawGraph();
      state.graphDirty = false;
    }
    requestAnimationFrame(tick);
  }

  function showToast(text, tone) {
    const toast = els.toast;
    if (!toast) return;
    setText(toast, text);
    toast.dataset.tone = tone || '';
    toast.hidden = false;
    void toast.offsetWidth;
    toast.classList.add('is-show');
    clearTimeout(state.toastTimer);
    state.toastTimer = setTimeout(() => {
      toast.classList.remove('is-show');
      state.toastTimer = setTimeout(() => { toast.hidden = true; }, 180);
    }, tone === 'bad' ? 4800 : 3000);
  }

  function setNavActive(index) {
    if (els.appNav) els.appNav.style.setProperty('--nav-i', index);
    els.navButtons.forEach((button) => {
      const active = Number(button.dataset.navIndex || -1) === index;
      button.classList.toggle('is-active', active);
      if (active) button.setAttribute('aria-current', 'page');
      else button.removeAttribute('aria-current');
    });
  }

  function navIndexForSheet(name) {
    if (name === 'summary') return 1;
    if (name === 'settings') return 2;
    return 0;
  }

  function openSheet(name) {
    const sheet = els.sheetMap[name];
    if (!sheet || !els.sheetLayer) return;
    clearTimeout(state.sheetTimer);
    if (!state.sheet) state.lastFocus = document.activeElement;

    Object.keys(els.sheetMap).forEach((key) => {
      const other = els.sheetMap[key];
      if (!other || key === name) return;
      other.hidden = true;
      other.classList.remove('is-open');
    });

    const opening = els.sheetLayer.hidden;
    if (opening) {
      els.sheetLayer.hidden = false;
      els.sheetLayer.classList.remove('is-open');
    }
    sheet.hidden = false;
    state.sheet = name;
    if (opening) {
      void sheet.offsetWidth;
      requestAnimationFrame(() => {
        if (state.sheet === name && els.sheetLayer && !els.sheetLayer.hidden) {
          els.sheetLayer.classList.add('is-open');
        }
      });
    } else {
      els.sheetLayer.classList.add('is-open');
    }
    setNavActive(navIndexForSheet(name));
    document.body.style.overflow = 'hidden';
    setTimeout(() => {
      try { sheet.focus({ preventScroll: true }); } catch (err) { sheet.focus(); }
    }, 30);
    state.graphDirty = true;
  }

  function closeSheet() {
    if (!state.sheet && els.sheetLayer && els.sheetLayer.hidden) return;
    state.sheet = null;
    if (els.sheetLayer) els.sheetLayer.classList.remove('is-open');
    clearTimeout(state.sheetTimer);
    state.sheetTimer = setTimeout(() => {
      if (els.sheetLayer) els.sheetLayer.hidden = true;
      Object.keys(els.sheetMap).forEach((key) => {
        if (els.sheetMap[key]) els.sheetMap[key].hidden = true;
      });
    }, 400);
    document.body.style.overflow = '';
    const focus = state.lastFocus;
    state.lastFocus = null;
    if (focus && typeof focus.focus === 'function') {
      try { focus.focus({ preventScroll: true }); } catch (err) { focus.focus(); }
    }
    setNavActive(0);
    state.graphDirty = true;
  }

  function openConnect() {
    const serial = 'serial' in navigator;
    if (els.connectSerial) {
      els.connectSerial.disabled = !serial;
      if (serial) els.connectSerial.removeAttribute('aria-disabled');
      else els.connectSerial.setAttribute('aria-disabled', 'true');
    }
    setText(els.serialHint, serial ? 'Chrome / Edge บนคอม' : 'เบราว์เซอร์นี้ไม่รองรับ');
    if (state.opts.demo && els.demoSelect) {
      try { els.demoSelect.value = state.opts.demo; } catch (err) { /* no such option */ }
    }
    refreshInfoPanels();
    openSheet('connect');
  }

  function openSettings() {
    refreshInfoPanels();
    refreshThemeButtons();
    refreshProfileButtons();
    setMetronome(state.metronome, false);
    openSheet('settings');
  }

  function openSummary() {
    if (state.lastSum) {
      if (els.summaryContent) els.summaryContent.hidden = false;
      if (els.summaryEmpty) els.summaryEmpty.hidden = true;
      if (els.summaryFoot) els.summaryFoot.hidden = false;
      populateSummary();
    } else {
      if (els.summaryContent) els.summaryContent.hidden = true;
      if (els.summaryEmpty) els.summaryEmpty.hidden = false;
      if (els.summaryFoot) els.summaryFoot.hidden = true;
    }
    openSheet('summary');
    state.graphDirty = true;
  }

  function modeLabel(value) {
    const key = typeof value === 'number' ? MODE_KEYS[value] : value;
    return MODE_TEXT[key] || MODE_TEXT.free;
  }

  function metricTone(value, ccf) {
    if (ccf) {
      if (value >= 60) return 'ok';
      if (value >= 40) return 'warn';
      return 'bad';
    }
    if (value >= 80) return 'ok';
    if (value >= 50) return 'warn';
    return 'bad';
  }

  function animateScore(target) {
    if (state.scoreAnim) cancelAnimationFrame(state.scoreAnim);
    state.scoreAnim = null;
    const reduce = instantMotion();
    if (reduce) {
      setText(els.scoreValue, String(target));
      return;
    }
    const started = performance.now();
    const step = (now) => {
      const t = clamp((now - started) / 900, 0, 1);
      const eased = 1 - Math.pow(1 - t, 3);
      setText(els.scoreValue, String(Math.round(target * eased)));
      if (t < 1) state.scoreAnim = requestAnimationFrame(step);
      else setText(els.scoreValue, String(target));
    };
    state.scoreAnim = requestAnimationFrame(step);
  }

  function populateSummary() {
    const sum = state.lastSum;
    if (!sum) return;
    const info = unitInfo();
    const score = clamp(Number(sum.score) || 0, 0, 100);

    let grade = 'ฝึกอีกนิด';
    if (score >= 85) grade = 'ยอดเยี่ยม';
    else if (score >= 70) grade = 'ดี';
    else if (score >= 50) grade = 'พอใช้';

    let tone = 'bad';
    if (score >= 70) tone = 'ok';
    else if (score >= 50) tone = 'warn';
    setText(els.gradeWord, grade);
    els.gradeWord.dataset.tone = tone;

    let date = '';
    try {
      date = new Intl.DateTimeFormat('th-TH', {
        day: 'numeric', month: 'short'
      }).format(new Date());
    } catch (err) {
      date = new Date().toLocaleDateString();
    }
    const profile = PROFILE_TEXT[sum.prof === 1 ? 'child' : 'adult'];
    setText(els.summaryMeta, modeLabel(sum.mode) + ' · ' + profile + ' · ' + date);

    const n = Number(sum.n) || 0;
    const rated = Number(sum.rr) || 0;
    const metrics = [
      { box: els.metricDepth, valueEl: els.mDepth, ring: els.mDepthRing, value: pct(sum.dg, n) },
      { box: els.metricRate, valueEl: els.mRate, ring: els.mRateRing, value: pct(sum.rg, rated) },
      { box: els.metricRecoil, valueEl: els.mRecoil, ring: els.mRecoilRing, value: pct(sum.rc, n) },
      { box: els.metricHand, valueEl: els.mHand, ring: els.mHandRing, value: pct(sum.hd, n) },
      { box: els.metricCcf, valueEl: els.mCcf, ring: els.mCcfRing, value: clamp(Number(sum.ccf) || 0, 0, 100), ccf: true }
    ];

    state.metricTimers.forEach((timer) => clearTimeout(timer));
    state.metricTimers = [];

    if (els.scoreRing) {
      els.scoreRing.style.transition = 'none';
      els.scoreRing.style.strokeDashoffset = '100';
    }

    metrics.forEach((metric) => {
      if (metric.box) metric.box.dataset.tone = metricTone(metric.value, metric.ccf);
      setText(metric.valueEl, metric.value + '%');
      if (metric.ring) {
        metric.ring.style.transition = 'none';
        metric.ring.style.strokeDashoffset = '100';
      }
    });
    void (els.scoreRing && els.scoreRing.getBoundingClientRect());

    if (els.scoreRing) els.scoreRing.style.transition = '';
    metrics.forEach((metric) => {
      if (metric.ring) metric.ring.style.transition = '';
    });

    animateScore(score);

    const longestPause = Number(sum.lp) || 0;
    const facts = [
      { icon: 'repeat', label: 'จำนวน', value: n + ' ครั้ง' },
      { icon: 'clock', label: 'เวลา', value: fmtDuration(sum.dur) },
      { icon: 'activity', label: 'จังหวะเฉลี่ย', value: (Number(sum.bpm) || 0) + ' /นาที' },
      { icon: 'ruler', label: 'ลึกเฉลี่ย', value: depthText(toDisplay(sum.pk)) + ' ' + info.unit },
      { icon: 'clock', label: 'หยุดนานสุด', value: longestPause > 0
        ? (longestPause / 1000).toFixed(1) + ' วิ' : 'ไม่มี' }
    ];
    if (Number(sum.mode) === 1) {
      facts.push({ icon: 'repeat', label: 'รอบ 30:2', value: (Number(sum.cy) || 0) + ' รอบ' });
    }
    let factsHtml = '';
    facts.forEach((fact, index) => {
      factsHtml += '<div class="fact" style="animation-delay:' + (index * 40) + 'ms">' +
        '<span class="fact-ico"><svg class="ic" aria-hidden="true"><use href="#i-' +
        fact.icon + '"></use></svg></span>' +
        '<span class="fact-label">' + esc(fact.label) + '</span>' +
        '<strong class="fact-value">' + esc(fact.value) + '</strong></div>';
    });
    if (els.summaryFacts) els.summaryFacts.innerHTML = factsHtml;

    const reduce = instantMotion();
    requestAnimationFrame(() => {
      if (els.scoreRing) els.scoreRing.style.strokeDashoffset = String(100 - score);
      metrics.forEach((metric, index) => {
        if (!metric.ring) return;
        if (reduce) {
          metric.ring.style.strokeDashoffset = String(100 - clamp(metric.value, 0, 100));
          return;
        }
        const timer = setTimeout(() => {
          metric.ring.style.strokeDashoffset = String(100 - clamp(metric.value, 0, 100));
        }, 80 * index + 60);
        state.metricTimers.push(timer);
      });
    });
  }

  const INFO = {
    depth: {
      title: 'ความลึก',
      icon: 'ruler',
      body: () => '<p>เด็กกดลึกราว 5 ซม. (ประมาณ 1 ใน 3 ของความหนาหน้าอก) แต่ไม่เกิน 6 ซม. ผู้ใหญ่ 5–6 ซม.</p>' +
        '<p class="muted">อ้างอิงแนวทาง AHA ปี 2025</p>'
    },
    rate: {
      title: 'จังหวะ',
      icon: 'activity',
      body: () => '<p>กด 100–120 ครั้งต่อนาที ถ้าจับจังหวะยาก ให้เปิดเสียงจังหวะ 110 ที่ปุ่มโน้ตด้านบน</p>'
    },
    recoil: {
      title: 'คืนตัว',
      icon: 'arrow-up-from-line',
      body: () => '<p>ยกมือให้หน้าอกกลับขึ้นสุดทุกครั้ง อย่ากดค้างไว้ เลือดจะได้ไหลกลับเข้าหัวใจ</p>'
    },
    hand: {
      title: 'ตำแหน่งมือ',
      icon: 'hand',
      body: () => (state.cfg && state.cfg.hand === 0
        ? '<p>วางส้นมือกลางหน้าอก บนครึ่งล่างของกระดูกหน้าอก</p>'
        : '<p>วางส้นมือกลางหน้าอก บนครึ่งล่างของกระดูกหน้าอก ไฟตำแหน่งบนหุ่นจะติดเมื่อวางถูก</p>')
    },
    count: {
      title: 'การนับ',
      icon: 'repeat',
      body: () => '<p>อิสระ นับไปเรื่อยๆ · 30:2 กด 30 ครั้งแล้วให้ลม 2 ครั้ง · 2 นาที จับเวลาแล้วสรุปผลให้</p>'
    },
    ccf: {
      title: 'ต่อเนื่อง',
      icon: 'clock',
      body: () => '<p>ต่อเนื่อง คือสัดส่วนเวลาที่กดอยู่จริง ควรได้ 60% ขึ้นไป และหยุดแต่ละครั้งไม่เกิน 10 วินาที</p>'
    },
    sps: {
      title: 'เซนเซอร์อ่านช้า',
      icon: 'triangle-alert',
      body: () => '<p>10 ครั้งต่อวินาทีเป็นค่าปกติของบอร์ด HX711 ทั่วไป ถ้าอยากให้จับการคืนตัวไวขึ้น ย้ายขา RATE ไปต่อไฟเลี้ยง จะอ่านได้ 80 ครั้งต่อวินาที</p>'
    },
    score: {
      title: 'คะแนนมาจากไหน',
      icon: 'trophy',
      body: scoreBody
    },
    about: {
      title: 'เกี่ยวกับ',
      icon: 'info',
      body: aboutBody
    }
  };

  function scoreBody() {
    const weights = [
      ['ลึก', 30, 's1'],
      ['จังหวะ', 25, 's2'],
      ['คืนตัว', 20, 's3'],
      ['มือ', 10, 's4'],
      ['ต่อเนื่อง', 15, 's5']
    ];
    let bar = '';
    let legend = '';
    weights.forEach((item) => {
      bar += '<span class="score-seg ' + item[2] + '" style="width:' + item[1] + '%"></span>';
      legend += '<span><i class="' + item[2] + '"></i>' + item[0] + ' ' + item[1] + '%</span>';
    });
    return '<div class="score-bar">' + bar + '</div>' +
      '<div class="score-legend">' + legend + '</div>' +
      '<p>ต่อเนื่อง คือสัดส่วนเวลาที่กดอยู่จริง ควรได้ 60% ขึ้นไป และหยุดแต่ละครั้งไม่เกิน 10 วินาที</p>' +
      '<p class="muted">สัดส่วนคะแนนทีมตั้งเอง ไม่ใช่เกณฑ์ทางการ</p>';
  }

  function diagSummary() {
    const diag = state.lastDiag;
    if (!diag) return 'ระบบ · —';

    const up = has(diag.up) ? diag.up : 0;
    const minutes = Math.floor(up / 60);
    const upText = minutes >= 60
      ? Math.floor(minutes / 60) + ' ชม. ' + (minutes % 60) + ' นาที'
      : minutes >= 1 ? minutes + ' นาที' : Math.floor(up) + ' วิ';
    const sps = has(diag.sps) ? diag.sps : 0;
    const usb = diag.ser ? 'เปิด' : 'ปิด';
    const bad = has(diag.rxBad) ? diag.rxBad : 0;
    const dropped = (has(diag.qDrop) ? diag.qDrop : 0) +
      (has(diag.fDrop) ? diag.fDrop : 0);

    return 'ระบบ · เปิดมา ' + upText + ' · อ่าน ' + sps + '/วิ · สาย USB ' +
      usb + ' · ข้อมูลเสีย ' + bad + ' · หลุด ' + dropped;
  }

  function aboutBody() {
    const cfg = state.cfg;
    let board = '—';
    if (cfg && cfg.board) {
      const name = String(cfg.board);
      board = name === 'nano' ? 'Nano' : name === 'esp32' ? 'ESP32' : esc(name);
    }
    const fw = cfg && cfg.fw ? 'FW ' + esc(cfg.fw) : 'FW —';
    const transport = state.status === 'off' ? 'ไม่ได้ต่อ'
      : state.transportKind === 'demo' ? 'เดโม'
        : state.transportKind === 'serial' ? 'USB' : 'Wi-Fi';
    return '<p>หุ่นฝึก CPR เด็ก โครงงานโรงเรียน</p>' +
      '<div class="disclaimer"><svg class="ic" aria-hidden="true"><use href="#i-shield-check"></use></svg>' +
      '<span>สื่อการเรียนรู้ ไม่ใช่เครื่องมือแพทย์</span></div>' +
      '<p class="muted">' + board + ' · ' + fw + ' · ' + transport + '</p>' +
      '<p class="muted about-diag">' + esc(diagSummary()) + '</p>';
  }

  function populateInfo() {
    const topic = INFO[state.infoKey] || INFO.depth;
    setText(els.sheetInfoTitle, topic.title);
    setUse(els.infoHeadUse, topic.icon);
    if (els.sheetInfoBody) els.sheetInfoBody.innerHTML = topic.body();
  }

  function openInfo(key) {
    state.infoKey = INFO[key] ? key : 'depth';
    populateInfo();
    openSheet('info');
  }

  function openCalib() {
    const tof = state.cfg ? !!state.cfg.tof : true;
    state.cal = {
      step: 1,
      tof,
      active: false,
      presses: 0,
      weight: null,
      goodKg: null,
      deepKg: null,
      saved: false,
      pending: null,
      message: '',
      tone: ''
    };
    openSheet('calib');
    renderCalib();
  }

  function calStatus(text, tone) {
    if (!state.cal) return;
    state.cal.message = text || '';
    state.cal.tone = tone || '';
    const el = els.calibStatus;
    if (!el) return;
    if (!text) {
      el.hidden = true;
      el.textContent = '';
      return;
    }
    el.hidden = false;
    el.dataset.tone = tone || '';
    setText(el, text);
  }

  function calActions(html) {
    if (els.calibActions) els.calibActions.innerHTML = html || '';
  }

  function calBadge(icon, tone) {
    return '<span class="calib-badge' + (tone ? ' is-' + tone : '') + '">' +
      '<svg class="ic" aria-hidden="true"><use href="#i-' + icon + '"></use></svg></span>';
  }

  function renderCalib() {
    const cal = state.cal;
    if (!cal) return;

    $$('.step', els.calibStepper).forEach((step) => {
      const n = Number(step.dataset.step);
      step.classList.toggle('active', !cal.saved && n === cal.step);
      step.classList.toggle('done', cal.saved || n < cal.step);
    });

    let panel = '';
    if (cal.saved) {
      panel = '<div class="calib-success">' + calBadge('circle-check', 'ok') +
        '<h3>เสร็จแล้ว</h3></div>';
      calActions('<button type="button" class="btn btn-primary" data-cal-action="close">ปิด</button>');
    } else if (cal.step === 1) {
      panel = calBadge('target') +
        '<h3>ยกมือออกจากหุ่น</h3>' +
        '<p>แล้วกดตั้งศูนย์</p>' +
        '<button type="button" class="btn btn-primary" data-cal-action="tare">ตั้งศูนย์</button>';
      calActions('');
    } else if (cal.step === 2) {
      panel = calBadge('scale') +
        '<h3>วางของที่รู้น้ำหนัก</h3>' +
        '<p>ใส่น้ำหนักเป็นกิโลกรัม</p>' +
        '<label class="field"><span>น้ำหนัก (กก.)</span>' +
        '<input id="calWeightInput" type="number" min="0.2" max="20" step="0.1" inputmode="decimal" value="' +
        (has(cal.weight) ? cal.weight.toFixed(1) : '2.0') + '"></label>' +
        '<button type="button" class="btn btn-primary" data-cal-action="weight">บันทึก</button>';
      calActions('<button type="button" class="btn btn-ghost" data-cal-action="back">ย้อนกลับ</button>');
    } else if (cal.step === 3 && cal.tof) {
      panel = calBadge('ruler') +
        '<h3>กดช้าๆ ให้ลึกราว 6 ซม.</h3>' +
        '<p>กดหลายๆ ครั้งจนตัวเลขขึ้น</p>' +
        '<div class="cal-counter"><span id="calPairCount">' + cal.presses + '</span>' +
        '<small>ครั้งที่กด</small></div>' +
        '<button type="button" class="btn btn-primary" data-cal-action="finish">เสร็จ</button>';
      calActions('<button type="button" class="btn btn-ghost" data-cal-action="back">ย้อนกลับ</button>');
    } else if (cal.step === 3) {
      const next = !has(cal.goodKg) ? 0 : (!has(cal.deepKg) ? 1 : -1);
      panel = calBadge('ruler') +
        '<h3>กดให้ลึก 5 ซม.</h3>' +
        '<p>วัดด้วยไม้บรรทัด แล้วกดเก็บค่า</p>' +
        '<div class="band-capture">' +
        '<div class="band-value' + (next === 0 ? ' is-next' : '') + '">' +
        '<span>5 ซม.</span><strong>' +
        (has(cal.goodKg) ? cal.goodKg.toFixed(1) + ' กก.' : '—') + '</strong></div>' +
        '<div class="band-value' + (next === 1 ? ' is-next' : '') + '">' +
        '<span>6 ซม.</span><strong>' +
        (has(cal.deepKg) ? cal.deepKg.toFixed(1) + ' กก.' : '—') + '</strong></div>' +
        '</div>' +
        '<button type="button" class="btn btn-primary" data-cal-action="peak">เก็บค่า</button>';
      calActions('<button type="button" class="btn btn-ghost" data-cal-action="back">ย้อนกลับ</button>');
    } else {
      panel = calBadge('circle-check') +
        '<h3>บันทึกลงหุ่น</h3>' +
        '<button type="button" class="btn btn-primary" data-cal-action="save">บันทึก</button>';
      calActions('<button type="button" class="btn btn-ghost" data-cal-action="back">ย้อนกลับ</button>');
    }
    if (els.calibPanel) els.calibPanel.innerHTML = panel;
    calStatus(cal.message, cal.tone);
  }

  function calSend(line, cmd, onOk) {
    if (!state.transport || state.status === 'off') {
      calStatus('ยังไม่ได้เชื่อมต่อหุ่น', 'bad');
      showToast('ยังไม่ได้เชื่อมต่อหุ่น', 'bad');
      return false;
    }
    state.cal.pending = { cmd, onOk };
    state.cal.message = '';
    state.cal.tone = '';
    const statusEl = els.calibStatus;
    if (statusEl) statusEl.hidden = true;
    state.transport.send(line);
    return true;
  }

  function calHandleAck(msg) {
    const cal = state.cal;
    if (!cal || !cal.pending) return false;
    if (cal.pending.cmd !== msg.cmd) return false;
    const pending = cal.pending;
    cal.pending = null;
    if (msg.ok) {
      if (pending.onOk) pending.onOk(msg);
    } else {
      calStatus(msg.msg || 'สั่งหุ่นไม่ได้', 'bad');
    }
    return true;
  }

  function beginCalRecording() {
    calStatus('กำลังเริ่มบันทึก…', 'ok');
    calSend('cal start', 'cal start', () => {
      state.cal.active = true;
      state.cal.presses = 0;
      calStatus('กำลังบันทึก… กดช้าๆ หลายครั้ง', 'ok');
      renderCalib();
    });
  }

  function handleCalAction(action) {
    const cal = state.cal;
    if (!cal) return;
    if (action === 'tare') {
      calSend('tare', 'tare', () => {
        state.cal.step = 2;
        calStatus('ตั้งศูนย์แล้ว', 'ok');
        renderCalib();
      });
    } else if (action === 'weight') {
      const input = document.getElementById('calWeightInput');
      const kg = input ? parseFloat(input.value) : NaN;
      if (!has(kg) || kg < .2 || kg > 20) {
        calStatus('ใส่น้ำหนัก 0.2–20 กก.', 'bad');
        return;
      }
      calSend('cal weight ' + kg.toFixed(1), 'cal weight', () => {
        state.cal.weight = kg;
        state.cal.goodKg = null;
        state.cal.deepKg = null;
        state.cal.step = 3;
        calStatus('บันทึกน้ำหนักแล้ว', 'ok');
        renderCalib();
        if (state.cal.tof) beginCalRecording();
      });
    } else if (action === 'finish') {
      calSend('cal finish', 'cal finish', () => {
        state.cal.active = false;
        state.cal.step = 4;
        calStatus('คำนวณช่วงกดแล้ว', 'ok');
        renderCalib();
      });
    } else if (action === 'peak') {
      calSend('cal peak', 'cal peak', (ack) => {
        const kg = parseFloat(ack.msg);
        if (!has(kg)) {
          calStatus('อ่านค่าน้ำหนักไม่ได้', 'bad');
          renderCalib();
          return;
        }
        if (!has(cal.goodKg)) {
          cal.goodKg = kg;
          calStatus('เก็บค่า 5 ซม. แล้ว', 'ok');
          renderCalib();
          return;
        }
        if (!has(cal.deepKg)) {
          if (kg <= cal.goodKg) {
            calStatus('ค่า 6 ซม. ต้องมากกว่าค่า 5 ซม.', 'bad');
            renderCalib();
            return;
          }
          cal.deepKg = kg;
          calSend('cal bands ' + cal.goodKg.toFixed(1) + ' ' + cal.deepKg.toFixed(1),
            'cal bands', () => {
              calStatus('ตั้งช่วงความลึกแล้ว', 'ok');
              cal.step = 4;
              renderCalib();
            });
        }
      });
    } else if (action === 'save') {
      calSend('cal save', 'cal save', () => {
        state.cal.saved = true;
        state.cal.step = 4;
        calStatus('', '');
        renderCalib();
      });
    } else if (action === 'back') {
      if (state.cal.pending) return;
      if (state.cal.step === 3) {
        if (state.cal.active && state.transport) state.transport.send('cal cancel');
        state.cal.active = false;
        state.cal.goodKg = null;
        state.cal.deepKg = null;
        state.cal.step = 2;
      } else if (state.cal.step === 4) {
        if (!state.cal.tof) state.cal.deepKg = null;
        state.cal.step = 3;
      } else {
        state.cal.step = Math.max(1, state.cal.step - 1);
      }
      calStatus('', '');
      renderCalib();
    } else if (action === 'close') {
      closeSheet();
    }
  }

  function downloadCsv() {
    const sum = state.lastSum;
    const info = unitInfo();
    const unit = info.kg ? 'kg' : 'mm';
    const depthNames = ['none', 'shallow', 'good', 'deep'];
    const rateNames = ['none', 'slow', 'good', 'fast'];
    const rows = [];
    rows.push('index,time_s,peak,unit,depth_level,bpm,rate_level,recoil_ok,hand_ok,all_ok');
    const firstT = state.compressions.length ? state.compressions[0].t : 0;
    state.compressions.forEach((comp) => {
      rows.push([
        comp.i,
        ((comp.t - firstT) / 1000).toFixed(1),
        has(comp.pk) ? Number(comp.pk).toFixed(1) : '',
        unit,
        depthNames[comp.dl] || 'none',
        comp.bpm || 0,
        rateNames[comp.rl] || 'none',
        comp.rc ? 1 : 0,
        comp.hd ? 1 : 0,
        comp.ok ? 1 : 0
      ].join(','));
    });
    if (sum) {
      rows.push('');
      rows.push('summary_field,value');
      const summaryRows = [
        ['score', sum.score],
        ['mode', modeLabel(sum.mode)],
        ['profile', PROFILE_TEXT[sum.prof === 1 ? 'child' : 'adult']],
        ['count', sum.n],
        ['depth_good', sum.dg],
        ['depth_shallow', sum.dsh],
        ['depth_deep', sum.dd],
        ['rate_rated', sum.rr],
        ['rate_good', sum.rg],
        ['rate_slow', sum.rs],
        ['rate_fast', sum.rf],
        ['recoil_good', sum.rc],
        ['hand_good', sum.hd],
        ['all_good', sum.ok],
        ['duration_s', sum.dur],
        ['hands_off_ms', sum.off],
        ['longest_pause_ms', sum.lp],
        ['pauses_too_long', sum.plong],
        ['mean_peak', sum.pk],
        ['mean_bpm', sum.bpm],
        ['cycles', sum.cy],
        ['ccf', sum.ccf]
      ];
      summaryRows.forEach((row) => {
        const value = row[1] == null ? '' : String(row[1]).replace(/,/g, ' ');
        rows.push(row[0] + ',' + value);
      });
    }
    const blob = new Blob(['\uFEFF' + rows.join('\r\n')], { type: 'text/csv;charset=utf-8' });
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    const stamp = new Date();
    const pad = (n) => String(n).padStart(2, '0');
    const name = 'cpr-session-' + stamp.getFullYear() + pad(stamp.getMonth() + 1) +
      pad(stamp.getDate()) + '-' + pad(stamp.getHours()) + pad(stamp.getMinutes()) + '.csv';
    link.href = url;
    link.download = name;
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
    setTimeout(() => URL.revokeObjectURL(url), 1200);
  }

  async function loadDemoManifest() {
    if (!els.demoSelect) return;
    const list = await fetchManifest();
    els.demoSelect.innerHTML = '';
    list.forEach((item) => {
      const option = document.createElement('option');
      option.value = item.id;
      option.textContent = item.title || item.id;
      els.demoSelect.appendChild(option);
    });
    if (state.opts.demo) {
      try { els.demoSelect.value = state.opts.demo; } catch (err) { /* no such option */ }
    }
  }

  function trapFocus(event) {
    if (!state.sheet) return;
    const sheet = els.sheetMap[state.sheet];
    if (!sheet) return;
    const nodes = Array.from(sheet.querySelectorAll(
      'button:not([disabled]), input:not([disabled]), select:not([disabled]), a[href], [tabindex]:not([tabindex="-1"])'
    )).filter((node) => node.getClientRects().length > 0);
    if (!nodes.length) return;
    const first = nodes[0];
    const last = nodes[nodes.length - 1];
    const active = document.activeElement;
    if (active === sheet || !sheet.contains(active)) {
      event.preventDefault();
      (event.shiftKey ? last : first).focus();
    } else if (event.shiftKey && active === first) {
      event.preventDefault();
      last.focus();
    } else if (!event.shiftKey && active === last) {
      event.preventDefault();
      first.focus();
    }
  }

  function isLocalPreview() {
    return location.protocol === 'file:' ||
      location.hostname === 'localhost' ||
      location.hostname === '127.0.0.1' ||
      location.hostname === '[::1]';
  }

  function wireEvents() {
    els.navButtons.forEach((button, index) => {
      button.dataset.navIndex = String(index);
      button.addEventListener('click', () => {
        const name = button.dataset.nav;
        if (name === 'live') {
          if (state.sheet) closeSheet();
          setNavActive(0);
          return;
        }
        if (name === 'summary') {
          openSummary();
          return;
        }
        openSettings();
      });
    });

    if (els.connPill) els.connPill.addEventListener('click', openConnect);

    els.modeButtons.forEach((button) => {
      button.addEventListener('click', () => setMode(button.dataset.mode, true));
    });
    els.profileButtons.forEach((button) => {
      button.addEventListener('click', () => setProfile(button.dataset.profile, true));
    });
    els.themeButtons.forEach((button) => {
      button.addEventListener('click', () => setTheme(button.dataset.themeSet, true));
    });

    if (els.ctaBtn) {
      els.ctaBtn.addEventListener('click', () => {
        if (state.sessionActive) stopSession();
        else startSession();
      });
    }
    if (els.metroBtn) els.metroBtn.addEventListener('click', () => setMetronome(!state.metronome, true));
    if (els.metroSwitch) els.metroSwitch.addEventListener('click', () => setMetronome(!state.metronome, true));

    document.addEventListener('click', (event) => {
      const info = event.target.closest('[data-info]');
      if (info) {
        event.preventDefault();
        openInfo(info.dataset.info);
        return;
      }
      if (event.target.closest('[data-close]')) closeSheet();
    });

    document.addEventListener('keydown', (event) => {
      if (event.key === 'Escape' && state.sheet) {
        closeSheet();
        return;
      }
      if (event.key === 'Tab') trapFocus(event);
    });

    if (els.connectWs) {
      els.connectWs.addEventListener('click', () => attachTransport(new WsTransport(), 'ws'));
    }
    if (els.connectSerial) {
      els.connectSerial.addEventListener('click', () => {
        if (els.connectSerial.disabled) return;
        attachTransport(new SerialTransport(), 'serial');
      });
    }
    if (els.connectDemo) {
      els.connectDemo.addEventListener('click', () => {
        const id = els.demoSelect && els.demoSelect.value ? els.demoSelect.value : 'good';
        attachTransport(new DemoSocket({
          id,
          speed: state.opts.speed || 1,
          seek: state.opts.seek || 0,
          mock: state.opts.mock
        }), 'demo');
      });
    }

    if (els.settingsConnectBtn) els.settingsConnectBtn.addEventListener('click', openConnect);
    if (els.connectDisconnectBtn) {
      els.connectDisconnectBtn.addEventListener('click', () => {
        if (state.transport) {
          try { state.transport.disconnect(); } catch (err) { /* ignore */ }
          state.transport = null;
        }
        state.transportKind = '';
        setStatus('off');
      });
    }
    if (els.openCalibBtn) els.openCalibBtn.addEventListener('click', openCalib);
    if (els.openThBtn) els.openThBtn.addEventListener('click', openTh);
    if (els.thResetBtn) els.thResetBtn.addEventListener('click', thReset);

    els.thStepButtons.forEach((button) => {
      button.addEventListener('click', () => {
        if (!state.th || button.disabled) return;
        const field = button.dataset.thStep;
        const dir = Number(button.dataset.thDir);
        const step = TH_STEP[field];
        if (!step) return;
        const candidate = Object.assign({}, state.th.values);
        candidate[field] = roundTh(candidate[field] + dir * step);
        if (!thValid(candidate)) return;
        state.th.values = candidate;
        thRender();
        thSchedule();
      });
    });

    els.thFiltButtons.forEach((button) => {
      button.addEventListener('click', () => {
        if (!state.th) return;
        const filt = parseFloat(button.dataset.thFilt);
        if (!has(filt)) return;
        state.th.values.filt = filt;
        thRender();
        thSchedule();
      });
    });

    if (els.csvBtn) els.csvBtn.addEventListener('click', downloadCsv);
    if (els.againBtn) {
      els.againBtn.addEventListener('click', () => {
        closeSheet();
        startSession();
      });
    }
    if (els.goPracticeBtn) els.goPracticeBtn.addEventListener('click', closeSheet);

    if (els.sheetCalib) {
      els.sheetCalib.addEventListener('click', (event) => {
        const button = event.target.closest('[data-cal-action]');
        if (button) handleCalAction(button.dataset.calAction);
      });
    }

    window.addEventListener('resize', () => {
      state.graphDirty = true;
      buildRuler(unitInfo());
    });
    if (els.graphWrap && 'ResizeObserver' in window) {
      const observer = new ResizeObserver(() => { state.graphDirty = true; });
      observer.observe(els.graphWrap);
    }
    if (els.ruler && 'ResizeObserver' in window) {
      const rulerObserver = new ResizeObserver(() => { buildRuler(unitInfo()); });
      rulerObserver.observe(els.ruler);
    }
  }

  function init() {
    cacheEls();
    state.theme = document.documentElement.dataset.theme === 'light' ? 'light' : 'dark';
    state.opts = parseUrl();
    if (state.opts.motion === 'freeze') {
      document.documentElement.dataset.motion = 'freeze';
    }
    if (state.opts.theme === 'light' || state.opts.theme === 'dark') {
      state.theme = state.opts.theme;
    }
    setTheme(state.theme, false);
    refreshModeButtons();
    refreshProfileButtons();
    refreshThemeButtons();
    setMetronome(false, false);
    refreshConfigUI();
    wireEvents();
    loadDemoManifest();

    if (state.opts.demo === 'cycle') state.mode = 'cycle';
    else if (state.opts.demo === 'test2min' || state.opts.demo === 'test') state.mode = 'test';
    else state.mode = 'free';
    refreshModeButtons();
    runStagger(staggerCards());

    requestAnimationFrame(tick);

    if (state.opts.ws) {
      attachTransport(new WsTransport(), 'ws');
    } else if (state.opts.demo) {
      attachTransport(new DemoSocket({
        id: state.opts.demo,
        speed: state.opts.speed,
        seek: state.opts.seek,
        mock: state.opts.mock
      }), 'demo');
    } else if (location.hostname === '192.168.4.1') {
      attachTransport(new WsTransport(), 'ws');
    } else if ((state.opts.sheet === 'summary' || state.opts.sheet === 'calib' ||
                state.opts.sheet === 'th') && isLocalPreview()) {
      attachTransport(new DemoSocket({
        id: 'good',
        speed: state.opts.speed,
        seek: state.opts.seek,
        mock: state.opts.mock
      }), 'demo');
    } else if (!state.opts.sheet) {
      setTimeout(() => {
        if (state.status === 'off') openConnect();
      }, 80);
    }

    if (!state.opts.ws && !state.opts.demo) {
      if (state.opts.sheet === 'connect') {
        setTimeout(() => {
          if (state.status === 'off') openConnect();
        }, 120);
      } else if (state.opts.sheet === 'settings') {
        setTimeout(openSettings, 120);
      } else if (state.opts.sheet === 'th') {
        setTimeout(openTh, 120);
      } else if (state.opts.sheet === 'info') {
        setTimeout(() => openInfo(state.opts.info || 'depth'), 120);
      }
    }
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }
})();
