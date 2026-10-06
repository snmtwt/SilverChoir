/* Native H5UI Canvas 2D and ordinary browsers share this component. */
(() => {
  'use strict';
  const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
  const finite = (v, fallback) => Number.isFinite(Number(v)) ? Number(v) : fallback;
  const mod = (v, n) => ((v % n) + n) % n;
  // Mix all int32 bits so adjacent native seeds do not produce adjacent phases.
  function mix32(value) {
    let x = value >>> 0;
    x = Math.imul(x ^ (x >>> 16), 0x7feb352d);
    x = Math.imul(x ^ (x >>> 15), 0x846ca68b);
    return (x ^ (x >>> 16)) >>> 0;
  }
  let autoSeedSequence = (Date.now() ^ Math.floor(Math.random() * 0x100000000)) >>> 0;
  function nextAutoSeed() {
    let seed;
    do {
      autoSeedSequence = (autoSeedSequence + 0x9e3779b9) >>> 0;
      seed = mix32(autoSeedSequence) | 0;
    } while (!seed);
    return seed;
  }
  // Stylized P-QRS-T morphology. BPM changes phase speed; intensity scales displacement only.
  function waveform(phase) {
    const p = mod(phase, 1), bell = (c, w) => Math.exp(-Math.pow((p - c) / w, 2));
    return .10 * bell(.17, .035) - .12 * bell(.305, .014) + 1.0 * bell(.345, .013)
      - .27 * bell(.387, .020) + .20 * bell(.60, .072);
  }
  class ECGMonitor {
    constructor(canvas, parameters = {}) {
      this.canvas = canvas; this.ctx = canvas.getContext('2d');
      this.parameters = {color: '#29dcf2', opacity: 1, bpm: 72, intensity: 1, sweepSeconds: 3.2, glow: 1, grid: true, background: true, seed: 0};
      this.autoSeed = nextAutoSeed();
      this.elapsed = 0; this.phase = 0; this.lastTime = null; this.frameId = 0;
      this.samples = new Float32Array(1); this.head = 0; this.w = 0; this.h = 0;
      this.setParameters(parameters && typeof parameters === 'object' ? parameters : {}); this.resize();
      window.addEventListener('resize', () => this.resize());
    }
    setParameters(p) {
      if (!p || typeof p !== 'object') return;
      const s = this.parameters;
      if (typeof p.color === 'string' && /^#[0-9a-f]{6}$/i.test(p.color)) s.color = p.color;
      if ('bpm' in p) s.bpm = clamp(finite(p.bpm, 72), 0, 300);
      if ('opacity' in p) s.opacity = clamp(finite(p.opacity, 1), 0, 1);
      if ('intensity' in p) s.intensity = clamp(finite(p.intensity, 0), 0, 2);
      if ('sweepSeconds' in p) s.sweepSeconds = clamp(finite(p.sweepSeconds, 3.2), 1, 10);
      if ('glow' in p) s.glow = clamp(finite(p.glow, 1), 0, 2);
      if ('grid' in p) s.grid = !!p.grid;
      if ('background' in p) s.background = !!p.background;
      // Missing seed preserves the current identity; explicit zero uses a stable
      // per-instance fallback for ordinary browsers and legacy native callers.
      const seed = ('seed' in p ? finite(p.seed, 0) | 0 : s.seed) || this.autoSeed;
      if (seed !== s.seed) {
        s.seed = seed;
        this.phase = mix32(seed ^ 0x9e3779b9) / 0x100000000;
        this.elapsed = mix32(seed ^ 0x243f6a88) / 0x100000000 * s.sweepSeconds;
        this.prefillHistory();
      }
      // Death clears old peaks immediately instead of displaying stale beats for another sweep.
      if (!s.intensity || !s.bpm) this.samples.fill(0);
      this.rgb = [1, 3, 5].map(i => parseInt(s.color.slice(i, i + 2), 16));
    }
    color(alpha) { return `rgba(${this.rgb[0]},${this.rgb[1]},${this.rgb[2]},${clamp(alpha * this.parameters.opacity, 0, 1).toFixed(4)})`; }
    resize() {
      const w = clamp(Math.round(this.canvas.clientWidth || 320), 16, 1024);
      const h = clamp(Math.round(this.canvas.clientHeight || 96), 16, 384);
      const dpr = Math.min(2, Math.max(1, globalThis.devicePixelRatio || 1), Math.sqrt(1048576 / (w * h)));
      if (this.w === w && this.h === h && this.dpr === dpr) return;
      this.w = w; this.h = h; this.dpr = dpr;
      this.canvas.height = 0;
      this.canvas.width = Math.floor(w * dpr); this.canvas.height = Math.floor(h * dpr);
      this.ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      this.samples = new Float32Array(w);
      this.prefillHistory();
    }
    prefillHistory() {
      if (!this.w) return;
      const s = this.parameters, w = this.w;
      const position = mod(this.elapsed / s.sweepSeconds, 1) * w;
      this.head = Math.floor(position);
      // Pre-fill a coherent trace so the Designer never starts with an empty monitor.
      // Use the fractional scan position to match samples written by advance().
      for (let x = 0; x < w; x++) {
        const age = mod(position - x, w) / w * s.sweepSeconds;
        this.samples[x] = s.bpm && s.intensity ? waveform(this.phase - age * s.bpm / 60) * s.intensity : 0;
      }
    }
    advance(dt) {
      dt = clamp(finite(dt, 0), 0, .25);
      const s = this.parameters, oldElapsed = this.elapsed, oldPhase = this.phase;
      this.elapsed += dt; this.phase = mod(this.phase + dt * s.bpm / 60, 1);
      const start = oldElapsed / s.sweepSeconds * this.w, end = this.elapsed / s.sweepSeconds * this.w;
      for (let step = Math.floor(start) + 1; step <= Math.floor(end); step++) {
        const phase = oldPhase + (step - start) / this.w * s.sweepSeconds * s.bpm / 60;
        this.samples[mod(step, this.w)] = s.bpm ? waveform(phase) * s.intensity : 0;
      }
      this.head = Math.floor(mod(end, this.w));
      if (!s.intensity || !s.bpm) this.samples.fill(0);
    }
    draw() {
      const c = this.ctx, w = this.w, h = this.h, s = this.parameters, head = this.head;
      c.clearRect(0, 0, w, h);
      if (s.background) {
        const bg = c.createLinearGradient(0, 0, 0, h);
        bg.addColorStop(0, 'rgba(4,15,23,0.96)'); bg.addColorStop(1, 'rgba(2,8,15,0.92)');
        c.fillStyle = bg; c.fillRect(0, 0, w, h);
      }
      if (s.grid) {
        c.beginPath(); const cell = h >= 70 ? 16 : 12;
        for (let x = .5; x < w; x += cell) { c.moveTo(x, 0); c.lineTo(x, h); }
        for (let y = .5; y < h; y += cell) { c.moveTo(0, y); c.lineTo(w, y); }
        c.lineWidth = .5; c.strokeStyle = this.color(.10); c.stroke();
        c.beginPath(); c.moveTo(0, h * .57); c.lineTo(w, h * .57);
        c.strokeStyle = this.color(.13); c.stroke();
      }
      const beam = c.createLinearGradient(head - 20, 0, head + 1, 0);
      beam.addColorStop(0, this.color(0)); beam.addColorStop(1, this.color(.11));
      c.fillStyle = beam; c.fillRect(Math.max(0, head - 20), 1, Math.min(20, head), h - 2);
      const yAt = x => clamp(h * .57 - this.samples[x] * h * .40, 3, h - 3);
      const opacity = x => .065 + .935 * Math.pow(1 - mod(head - x, w) / w, 2.0);
      const gradient = c.createLinearGradient(0, 0, w, 0);
      gradient.addColorStop(0, this.color(opacity(0)));
      gradient.addColorStop(head / w, this.color(1));
      gradient.addColorStop(Math.min(1, (head + 1) / w), this.color(.065));
      gradient.addColorStop(1, this.color(opacity(w - 1)));
      c.beginPath(); let open = false;
      const gap = Math.max(4, Math.min(12, w * .035));
      for (let x = 0; x < w; x++) {
        if (mod(x - head, w) > 0 && mod(x - head, w) < gap) { open = false; continue; }
        if (!open) { c.moveTo(x, yAt(x)); open = true; } else c.lineTo(x, yAt(x));
      }
      c.lineCap = 'round'; c.strokeStyle = gradient;
      if (s.glow > 0) {
        c.globalAlpha = .07 * s.glow; c.lineWidth = 7; c.stroke();
        c.globalAlpha = .18 * s.glow; c.lineWidth = 3.6; c.stroke();
      }
      c.globalAlpha = 1; c.lineWidth = 1.35; c.stroke();
      c.beginPath(); c.arc(head, yAt(head), 1.0, 0, Math.PI * 2); c.lineWidth = 1.6;
      c.globalAlpha = s.opacity; c.strokeStyle = 'rgba(220,255,255,0.98)'; c.stroke(); c.globalAlpha = 1;
      // Small corner registration marks keep the component usable at member-card sizes.
      c.beginPath(); for (const [x,y,dx,dy] of [[1,1,1,1],[w-1,1,-1,1],[1,h-1,1,-1],[w-1,h-1,-1,-1]]) {
        c.moveTo(x+dx*6,y); c.lineTo(x,y); c.lineTo(x,y+dy*4);
      }
      c.lineWidth = .7; c.strokeStyle = this.color(.26); c.stroke();
    }
    start() {
      if (this.frameId) return;
      const frame = time => {
        this.frameId = 0;
        if (this.lastTime !== null) this.advance((time - this.lastTime) / 1000);
        this.lastTime = time; this.draw(); this.frameId = requestAnimationFrame(frame);
      };
      this.draw(); this.frameId = requestAnimationFrame(frame);
    }
    stop() { if (this.frameId) cancelAnimationFrame(this.frameId); this.frameId = 0; this.lastTime = null; }
  }
  globalThis.SilverChoirECG = {Monitor: ECGMonitor, waveform};
  const canvas = document.getElementById('ecg');
  if (canvas) {
    function decode(value) { try { return typeof value === 'string' ? JSON.parse(value) : value; } catch (_) { return {}; } }
    // Apply spawn parameters before pre-filling history, so weak/fast monitors
    // never briefly show the default healthy waveform when first displayed.
    const monitor = new ECGMonitor(canvas, globalThis.ue ? decode(ue.getData('ecgParameters')) : {});
    globalThis.ecgMonitor = monitor;
    function parameters(value) { monitor.setParameters(decode(value)); }
    window.addEventListener('ECGParameters', e => parameters(e.detail));
    window.addEventListener('unload', () => monitor.stop());
    monitor.start();
  }
})();
