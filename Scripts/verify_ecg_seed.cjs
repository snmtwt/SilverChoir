#!/usr/bin/env node
'use strict';

// Run with: node Scripts/verify_ecg_seed.cjs
// Exercise the shipped page script without UE, a browser, or external packages.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname, '../Content/UI/ECG/ecg.js'), 'utf8');

function canvas(width = 160, height = 48) {
  const finiteCoordinates = (...values) => values.forEach(value => assert.ok(Number.isFinite(value)));
  const ctx = {};
  for (const name of ['setTransform', 'clearRect', 'fillRect', 'beginPath', 'moveTo', 'lineTo', 'stroke', 'arc']) {
    ctx[name] = finiteCoordinates;
  }
  ctx.createLinearGradient = (...values) => {
    finiteCoordinates(...values);
    return {addColorStop(offset) { assert.ok(Number.isFinite(offset) && offset >= 0 && offset <= 1); }};
  };
  return {clientWidth: width, clientHeight: height, getContext: () => ctx};
}

function environment(initialParameters, mount = false) {
  const listeners = new Map(), frames = new Map();
  let nextFrame = 1;
  const fixedMath = Object.create(Math);
  fixedMath.random = () => .25;
  const pageCanvas = mount ? canvas() : null;
  const context = vm.createContext({
    Math: fixedMath, Date: {now: () => 1728000000000}, devicePixelRatio: 1,
    window: {addEventListener(name, callback) {
      if (!listeners.has(name)) listeners.set(name, []);
      listeners.get(name).push(callback);
    }},
    document: {getElementById: () => pageCanvas},
    requestAnimationFrame(callback) { const id = nextFrame++; frames.set(id, callback); return id; },
    cancelAnimationFrame(id) { frames.delete(id); }
  });
  if (initialParameters !== undefined) {
    context.ue = {getData(name) {
      assert.equal(name, 'ecgParameters');
      return typeof initialParameters === 'string' ? initialParameters : JSON.stringify(initialParameters);
    }};
  }
  vm.runInContext(source, context, {filename: 'ecg.js'});
  return {
    context, frames, pageCanvas,
    create: p => new context.SilverChoirECG.Monitor(canvas(), p),
    emit(name, detail) { for (const callback of listeners.get(name) || []) callback({detail}); },
    frame(time) {
      const callbacks = Array.from(frames.values()); frames.clear();
      callbacks.forEach(callback => callback(time));
    }
  };
}

function snapshot(monitor) {
  return {seed: monitor.parameters.seed, phase: monitor.phase, elapsed: monitor.elapsed,
    head: monitor.head, samples: Array.from(monitor.samples)};
}
function near(actual, expected, tolerance = 1e-7) {
  assert.ok(Math.abs(actual - expected) <= tolerance, `${actual} differs from ${expected}`);
}
function flat(monitor) {
  assert.ok(monitor.samples.every(value => value === 0), 'dead monitor must contain no historic peaks');
  monitor.draw();
}
function coherent(monitor, waveform) {
  const s = monitor.parameters, period = s.sweepSeconds;
  for (let x = 0; x < monitor.w; x++) {
    // Last crossing of pixel x in the circular scan, at or before the present.
    const crossing = Math.floor(monitor.elapsed / period - x / monitor.w) * period + x / monitor.w * period;
    const age = monitor.elapsed - crossing;
    const expected = s.bpm && s.intensity ? waveform(monitor.phase - age * s.bpm / 60) * s.intensity : 0;
    near(monitor.samples[x], expected);
  }
}
let passed = 0;
function test(name, callback) { callback(); passed++; console.log(`PASS ${name}`); }

test('explicit seed reproduces initial and advancing waveforms', () => {
  const a = environment().create({seed: -2147483648, bpm: 103, intensity: .63});
  const b = environment().create({seed: -2147483648, bpm: 103, intensity: .63});
  assert.deepEqual(snapshot(a), snapshot(b));
  for (const dt of [.016, .09, .25, .02]) { a.advance(dt); b.advance(dt); }
  assert.deepEqual(snapshot(a), snapshot(b));
  assert.equal(a.parameters.seed, -2147483648);
});

test('six adjacent seeds separate scan heads and heartbeat phases at equal BPM', () => {
  const env = environment();
  const monitors = Array.from({length: 6}, (_, i) => env.create({seed: i + 1, bpm: 72, intensity: .7}));
  assert.equal(new Set(monitors.map(m => m.phase)).size, 6);
  assert.equal(new Set(monitors.map(m => m.head)).size, 6);
  assert.equal(new Set(monitors.map(m => JSON.stringify(Array.from(m.samples)))).size, 6);
  for (const m of monitors) {
    assert.equal(m.parameters.bpm, 72); assert.equal(m.parameters.intensity, .7);
    const phase = m.phase, elapsed = m.elapsed;
    m.advance(.125);
    near((m.phase - phase + 1) % 1, .15); near(m.elapsed - elapsed, .125);
    m.draw();
  }
});

