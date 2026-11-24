'use strict';

(function () {
  const $ = (selector, root) => (root || document).querySelector(selector);
  const $$ = (selector, root) => Array.from((root || document).querySelectorAll(selector));
  const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
  const has = (v) => typeof v === 'number' && Number.isFinite(v);

  const STATUS_TEXT = {
    off: 'ไม่ได้เชื่อมต่อ',
    connecting: 'กำลังเชื่อมต่อ…',
    connected: 'เชื่อมต่อแล้ว',
    demo: 'เดโม'
  };

  const MODE_TEXT = { free: 'ฝึกอิสระ', cycle: 'รอบ 30:2', test: 'ทดสอบ 2 นาที' };
  const PROFILE_TEXT = { adult: 'ผู้ใหญ่', child: 'เด็ก' };
  const DEPTH_TEXT = { 0: 'none', 1: 'shallow', 2: 'good', 3: 'deep' };
  const RATE_TEXT = { 0: 'none', 1: 'slow', 2: 'good', 3: 'fast' };

  const state = {
    transport: null,
    status: 'off',
    demoTitle: '',
    cfg: null,
    mode: 'free',
    profile: 'adult',
    theme: 'dark',
    frame: null,
    history: [],
    compressions: [],
    lastCompression: null,
    lastSum: null,
    lastSumAt: null,
    sumSeq: 0,
    lastPeak: null,
    lastPeakT: null,
    pauseActive: false,
    pauseStart: null,
    overload: false,
    overloadUntil: 0,
    lowSps: false,
    sessionActive: false,
    metronome: false,
    liveDepth: 0,
    rateLevel: 0,
    phase: 0,
    hand: null,
    count: 0,
    cc: 0,
    cy: 0,
    elapsed: 0,
    sps: 0,
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
    colorCache: null
  };

  const els = {};

  function cacheEls() {
    const ids = [
      'connPill', 'connText', 'settingsBtn', 'depthChip', 'depthValue', 'depthUnit',
      'depthHint', 'targetText', 'lastPeakText', 'gaugeBand', 'gaugeFill', 'gaugePeak',
      'gaugeAxis', 'rateChip', 'rateArc', 'rateNeedle', 'rateValue', 'recoilIcon',
      'recoilText', 'recoilDots', 'handIcon', 'handText', 'counterChip', 'counterValue',
      'ringCount', 'counterRing', 'cycleText', 'cycleTotal', 'testCountdown', 'testCount',
      'breathBanner', 'breathText', 'depthGraph', 'graphEmpty', 'startBtn', 'stopBtn',
      'metroBtn', 'metroLabel', 'metroSwitch', 'sourceInfo', 'sourceText', 'pauseBanner',
      'pauseText', 'warnSps', 'warnOverload', 'toast', 'modalLayer', 'sheetConnect',
      'sheetSummary', 'sheetSettings', 'sheetCalib', 'demoSelect', 'connectWs',
      'connectSerial', 'serialHint', 'connectDemo', 'csvBtn', 'againBtn',
      'settingsConnText', 'settingsConnectBtn', 'settingsDisconnectBtn', 'openCalibBtn',
      'aboutMeta', 'calibStepper', 'calibPanel', 'calibStatus', 'calibActions',
      'calibSub', 'scoreRing', 'scoreValue', 'gradeWord', 'summaryMeta', 'mDepth',
      'mDepthBar', 'mRate', 'mRateBar', 'mRecoil', 'mRecoilBar', 'mHand', 'mHandBar',
      'summaryFacts'
    ];
    ids.forEach((id) => {
      els[id] = document.getElementById(id);
    });
    els.modeButtons = $$('[data-mode]');
    els.profileButtons = $$('[data-profile]');
    els.themeButtons = $$('[data-theme-set]');
    els.counterBlocks = $$('[data-counter]');
    els.graphWrap = els.depthGraph ? els.depthGraph.parentElement : null;
    els.sheetMap = {
      connect: els.sheetConnect,
      summary: els.sheetSummary,
      settings: els.sheetSettings,
      calib: els.sheetCalib
    };
  }

  function setText(el, text) {
    if (el && el.textContent !== text) el.textContent = text;
  }

  function setChip(el, text, tone) {
    if (!el) return;
    setText(el, text);
    const next = 'chip ' + (tone || 'chip-idle');
    if (el.className !== next) el.className = next;
  }

  function setStatusIcon(el, tone) {
    if (!el) return;
    const next = 'status-icon is-' + (tone || 'idle');
    if (el.className !== next) el.className = next;
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
        scaleMax: Math.max(20, Math.ceil(deep * 1.55 / 5) * 5, Math.ceil(overload * 1.1 / 5) * 5)
      };
    }
    const goodMm = has(cfg && cfg.gmin) ? cfg.gmin : 50;
    const deepMm = has(cfg && cfg.dmin) ? cfg.dmin : 60;
    const good = goodMm / 10;
    const deep = deepMm / 10;
    let max = 8;
    if (deep > 6.5) max = Math.ceil(deep * 1.2 / 2) * 2;
    return { kg: false, unit: 'ซม.', good, deep, scaleMax: Math.max(max, deep + 1) };
  }

  function toDisplay(value) {
    if (!has(value)) return 0;
    return unitInfo().kg ? value : value / 10;
  }

  function depthText(value) {
    return has(value) ? Number(value).toFixed(1) : '—';
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
      border: get('--border'),
      okBg: get('--ok-bg'),
      okBorder: get('--ok-border')
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
    return {
      demo: q.get('demo') || '',
      speed: has(speedRaw) && speedRaw > 0 ? clamp(speedRaw, .1, 20) : 1,
      seek: has(seekRaw) && seekRaw > 0 ? seekRaw : 0,
      theme: q.get('theme') || '',
      sheet: q.get('sheet') || '',
      ws: q.get('ws') === '1'
    };
  }

  async function fetchManifest() {
    try {
      const response = await fetch('demo/manifest.json', { cache: 'no-store' });
      if (!response.ok) throw new Error('manifest');
      const list = await response.json();
      if (Array.isArray(list) && list.length) return list;
    } catch (err) {
      /* fall through to the built-in entry */
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
      this.messages = [];
      this.index = 0;
      this.offsetAt = 0;
      this.startedAt = 0;
      this.loopLen = 1;
      this.sampleSum = null;
      this.cfg = null;
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
        if (!response.ok) throw new Error('โหลดไฟล์เดโมไม่สำเร็จ');
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
        this.onError && this.onError(err.message || 'เปิดโหมดเดโมไม่สำเร็จ');
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
      this.onInstantDone && this.onInstantDone();
      this.pump();
    }

    restart() {
      if (this.stopped) return;
      this.onReset && this.onReset();
      this.index = 0;
      this.offsetAt = 0;
      this.startedAt = performance.now();
      this.pump();
    }

    pump() {
      if (this.stopped) return;
      const now = performance.now();
      const deviceNow = this.offsetAt + (now - this.startedAt) * this.speed;
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
      this.pump();
    }

    emitAck(cmd, line) {
      let msg = 'ok';
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
      this.timer = this.timer || null;
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
      } catch (err) {
        this.stopped = true;
        this.onStatus && this.onStatus('off', { error: err.message });
        this.onError && this.onError('เชื่อมต่อสาย USB ไม่สำเร็จ');
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
      try { if (this.reader) await this.reader.cancel(); } catch (err) { /* ignore */ }
      try { if (this.writer) this.writer.releaseLock(); } catch (err) { /* ignore */ }
      try { if (this.port) await this.port.close(); } catch (err) { /* ignore */ }
      this.reader = null;
      this.writer = null;
      this.port = null;
      this.onStatus && this.onStatus('off');
    }
  }

  const HANDS_OFF_MS = 2000;

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

  function attachTransport(transport) {
    if (state.transport && state.transport !== transport) {
      try { state.transport.disconnect(); } catch (err) { /* ignore */ }
    }
    state.transport = transport;
    transport.onStatus = (status, detail) => setStatus(status, detail);
    transport.onMessage = (obj, meta) => handleMessage(obj, meta || {});
    transport.onReset = () => resetSession(true);
    transport.onInstantDone = () => completeInstant();
    transport.onError = (message) => showToast(message, 'bad');
    Promise.resolve(transport.connect()).catch((err) => {
      showToast(err && err.message ? err.message : 'เชื่อมต่อไม่สำเร็จ', 'bad');
    });
  }

  function resetSession(keepConfig) {
    state.frame = null;
    state.history = [];
    state.compressions = [];
    state.lastCompression = null;
    state.lastPeak = null;
    state.lastPeakT = null;
    state.pauseActive = false;
    state.pauseStart = null;
    state.overload = false;
    state.overloadUntil = 0;
    state.lowSps = false;
    state.sessionActive = false;
    state.liveDepth = 0;
    state.rateLevel = 0;
    state.phase = 0;
    state.hand = null;
    state.count = 0;
    state.cc = 0;
    state.cy = 0;
    state.elapsed = 0;
    state.sps = 0;
    state.targetDepth = 0;
    state.targetRate = 60;
    state.dotsKey = '';
    state.graphDirty = true;
    if (!keepConfig) state.cfg = null;
    renderRecoilDots();
    refreshConfigUI();
  }

  function applyCfg(msg) {
    state.cfg = Object.assign({}, msg);
    state.profile = msg.prof === 1 ? 'child' : 'adult';
    refreshProfileButtons();
    refreshConfigUI();
    refreshInfoPanels();
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
    state.phase = msg.ph || 0;
    state.liveDepth = msg.ld || 0;
    state.rateLevel = msg.rl || 0;
    state.hand = msg.hand ? 1 : 0;
    state.count = has(msg.n) ? msg.n : 0;
    state.cc = has(msg.cc) ? msg.cc : 0;
    state.cy = has(msg.cy) ? msg.cy : 0;
    state.elapsed = has(msg.el) ? msg.el : 0;
    state.sps = has(msg.sps) ? msg.sps : 0;
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
    state.lastPeakT = msg.t;
    state.dotsKey = '';
    state.graphDirty = true;

    if (state.cal && state.cal.active && state.cal.tof) {
      state.cal.presses += 1;
      const counter = document.getElementById('calPairCount');
      if (counter) setText(counter, String(state.cal.presses));
    }

    if (!meta.instant) pulseCounter();
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
    if (sheet === 'connect') {
      openConnect();
      return;
    }
    if (state.lastSum && state.opts.seek > 0 && has(state.lastSumAt) &&
        state.lastSumAt <= state.opts.seek * 1000) {
      openSummary();
    }
  }

  function handleAck(msg) {
    if (calHandleAck(msg)) return;
    if (!msg.ok) {
      showToast(msg.msg || 'คำสั่งไม่สำเร็จ', 'bad');
    }
  }

  function refreshModeButtons() {
    els.modeButtons.forEach((button) => {
      const active = button.dataset.mode === state.mode;
      button.classList.toggle('is-active', active);
      button.setAttribute('aria-pressed', active ? 'true' : 'false');
    });
  }

  function refreshProfileButtons() {
    els.profileButtons.forEach((button) => {
      const active = button.dataset.profile === state.profile;
      button.classList.toggle('is-active', active);
      button.setAttribute('aria-pressed', active ? 'true' : 'false');
    });
  }

  function refreshThemeButtons() {
    els.themeButtons.forEach((button) => {
      const active = button.dataset.themeSet === state.theme;
      button.classList.toggle('is-active', active);
      button.setAttribute('aria-pressed', active ? 'true' : 'false');
    });
  }

  function refreshConfigUI() {
    const info = unitInfo();
    if (els.depthUnit) setText(els.depthUnit, info.unit);
    setText(els.targetText, tickNum(info.good) + '–' + tickNum(info.deep) + ' ' + info.unit);

    if (els.gaugeBand) {
      const top = clamp((1 - info.deep / info.scaleMax) * 100, 0, 100);
      const height = clamp((info.deep - info.good) / info.scaleMax * 100, 0, 100);
      els.gaugeBand.style.top = top + '%';
      els.gaugeBand.style.height = height + '%';
    }

    if (els.gaugeAxis) {
      const ticks = 4;
      let html = '';
      for (let i = 0; i <= ticks; i += 1) {
        const value = info.scaleMax * i / ticks;
        html += '<span style="bottom:' + (i / ticks * 100) + '%">' + tickNum(value) + '</span>';
      }
      els.gaugeAxis.innerHTML = html;
    }
    refreshInfoPanels();
  }

  function refreshInfoPanels() {
    const connected = state.status !== 'off';
    if (els.sourceInfo) els.sourceInfo.classList.toggle('is-live', connected);
    if (!connected) {
      setText(els.sourceText, 'ยังไม่เชื่อมต่อ');
    } else {
      const src = state.frame ? state.frame.src : (state.cfg ? state.cfg.src : 0);
      const name = src === 2 ? 'ToF' : src === 1 ? 'แมปความลึก' : 'โหลดเซลล์';
      const sps = state.frame && has(state.frame.sps) && state.frame.sps > 0 ? state.frame.sps : 0;
      setText(els.sourceText, name + ' · ' + (sps ? sps + ' SPS' : 'รอข้อมูล'));
    }

    const statusText = STATUS_TEXT[state.status] || STATUS_TEXT.off;
    setText(els.settingsConnText, state.status === 'demo' && state.demoTitle
      ? 'เดโม · ' + state.demoTitle : statusText);
    if (els.settingsDisconnectBtn) els.settingsDisconnectBtn.hidden = state.status === 'off';
    if (els.settingsConnectBtn) setText(els.settingsConnectBtn, state.status === 'off' ? 'เชื่อมต่อ' : 'เปลี่ยน');

    const parts = [];
    if (state.cfg) {
      parts.push(state.cfg.board === 'nano' ? 'Nano' : 'ESP32');
      if (state.cfg.fw) parts.push('FW ' + state.cfg.fw);
    }
    if (state.status === 'demo') parts.push('เดโม');
    setText(els.aboutMeta, parts.length ? parts.join(' · ') : 'ยังไม่เชื่อมต่อ');
  }

  function setTheme(theme, persist) {
    state.theme = theme === 'light' ? 'light' : 'dark';
    document.documentElement.dataset.theme = state.theme;
    if (persist) {
      try { localStorage.setItem('cpr-theme', state.theme); } catch (err) { /* ignore */ }
    }
    const meta = document.querySelector('meta[name="theme-color"]');
    if (meta) meta.setAttribute('content', state.theme === 'light' ? '#F4F6FB' : '#0B1220');
    state.colorCache = null;
    state.graphDirty = true;
    refreshThemeButtons();
  }

  function setMode(mode, send) {
    if (!MODE_TEXT[mode]) return;
    state.mode = mode;
    refreshModeButtons();
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
    if (els.metroBtn) els.metroBtn.setAttribute('aria-pressed', state.metronome ? 'true' : 'false');
    if (els.metroSwitch) els.metroSwitch.setAttribute('aria-checked', state.metronome ? 'true' : 'false');
    const bpm = state.cfg && has(state.cfg.metro) ? state.cfg.metro : 110;
    setText(els.metroLabel, 'จังหวะ ' + bpm);
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
      if (state.sumSeq === seq) showToast('ยังไม่ได้รับผลสรุปจากหุ่น', 'warn');
    }, 1800);
  }

  function pulseCounter() {
    if (window.matchMedia('(prefers-reduced-motion: reduce)').matches) return;
    const targets = [els.counterValue, els.ringCount, els.testCountdown];
    targets.forEach((el) => {
      if (!el) return;
      el.classList.remove('pulse');
    });
    void (els.counterValue && els.counterValue.offsetWidth);
    targets.forEach((el) => {
      if (!el) return;
      el.classList.add('pulse');
    });
    clearTimeout(state.pulseTimer);
    state.pulseTimer = setTimeout(() => {
      targets.forEach((el) => { if (el) el.classList.remove('pulse'); });
    }, 230);
  }

  function updateHero() {
    const info = unitInfo();
    const value = state.display.depth;
    setText(els.depthValue, value.toFixed(1));

    let chipText = 'พร้อม';
    let chipTone = 'chip-idle';
    let hint = 'เริ่มกดเพื่อวัดความลึก';
    let color = 'var(--accent)';

    const live = state.frame;
    const level = state.liveDepth;
    if (!live && state.status === 'off') {
      hint = 'เชื่อมต่อหุ่นเพื่อเริ่มฝึก';
    } else if (live && (state.phase !== 0 || state.sessionActive)) {
      if (level === 1) {
        chipText = 'ตื้นไป'; chipTone = 'chip-warn'; color = 'var(--warn)';
        hint = 'กดให้ลึกอีกนิด';
      } else if (level === 2) {
        chipText = 'ลึกพอดี'; chipTone = 'chip-ok'; color = 'var(--ok)';
        hint = 'ความลึกอยู่ในช่วงเป้าหมาย';
      } else if (level === 3) {
        chipText = 'ลึกเกินไป'; chipTone = 'chip-bad'; color = 'var(--bad)';
        hint = 'ผ่อนแรงลงเล็กน้อย';
      } else if (state.pauseActive) {
        hint = 'หยุดพักอยู่ ค่อยเริ่มกดต่อ';
      } else {
        hint = 'กดให้ผ่านช่วงเป้าหมาย';
      }
    } else if (live && state.pauseActive) {
      hint = 'หยุดพักอยู่ ค่อยเริ่มกดต่อ';
    } else if (live && state.lastCompression) {
      hint = 'พร้อมสำหรับครั้งต่อไป';
    }
    setChip(els.depthChip, chipText, chipTone);

    if (els.gaugeFill) {
      els.gaugeFill.style.setProperty('--state', color);
      els.gaugeFill.style.height = (clamp(value / info.scaleMax, 0, 1) * 100) + '%';
    }
    if (els.gaugePeak) {
      if (has(state.lastPeak)) {
        els.gaugePeak.style.opacity = '1';
        els.gaugePeak.style.bottom = (clamp(state.lastPeak / info.scaleMax, 0, 1) * 100) + '%';
      } else {
        els.gaugePeak.style.opacity = '0';
      }
    }
    setText(els.lastPeakText, has(state.lastPeak)
      ? depthText(state.lastPeak) + ' ' + info.unit : '—');
    setText(els.depthHint, hint);
  }

  function updateRate() {
    const info = null; // rate is always per minute
    void info;
    const bpm = state.frame && has(state.frame.bpm) ? state.frame.bpm : 0;
    const shown = clamp(state.display.rate, 60, 160);
    const fraction = clamp((shown - 60) / 100, 0, 1);
    if (els.rateArc) {
      els.rateArc.style.opacity = bpm > 0 ? '1' : '0';
      els.rateArc.style.strokeDasharray = (fraction * 100).toFixed(2) + ' 100';
      const rateColor = state.rateLevel === 1 ? 'var(--warn)'
        : state.rateLevel === 3 ? 'var(--bad)' : 'var(--accent)';
      els.rateArc.style.stroke = rateColor;
    }
    if (els.rateNeedle) {
      const angle = -90 + fraction * 180;
      els.rateNeedle.setAttribute('transform', 'rotate(' + angle.toFixed(2) + ' 110 100)');
    }
    setText(els.rateValue, bpm > 0 ? String(Math.round(bpm)) : '—');

    if (!state.frame) {
      setChip(els.rateChip, 'รอจังหวะ', 'chip-idle');
    } else if (state.rateLevel === 1) {
      setChip(els.rateChip, 'ช้าไป', 'chip-warn');
    } else if (state.rateLevel === 2) {
      setChip(els.rateChip, 'พอดี', 'chip-ok');
    } else if (state.rateLevel === 3) {
      setChip(els.rateChip, 'เร็วไป', 'chip-bad');
    } else {
      setChip(els.rateChip, 'รอจังหวะ', 'chip-idle');
    }
  }

  function renderRecoilDots() {
    if (!els.recoilDots) return;
    const last = state.compressions.slice(-10);
    const key = last.map((c) => c.i + ':' + (c.rc ? 1 : 0)).join('|') + '#' + state.compressions.length;
    if (key === state.dotsKey) return;
    state.dotsKey = key;
    let html = '';
    for (let i = last.length; i < 10; i += 1) html += '<span class="dot-item"></span>';
    last.forEach((c) => {
      const cls = c.rc ? 'ok' : 'warn';
      const title = 'ครั้งที่ ' + c.i + ': ' + (c.rc ? 'คืนตัวสุด' : 'ยังค้างน้ำหนัก');
      html += '<span class="dot-item ' + cls + '" title="' + esc(title) + '"></span>';
    });
    els.recoilDots.innerHTML = html;
  }

  function updateStatusCards() {
    const last = state.lastCompression;
    if (last) {
      setText(els.recoilText, last.rc ? 'คืนตัวสุด' : 'ยังค้างน้ำหนัก');
      setStatusIcon(els.recoilIcon, last.rc ? 'ok' : 'warn');
    } else {
      setText(els.recoilText, 'ยังไม่มีข้อมูล');
      setStatusIcon(els.recoilIcon, 'idle');
    }

    if (state.frame) {
      setText(els.handText, state.frame.hand ? 'วางมือถูกตำแหน่ง' : 'วางมือผิดตำแหน่ง');
      setStatusIcon(els.handIcon, state.frame.hand ? 'ok' : 'bad');
    } else {
      setText(els.handText, 'ยังไม่ตรวจ');
      setStatusIcon(els.handIcon, 'idle');
    }
    renderRecoilDots();
  }

  function updateCounter() {
    els.counterBlocks.forEach((block) => {
      block.hidden = block.dataset.counter !== state.mode;
    });

    const frame = state.frame;
    if (state.mode === 'free') {
      setText(els.counterValue, String(state.count));
      if (frame && state.sessionActive) setChip(els.counterChip, 'กำลังฝึก', 'chip-ok');
      else if (frame) setChip(els.counterChip, 'พร้อม', 'chip-idle');
      else setChip(els.counterChip, 'พร้อม', 'chip-idle');
    } else if (state.mode === 'cycle') {
      const cc = clamp(state.cc, 0, 30);
      const fraction = cc / 30;
      setText(els.ringCount, String(cc));
      if (els.counterRing) {
        els.counterRing.style.opacity = cc > 0 ? '1' : '0';
        els.counterRing.style.strokeDasharray = (fraction * 100).toFixed(2) + ' 100';
        els.counterRing.style.stroke = cc >= 30 ? 'var(--ok)' : 'var(--accent)';
      }
      setText(els.cycleText, 'รอบที่ ' + (state.cy + 1));
      setText(els.cycleTotal, 'ทั้งหมด ' + state.count + ' ครั้ง');
      if (state.cc >= 30) setChip(els.counterChip, 'พักหายใจ', 'chip-warn');
      else if (frame && state.sessionActive) setChip(els.counterChip, 'กำลังฝึก', 'chip-ok');
      else setChip(els.counterChip, 'พร้อม', 'chip-idle');

      if (state.sessionActive && state.cc >= 30) {
        els.breathBanner.hidden = false;
        const over = state.cc > 30;
        els.breathBanner.classList.toggle('is-over', over);
        setText(els.breathText, over ? 'เกิน 30 ครั้ง' : 'ให้ลมหายใจ 2 ครั้ง');
      } else {
        els.breathBanner.hidden = true;
      }
    } else {
      const remaining = clamp(120 - state.elapsed, 0, 120);
      setText(els.testCountdown, fmtDuration(remaining));
      setText(els.testCount, 'กดแล้ว ' + state.count + ' ครั้ง');
      if (frame && state.sessionActive) setChip(els.counterChip, 'กำลังทดสอบ', 'chip-ok');
      else if (frame) setChip(els.counterChip, 'จบแล้ว', 'chip-idle');
      else setChip(els.counterChip, 'พร้อม', 'chip-idle');
      els.breathBanner.hidden = true;
    }
  }

  function updateNotices() {
    const frame = state.frame;
    if (els.pauseBanner) {
      if (state.pauseActive && frame) {
        const start = has(state.pauseStart) ? state.pauseStart : frame.t - HANDS_OFF_MS;
        const elapsed = Math.max(0, frame.t - start);
        const tooLong = elapsed >= 10000;
        els.pauseBanner.hidden = false;
        els.pauseBanner.classList.toggle('is-long', tooLong);
        setText(els.pauseText, tooLong
          ? 'หยุดนานเกิน 10 วินาที'
          : 'หยุดกดแล้ว ' + Math.floor(elapsed / 1000) + ' วินาที');
      } else {
        els.pauseBanner.hidden = true;
      }
    }
    if (els.warnSps) els.warnSps.hidden = !state.lowSps;
    if (els.warnOverload) {
      if (!state.overload && frame && has(frame.t) && frame.t > state.overloadUntil) {
        state.overload = false;
      }
      els.warnOverload.hidden = !state.overload;
    }
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
    const pad = { l: 40, r: 12, t: 8, b: 20 };
    const width = Math.max(10, rect.width - pad.l - pad.r);
    const height = Math.max(10, rect.height - pad.t - pad.b);
    const nowT = history[history.length - 1].t;
    const startT = nowT - 10000;
    const xOf = (t) => pad.l + clamp((t - startT) / 10000, 0, 1) * width;
    const yOf = (v) => pad.t + clamp(v / max, 0, 1) * height;

    const bandTop = yOf(info.good);
    const bandBottom = yOf(info.deep);
    ctx.fillStyle = c.okBg;
    ctx.fillRect(pad.l, bandTop, width, Math.max(1, bandBottom - bandTop));
    ctx.strokeStyle = c.okBorder;
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(pad.l, bandTop);
    ctx.lineTo(pad.l + width, bandTop);
    ctx.moveTo(pad.l, bandBottom);
    ctx.lineTo(pad.l + width, bandBottom);
    ctx.stroke();

    ctx.font = '10px "Anuphan", "Noto Sans Thai", sans-serif';
    ctx.textBaseline = 'middle';
    ctx.textAlign = 'right';
    for (let i = 0; i <= 4; i += 1) {
      const y = pad.t + height - (height * i / 4);
      ctx.strokeStyle = c.border;
      ctx.beginPath();
      ctx.moveTo(pad.l, y);
      ctx.lineTo(pad.l + width, y);
      ctx.stroke();
      ctx.fillStyle = c.text2;
      const label = tickNum(max * i / 4);
      ctx.fillText(label, pad.l - 7, y);
    }
    ctx.textAlign = 'left';
    ctx.fillStyle = c.text2;
    ctx.fillText(info.kg ? 'กก.' : 'ซม.', 4, pad.t + 4);

    ctx.strokeStyle = c.border;
    ctx.lineWidth = 1;
    for (let i = 0; i <= 2; i += 1) {
      const x = pad.l + width * i / 2;
      ctx.beginPath();
      ctx.moveTo(x, pad.t);
      ctx.lineTo(x, pad.t + height);
      ctx.stroke();
    }
    ctx.textAlign = 'left';
    ctx.textBaseline = 'top';
    ctx.fillStyle = c.text2;
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
    ctx.globalAlpha = .13;
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
    ctx.lineWidth = 2.2;
    ctx.lineJoin = 'round';
    ctx.lineCap = 'round';
    ctx.stroke();

    state.compressions.forEach((comp) => {
      if (!has(comp.t) || comp.t < startT - 500 || comp.t > nowT + 500) return;
      const level = comp.dl || 0;
      const fill = level === 1 ? c.warn : level === 3 ? c.bad : level === 2 ? c.ok : c.accent;
      const x = xOf(comp.t);
      const y = yOf(toDisplay(comp.pk));
      ctx.beginPath();
      ctx.arc(x, y, 3.6, 0, Math.PI * 2);
      ctx.fillStyle = fill;
      ctx.fill();
      ctx.strokeStyle = c.text;
      ctx.globalAlpha = .55;
      ctx.lineWidth = 1.2;
      ctx.stroke();
      ctx.globalAlpha = 1;
    });

    if (last) {
      ctx.beginPath();
      ctx.arc(xOf(last.t), yOf(last.v), 2.8, 0, Math.PI * 2);
      ctx.fillStyle = c.accent;
      ctx.fill();
    }
  }

  function updateSourceAndInfo() {
    refreshInfoPanels();
  }

  let lastTick = 0;
  function tick(now) {
    const dt = lastTick ? Math.min(80, now - lastTick) : 16;
    lastTick = now;
    const reduce = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
    const k = reduce ? 1 : 1 - Math.pow(1 - .25, dt / 16.667);
    state.display.depth += (state.targetDepth - state.display.depth) * k;
    state.display.rate += (state.targetRate - state.display.rate) * k;
    if (Math.abs(state.targetDepth - state.display.depth) < .005) state.display.depth = state.targetDepth;
    if (Math.abs(state.targetRate - state.display.rate) < .05) state.display.rate = state.targetRate;

    updateHero();
    updateRate();
    updateStatusCards();
    updateCounter();
    updateNotices();
    updateSourceAndInfo();
    if (state.graphDirty) {
      drawGraph();
      state.graphDirty = false;
    }
    requestAnimationFrame(tick);
  }

  function showToast(text, tone) {
    if (!els.toast) return;
    setText(els.toast, text);
    els.toast.dataset.tone = tone || '';
    els.toast.hidden = false;
    clearTimeout(state.toastTimer);
    state.toastTimer = setTimeout(() => {
      els.toast.hidden = true;
    }, tone === 'bad' ? 4800 : 3000);
  }

  function openSheet(name) {
    const sheet = els.sheetMap[name];
    if (!sheet) return;
    state.lastFocus = document.activeElement;
    Object.keys(els.sheetMap).forEach((key) => {
      els.sheetMap[key].hidden = key !== name;
    });
    els.modalLayer.hidden = false;
    state.sheet = name;
    document.body.style.overflow = 'hidden';
    setTimeout(() => {
      try { sheet.focus({ preventScroll: true }); } catch (err) { sheet.focus(); }
    }, 30);
  }

  function closeSheet() {
    if (!state.sheet) return;
    els.modalLayer.hidden = true;
    Object.keys(els.sheetMap).forEach((key) => {
      els.sheetMap[key].hidden = true;
    });
    state.sheet = null;
    document.body.style.overflow = '';
    const focus = state.lastFocus;
    state.lastFocus = null;
    if (focus && typeof focus.focus === 'function') {
      try { focus.focus({ preventScroll: true }); } catch (err) { focus.focus(); }
    }
    state.graphDirty = true;
  }

  function openConnect() {
    if (!('serial' in navigator)) {
      if (els.connectSerial) {
        els.connectSerial.disabled = true;
        els.connectSerial.setAttribute('aria-disabled', 'true');
      }
      setText(els.serialHint, 'เบราว์เซอร์นี้ไม่รองรับ Web Serial');
    } else if (els.connectSerial) {
      els.connectSerial.disabled = false;
      els.connectSerial.removeAttribute('aria-disabled');
      setText(els.serialHint, 'ต่อด้วยสาย USB ความเร็ว 115200');
    }
    if (state.opts.demo && els.demoSelect) {
      try { els.demoSelect.value = state.opts.demo; } catch (err) { /* no such option */ }
    }
    openSheet('connect');
  }

  function openSettings() {
    refreshInfoPanels();
    setMetronome(state.metronome, false);
    openSheet('settings');
  }

  const MODE_BY_NUM = ['free', 'cycle', 'test'];

  function modeLabel(value) {
    const key = typeof value === 'number' ? MODE_BY_NUM[value] : value;
    return MODE_TEXT[key] || MODE_TEXT.free;
  }

  function openSummary() {
    if (!state.lastSum) {
      showToast('ยังไม่มีผลสรุปให้แสดง', 'warn');
      return;
    }
    populateSummary();
    if (state.sheet !== 'summary') openSheet('summary');
    state.graphDirty = true;
  }

  function populateSummary() {
    const sum = state.lastSum;
    if (!sum) return;
    const info = unitInfo();
    const score = clamp(Number(sum.score) || 0, 0, 100);
    setText(els.scoreValue, String(score));

    let grade = 'ฝึกอีกนิด';
    let tone = 'bad';
    let ring = 'var(--bad)';
    if (score >= 85) {
      grade = 'ยอดเยี่ยม'; tone = 'ok'; ring = 'var(--ok)';
    } else if (score >= 70) {
      grade = 'ดี'; tone = ''; ring = 'var(--accent)';
    } else if (score >= 50) {
      grade = 'พอใช้'; tone = 'warn'; ring = 'var(--warn)';
    }
    setText(els.gradeWord, grade);
    if (tone) els.gradeWord.dataset.tone = tone;
    else els.gradeWord.removeAttribute('data-tone');

    let date = '';
    try {
      date = new Intl.DateTimeFormat('th-TH', {
        day: 'numeric', month: 'short', year: 'numeric'
      }).format(new Date());
    } catch (err) {
      date = new Date().toLocaleDateString();
    }
    const profile = PROFILE_TEXT[sum.prof === 1 ? 'child' : 'adult'];
    setText(els.summaryMeta, date + ' · ' + modeLabel(sum.mode) + ' · ' + profile);

    const n = Number(sum.n) || 0;
    const rated = Number(sum.rr) || 0;
    const depthPct = pct(sum.dg, n);
    const ratePct = pct(sum.rg, rated);
    const recoilPct = pct(sum.rc, n);
    const handPct = pct(sum.hd, n);

    const metrics = [
      [els.mDepth, els.mDepthBar, depthPct],
      [els.mRate, els.mRateBar, ratePct],
      [els.mRecoil, els.mRecoilBar, recoilPct],
      [els.mHand, els.mHandBar, handPct]
    ];
    metrics.forEach((entry) => {
      setText(entry[0], entry[2] + '%');
      if (entry[1]) entry[1].style.width = '0%';
    });

    if (els.scoreRing) {
      els.scoreRing.style.strokeDasharray = '0 100';
      els.scoreRing.style.stroke = ring;
    }

    const facts = [
      ['CCF', (Number(sum.ccf) || 0) + '%'],
      ['อัตราเฉลี่ย', (Number(sum.bpm) || 0) + ' ครั้ง/นาที'],
      ['ความลึกเฉลี่ย', depthText(toDisplay(sum.pk)) + ' ' + info.unit],
      ['จำนวนครั้ง', n + ' ครั้ง'],
      ['ระยะเวลา', fmtDuration(sum.dur)],
      ['หยุดนานสุด', ((Number(sum.lp) || 0) / 1000).toFixed(1) + ' วิ'],
      ['รอบ 30:2', (Number(sum.cy) || 0) + ' รอบ'],
      ['ครั้งที่ถูกทั้งหมด', (Number(sum.ok) || 0) + ' ครั้ง']
    ];
    let factsHtml = '';
    facts.forEach((fact) => {
      factsHtml += '<div class="fact"><span>' + esc(fact[0]) + '</span><strong>' +
        esc(fact[1]) + '</strong></div>';
    });
    if (els.summaryFacts) els.summaryFacts.innerHTML = factsHtml;

    requestAnimationFrame(() => {
      if (els.scoreRing) els.scoreRing.style.strokeDasharray = score + ' 100';
      metrics.forEach((entry) => {
        if (entry[1]) entry[1].style.width = clamp(entry[2], 0, 100) + '%';
      });
    });
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

  function renderCalib() {
    const cal = state.cal;
    if (!cal) return;

    if (els.calibSub) {
      setText(els.calibSub, cal.saved
        ? 'บันทึกค่าปรับเทียบเรียบร้อย'
        : 'ขั้นที่ ' + cal.step + ' จาก 4 · ทำตามลำดับ');
    }

    $$('.step', els.calibStepper).forEach((step) => {
      const n = Number(step.dataset.step);
      step.classList.toggle('active', !cal.saved && n === cal.step);
      step.classList.toggle('done', cal.saved || n < cal.step);
    });

    let panel = '';
    if (cal.saved) {
      panel = '<div class="calib-success">' +
        '<span class="status-icon is-ok"><svg class="icon" aria-hidden="true"><use href="#i-check"></use></svg></span>' +
        '<h3>บันทึกเรียบร้อย</h3>' +
        '<p>หุ่นจะใช้ค่าปรับเทียบนี้ในรอบถัดไป</p>' +
        '</div>';
      calActions('<button type="button" class="btn btn-primary" data-cal-action="close">เสร็จสิ้น</button>');
    } else if (cal.step === 1) {
      panel = '<h3>ยกมือออกจากหุ่น</h3>' +
        '<p>วางหุ่นบนพื้นเรียบ แล้วยกมือออกจากแผ่นกดเพื่อให้เซนเซอร์ตั้งศูนย์</p>' +
        '<button type="button" class="btn btn-primary" data-cal-action="tare">ตั้งศูนย์</button>' +
        '<p class="calib-hint">ใช้เวลาประมาณ 1 วินาที</p>';
      calActions('');
    } else if (cal.step === 2) {
      panel = '<h3>วางของที่รู้น้ำหนัก</h3>' +
        '<p>วางของที่ทราบน้ำหนักบนแผ่นกด เช่น ดัมเบลหรือขวดน้ำที่ชั่งไว้แล้ว</p>' +
        '<label class="field"><span>น้ำหนัก (กก.)</span>' +
        '<input id="calWeightInput" type="number" min="0.2" max="20" step="0.1" inputmode="decimal" value="' +
        (has(cal.weight) ? cal.weight.toFixed(1) : '2.0') + '"></label>' +
        '<button type="button" class="btn btn-primary" data-cal-action="weight">บันทึกน้ำหนัก</button>' +
        '<p class="calib-hint">ใช้ค่าตั้งแต่ 0.2 ถึง 20 กก.</p>';
      calActions('<button type="button" class="btn btn-ghost" data-cal-action="back">ย้อนกลับ</button>');
    } else if (cal.step === 3 && cal.tof) {
      panel = '<h3>กดช้าๆ หลายครั้ง</h3>' +
        '<p>กดให้ลึกสุดประมาณ 6 ซม. ทำช้าๆ 5–10 ครั้ง ระบบจะเก็บคู่แรงกับความลึกไว้</p>' +
        '<div class="cal-counter"><span id="calPairCount">' + cal.presses + '</span>' +
        '<small>ครั้งที่กด</small></div>' +
        '<button type="button" class="btn btn-primary" data-cal-action="finish">เสร็จสิ้น</button>' +
        '<p class="calib-hint">' + (cal.active ? 'ระบบจะนับการกดให้อัตโนมัติ' : 'กำลังเริ่มบันทึก…') + '</p>';
      calActions('<button type="button" class="btn btn-ghost" data-cal-action="back">ย้อนกลับ</button>');
    } else if (cal.step === 3) {
      const ready = has(cal.goodKg) && has(cal.deepKg) && cal.deepKg > cal.goodKg;
      panel = '<h3>ตั้งช่วงความลึกด้วยน้ำหนัก</h3>' +
        '<p>กดให้ได้ 5 ซม. (วัดด้วยไม้บรรทัด) แล้วกดปุ่มแรก จากนั้นทำซ้ำที่ 6 ซม.</p>' +
        '<div class="band-capture">' +
        '<button type="button" class="btn btn-ghost" data-cal-action="peak-good">บันทึกจุด 5 ซม.<span>' +
        (has(cal.goodKg) ? cal.goodKg.toFixed(1) + ' กก.' : '—') + '</span></button>' +
        '<button type="button" class="btn btn-ghost" data-cal-action="peak-deep">บันทึกจุด 6 ซม.<span>' +
        (has(cal.deepKg) ? cal.deepKg.toFixed(1) + ' กก.' : '—') + '</span></button>' +
        '</div>' +
        '<button type="button" class="btn btn-primary" data-cal-action="bands"' +
        (ready ? '' : ' disabled') + '>ใช้ช่วงนี้</button>' +
        '<p class="calib-hint">ต้องได้ค่าจุดลึกมากกว่าจุดตื้น</p>';
      calActions('<button type="button" class="btn btn-ghost" data-cal-action="back">ย้อนกลับ</button>');
    } else {
      panel = '<h3>บันทึกค่าลงเครื่อง</h3>' +
        '<p>ค่าปรับเทียบจะถูกเก็บไว้ในหน่วยความจำของหุ่น และใช้ในรอบถัดไป</p>' +
        '<button type="button" class="btn btn-primary" data-cal-action="save">บันทึก</button>' +
        '<p class="calib-hint">เขียนค่าลงหน่วยความจำของบอร์ด</p>';
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
      calStatus(msg.msg || 'คำสั่งไม่สำเร็จ', 'bad');
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
        calStatus('ตั้งศูนย์เรียบร้อย', 'ok');
        renderCalib();
      });
    } else if (action === 'weight') {
      const input = document.getElementById('calWeightInput');
      const kg = input ? parseFloat(input.value) : NaN;
      if (!has(kg) || kg < .2 || kg > 20) {
        calStatus('กรอกน้ำหนักระหว่าง 0.2 ถึง 20 กก.', 'bad');
        return;
      }
      calSend('cal weight ' + kg.toFixed(1), 'cal weight', () => {
        state.cal.weight = kg;
        state.cal.step = 3;
        calStatus('บันทึกน้ำหนักแล้ว', 'ok');
        renderCalib();
        if (state.cal.tof) beginCalRecording();
      });
    } else if (action === 'finish') {
      calSend('cal finish', 'cal finish', () => {
        state.cal.active = false;
        state.cal.step = 4;
        calStatus('ประมวลผลช่วงความลึกแล้ว', 'ok');
        renderCalib();
      });
    } else if (action === 'peak-good' || action === 'peak-deep') {
      calSend('cal peak', 'cal peak', (ack) => {
        const kg = parseFloat(ack.msg);
        if (!has(kg)) {
          calStatus('อ่านค่าน้ำหนักไม่ได้', 'bad');
          return;
        }
        if (action === 'peak-good') {
          state.cal.goodKg = kg;
          calStatus('บันทึกจุด 5 ซม. ที่ ' + kg.toFixed(1) + ' กก.', 'ok');
        } else {
          state.cal.deepKg = kg;
          calStatus('บันทึกจุด 6 ซม. ที่ ' + kg.toFixed(1) + ' กก.', 'ok');
        }
        renderCalib();
      });
    } else if (action === 'bands') {
      if (!has(cal.goodKg) || !has(cal.deepKg) || cal.deepKg <= cal.goodKg) {
        calStatus('ต้องได้ค่าจุดลึกมากกว่าจุดตื้น', 'bad');
        return;
      }
      calSend('cal bands ' + cal.goodKg.toFixed(1) + ' ' + cal.deepKg.toFixed(1),
        'cal bands', () => {
          calStatus('ตั้งช่วงความลึกแล้ว', 'ok');
          state.cal.step = 4;
          renderCalib();
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
        state.cal.step = 2;
      } else if (state.cal.step === 4) {
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
    if (els.settingsBtn) els.settingsBtn.addEventListener('click', openSettings);
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

    if (els.startBtn) els.startBtn.addEventListener('click', startSession);
    if (els.stopBtn) els.stopBtn.addEventListener('click', stopSession);
    if (els.metroBtn) els.metroBtn.addEventListener('click', () => setMetronome(!state.metronome, true));
    if (els.metroSwitch) {
      els.metroSwitch.addEventListener('click', () => setMetronome(!state.metronome, true));
    }

    if (els.modalLayer) {
      els.modalLayer.addEventListener('click', (event) => {
        if (event.target.closest('[data-close]')) closeSheet();
      });
    }

    document.addEventListener('keydown', (event) => {
      if (event.key === 'Escape' && state.sheet) {
        closeSheet();
        return;
      }
      if (event.key === 'Tab') trapFocus(event);
    });

    if (els.connectWs) {
      els.connectWs.addEventListener('click', () => attachTransport(new WsTransport()));
    }
    if (els.connectSerial) {
      els.connectSerial.addEventListener('click', () => {
        if (els.connectSerial.disabled) return;
        attachTransport(new SerialTransport());
      });
    }
    if (els.connectDemo) {
      els.connectDemo.addEventListener('click', () => {
        const id = els.demoSelect && els.demoSelect.value ? els.demoSelect.value : 'good';
        attachTransport(new DemoSocket({
          id,
          speed: state.opts.speed || 1,
          seek: state.opts.seek || 0
        }));
      });
    }

    if (els.settingsConnectBtn) els.settingsConnectBtn.addEventListener('click', openConnect);
    if (els.settingsDisconnectBtn) {
      els.settingsDisconnectBtn.addEventListener('click', () => {
        if (state.transport) {
          try { state.transport.disconnect(); } catch (err) { /* ignore */ }
          state.transport = null;
        }
        setStatus('off');
      });
    }
    if (els.openCalibBtn) els.openCalibBtn.addEventListener('click', openCalib);
    if (els.csvBtn) els.csvBtn.addEventListener('click', downloadCsv);
    if (els.againBtn) {
      els.againBtn.addEventListener('click', () => {
        closeSheet();
        startSession();
      });
    }

    if (els.sheetCalib) {
      els.sheetCalib.addEventListener('click', (event) => {
        const button = event.target.closest('[data-cal-action]');
        if (button) handleCalAction(button.dataset.calAction);
      });
    }

    window.addEventListener('resize', () => { state.graphDirty = true; });
    if (els.graphWrap && 'ResizeObserver' in window) {
      const observer = new ResizeObserver(() => { state.graphDirty = true; });
      observer.observe(els.graphWrap);
    }
  }

  function init() {
    cacheEls();
    state.theme = document.documentElement.dataset.theme === 'light' ? 'light' : 'dark';
    state.opts = parseUrl();
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

    requestAnimationFrame(tick);

    if (state.opts.ws) {
      attachTransport(new WsTransport());
    } else if (state.opts.demo) {
      attachTransport(new DemoSocket({
        id: state.opts.demo,
        speed: state.opts.speed,
        seek: state.opts.seek
      }));
    } else if (location.hostname === '192.168.4.1') {
      attachTransport(new WsTransport());
    } else if ((state.opts.sheet === 'summary' || state.opts.sheet === 'calib') && isLocalPreview()) {
      attachTransport(new DemoSocket({
        id: 'good',
        speed: state.opts.speed,
        seek: state.opts.seek
      }));
    } else {
      setTimeout(() => {
        if (state.status === 'off') openConnect();
      }, 80);
    }

    if (state.opts.sheet === 'connect') {
      setTimeout(() => {
        if (state.status === 'off') openConnect();
      }, 120);
    }
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }
})();