test('same seed and partial parameter refresh preserve live history and frame clock', () => {
  const env = environment(), p = {seed: 42, bpm: 89, intensity: .6, sweepSeconds: 4};
  const m = env.create(p); m.advance(.2); m.lastTime = 1234;
  const state = snapshot(m), samples = m.samples;
  m.setParameters(p); m.setParameters(p); m.setParameters({color: '#abcdef'});
  assert.deepEqual(snapshot(m), state); assert.equal(m.samples, samples); assert.equal(m.lastTime, 1234);
  m.setParameters({seed: 42, bpm: 120, intensity: .2});
  assert.deepEqual(snapshot(m), state);
  const phase = m.phase; m.advance(.25); near((m.phase - phase + 1) % 1, .5);
});

test('changed seed applies current vitals then pre-fills deterministic history', () => {
  const env = environment(), m = env.create({seed: 12}); m.advance(.2);
  m.lastTime = 200;
  const p = {seed: -33, bpm: 137, intensity: .42, sweepSeconds: 2.7};
  m.setParameters(p);
  assert.deepEqual(snapshot(m), snapshot(env.create(p)));
  assert.equal(m.lastTime, 200);
  coherent(m, env.context.SilverChoirECG.waveform);
  for (let i = 0; i < 40; i++) {
    m.advance(.173); coherent(m, env.context.SilverChoirECG.waveform);
  }
});

test('resize and DPR changes preserve phase and elapsed, with coherent new samples', () => {
  const env = environment(), m = env.create({seed: 29, bpm: 112}); m.advance(.19);
  const phase = m.phase, elapsed = m.elapsed, samples = m.samples;
  m.resize(); assert.equal(m.samples, samples);
  m.canvas.clientWidth = 237; m.canvas.clientHeight = 72; m.resize();
  assert.equal(m.phase, phase); assert.equal(m.elapsed, elapsed);
  coherent(m, env.context.SilverChoirECG.waveform);
  env.context.devicePixelRatio = 2; env.emit('resize');
  assert.equal(m.phase, phase); assert.equal(m.elapsed, elapsed);
  coherent(m, env.context.SilverChoirECG.waveform); m.draw();
});

test('zero intensity or BPM stays flat across initial seed, reseed, update, resize, and advance', () => {
  const env = environment();
  for (const stopped of [{intensity: 0}, {bpm: 0}]) {
    const m = env.create({seed: 1, ...stopped}); flat(m);
    m.setParameters({seed: 2}); flat(m);
    m.canvas.clientWidth = 200; m.resize(); flat(m);
    m.advance(.25); flat(m);
    m.setParameters({bpm: 72, intensity: 1}); m.advance(.25);
    assert.ok(m.samples.some(value => value !== 0));
    const phase = m.phase, elapsed = m.elapsed;
    m.setParameters(stopped); flat(m);
    assert.equal(m.phase, phase); assert.equal(m.elapsed, elapsed);
  }
});

test('browser fallback is per-instance, nonzero, and stable for repeated zero or missing seed', () => {
  const env = environment();
  // Even identical wall clock and random values cannot synchronize instances in one page.
  const monitors = Array.from({length: 20}, () => env.create());
  assert.equal(new Set(monitors.map(m => m.parameters.seed)).size, monitors.length);
  assert.ok(monitors.every(m => Number.isInteger(m.parameters.seed) && m.parameters.seed !== 0));
  const m = monitors[0]; m.advance(.15);
  const state = snapshot(m);
  m.setParameters({seed: 0}); m.setParameters({seed: NaN}); m.setParameters({seed: Infinity}); m.setParameters({});
  assert.deepEqual(snapshot(m), state);
  for (const seed of [-2147483648, -1, 1, 2147483647]) {
    assert.equal(env.create({seed}).parameters.seed, seed);
  }
  assert.ok(Number.isFinite(env.create(null).phase));
});

test('native initial data and late events share one seed without resetting requestAnimationFrame', () => {
  const p = {seed: 512, bpm: 120, intensity: .5, color: '#ffbb22'};
  const env = environment(p, true), m = env.context.ecgMonitor;
  assert.deepEqual(snapshot(m), snapshot(environment().create(p)));
  assert.equal(env.frames.size, 1); m.start(); assert.equal(env.frames.size, 1);
  env.frame(1000); env.frame(1100);
  const state = snapshot(m), samples = m.samples;
  env.emit('ECGParameters', JSON.stringify(p));
  assert.deepEqual(snapshot(m), state); assert.equal(m.samples, samples);
  env.emit('ECGParameters', '{malformed'); assert.deepEqual(snapshot(m), state);
  env.emit('ECGParameters', JSON.stringify({...p, seed: 513}));
  assert.deepEqual(snapshot(m), snapshot(environment().create({...p, seed: 513})));
  env.emit('ECGParameters', {bpm: 60, intensity: 0}); flat(m);
  const head = m.head; env.frame(1200); flat(m); assert.notEqual(m.head, head);
  env.emit('unload'); assert.equal(env.frames.size, 0); assert.equal(m.lastTime, null);
});

test('browser bootstrap and malformed native initial payload still receive automatic seeds', () => {
  for (const data of [undefined, 'null', '{malformed']) {
    const env = environment(data, true), m = env.context.ecgMonitor;
    assert.ok(m.parameters.seed !== 0); assert.ok(Number.isFinite(m.phase));
    env.emit('ECGParameters', {seed: -77});
    assert.deepEqual(snapshot(m), snapshot(environment().create({seed: -77})));
    env.emit('unload');
  }
});

console.log(`${passed} ECG seed regression checks passed.`);
