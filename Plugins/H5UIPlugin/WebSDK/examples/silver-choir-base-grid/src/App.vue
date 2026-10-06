<script setup lang="ts">
import { computed, onMounted, onUnmounted, reactive, ref } from '@h5ui-plugin/vue'
import SectorSceneTabs from './components/SectorSceneTabs.vue'
import CameraSpine from './components/CameraSpine.vue'
import ChronometerPod from './components/ChronometerPod.vue'
import AssetBeacon from './components/AssetBeacon.vue'
import localizationConfig from './localization.json'
import type { CalendarTime, LocationId, LocationOption, SceneId, SceneOption } from './types'

type Language = keyof typeof localizationConfig.languages
type H5UIWindow = Window & {
  ue?: { emit: (event: string, payload?: unknown, source?: string) => void }
}

type BaseHudState = {
  language?: Language
  scene?: SceneId
  location?: LocationId
  currency?: number
  currencyDelta?: number
  paused?: boolean
  speed?: number
  time?: Partial<CalendarTime>
}

const STATE_EVENT_NAME = 'SilverBaseHUDState'
const LANGUAGE_EVENT_NAME = 'SilverBaseHUDLanguage'
const fastSpeeds = [5, 10, 20]

const language = ref<Language>(localizationConfig.defaultLanguage as Language)
const copy = computed(() => localizationConfig.languages[language.value])
const activeScene = ref<SceneId>('haven')
const activeLocations = reactive<Record<SceneId, LocationId>>({ haven: 'hall', underground: 'command-room' })
const currency = ref(128460)
const currencyDelta = ref(2340)
const paused = ref(false)
const speed = ref(1)
const time = reactive<CalendarTime>({ month: 9, day: 18, hour: 21, minute: 40 })

const scenes = computed<SceneOption[]>(() => [
  { id: 'haven', code: '01', ...copy.value.scenes.haven },
  { id: 'underground', code: '02', ...copy.value.scenes.underground }
])

const locations = computed<Record<SceneId, LocationOption[]>>(() => ({
  haven: [
    { id: 'hall', code: 'A1', label: copy.value.locations.hall },
    { id: 'kitchen', code: 'A2', label: copy.value.locations.kitchen },
    { id: 'office', code: 'A3', label: copy.value.locations.office }
  ],
  underground: [
    { id: 'command-room', code: 'B1', label: copy.value.locations.commandRoom },
    { id: 'readiness-room', code: 'B2', label: copy.value.locations.readinessRoom },
    { id: 'squad-room', code: 'B3', label: copy.value.locations.squadRoom },
    { id: 'commander-office', code: 'B4', label: copy.value.locations.commanderOffice }
  ]
}))

const activeLocation = computed(() => activeLocations[activeScene.value])

function emitToUnreal(event: string, payload: unknown): void {
  ;(window as H5UIWindow).ue?.emit(event, payload, 'base-control-grid')
}

function selectScene(scene: SceneId): void {
  activeScene.value = scene
  emitToUnreal('BaseSceneChanged', { scene, location: activeLocations[scene] })
}

function selectLocation(location: LocationId): void {
  activeLocations[activeScene.value] = location
  emitToUnreal('CameraFocusRequested', { scene: activeScene.value, location })
}

function togglePause(): void {
  paused.value = !paused.value
  speed.value = paused.value ? 0 : 1
  emitTimeControl()
}

function cycleSpeed(): void {
  const currentIndex = fastSpeeds.indexOf(speed.value)
  speed.value = fastSpeeds[(currentIndex + 1) % fastSpeeds.length]
  paused.value = false
  emitTimeControl()
}

function emitTimeControl(): void {
  emitToUnreal('TimeControlChanged', { paused: paused.value, speed: paused.value ? 0 : speed.value })
}

function setLanguage(nextLanguage: Language): void {
  language.value = nextLanguage
  document.querySelector('html')?.setAttribute('lang', copy.value.htmlLanguage)
  const title = document.querySelector('title')
  if (title) title.textContent = copy.value.documentTitle
}

function readEventDetail(event: Event): unknown {
  const raw = (event as CustomEvent).detail
  if (typeof raw !== 'string') return raw
  try {
    return JSON.parse(raw)
  } catch {
    return raw
  }
}

function applyState(nextState: BaseHudState): void {
  if (nextState.language === 'zh-CN' || nextState.language === 'en-US') setLanguage(nextState.language)
  if (nextState.scene === 'haven' || nextState.scene === 'underground') activeScene.value = nextState.scene
  if (nextState.location && locations.value[activeScene.value].some(item => item.id === nextState.location)) {
    activeLocations[activeScene.value] = nextState.location
  }
  if (typeof nextState.currency === 'number') currency.value = nextState.currency
  if (typeof nextState.currencyDelta === 'number') currencyDelta.value = nextState.currencyDelta
  if (typeof nextState.paused === 'boolean') paused.value = nextState.paused
  if (typeof nextState.speed === 'number') speed.value = nextState.speed
  if (nextState.time) {
    if (typeof nextState.time.month === 'number') time.month = nextState.time.month
    if (typeof nextState.time.day === 'number') time.day = nextState.time.day
    if (typeof nextState.time.hour === 'number') time.hour = nextState.time.hour
    if (typeof nextState.time.minute === 'number') time.minute = nextState.time.minute
  }
}

function handleStateEvent(event: Event): void {
  const detail = readEventDetail(event)
  if (detail && typeof detail === 'object') applyState(detail as BaseHudState)
}

function handleLanguageEvent(event: Event): void {
  const detail = readEventDetail(event)
  const nextLanguage = typeof detail === 'string' ? detail : (detail as { language?: string } | null)?.language
  if (nextLanguage === 'zh-CN' || nextLanguage === 'en-US') setLanguage(nextLanguage)
}

onMounted(() => {
  window.addEventListener(STATE_EVENT_NAME, handleStateEvent)
  window.addEventListener(LANGUAGE_EVENT_NAME, handleLanguageEvent)
  emitToUnreal('BaseHUDReady', {
    scene: activeScene.value,
    location: activeLocation.value,
    language: language.value,
    variant: 'lattice'
  })
})

onUnmounted(() => {
  window.removeEventListener(STATE_EVENT_NAME, handleStateEvent)
  window.removeEventListener(LANGUAGE_EVENT_NAME, handleLanguageEvent)
})
</script>

<template>
  <main id="base-hud-grid" class="base-hud-grid">
    <div class="grid-frame grid-frame-top"><i></i><span></span><i></i></div>
    <div class="grid-frame grid-frame-right"><i></i><span></span><i></i></div>

    <AssetBeacon
      :channel="copy.status.channel"
      :network="copy.status.network"
      :heading="copy.currency.heading"
      :currency-name="copy.currency.name"
      :amount="currency"
      :delta="currencyDelta"
      :delta-label="copy.currency.deltaLabel"
    />

    <div class="grid-sector-tag">
      <i></i><span>{{ copy.status.sector }}</span><b></b>
    </div>

    <CameraSpine
      :key="activeScene"
      :scene="activeScene"
      :locations="locations[activeScene]"
      :active-location="activeLocation"
      :heading="copy.locationHeading"
      :hint="copy.locationHint"
      @select="selectLocation"
    />

    <ChronometerPod
      :time="time"
      :paused="paused"
      :speed="speed"
      :month-label="copy.time.month"
      :day-label="copy.time.day"
      :heading="copy.time.heading"
      :paused-label="copy.time.paused"
      :running-label="copy.time.running"
      :pause-hint="copy.time.pauseHint"
      :fast-hint="copy.time.fastHint"
      @toggle-pause="togglePause"
      @cycle-speed="cycleSpeed"
    />

    <SectorSceneTabs
      :scenes="scenes"
      :active-scene="activeScene"
      :heading="copy.sceneHeading"
      :hint="copy.sceneHint"
      @select="selectScene"
    />

    <div id="base-overlay-host-grid" class="overlay-host-grid" :data-label="copy.status.overlayReady"></div>
  </main>
</template>

<style>
@keyframes lattice-enter {
  0% { opacity: 0; }
  100% { opacity: 1; }
}

@keyframes beacon-enter {
  0% { opacity: 0; transform: translateX(-34px) translateY(-12px); }
  70% { opacity: 1; transform: translateX(5px) translateY(2px); }
  100% { opacity: 1; transform: translateX(0) translateY(0); }
}

@keyframes spine-enter {
  0% { opacity: 0; transform: translateX(42px); }
  70% { opacity: 1; transform: translateX(-6px); }
  100% { opacity: 1; transform: translateX(0); }
}

@keyframes chrono-enter {
  0% { opacity: 0; transform: translateX(-32px) translateY(24px); }
  100% { opacity: 1; transform: translateX(0) translateY(0); }
}

@keyframes deck-enter {
  0% { opacity: 0; transform: translateY(38px); }
  70% { opacity: 1; transform: translateY(-5px); }
  100% { opacity: 1; transform: translateY(0); }
}

@keyframes sigil-pulse {
  0% { opacity: 0.4; transform: rotate(0deg) scale(0.9); }
  100% { opacity: 1; transform: rotate(90deg) scale(1.08); }
}

@keyframes value-update {
  0% { opacity: 0.25; transform: translateY(5px); }
  100% { opacity: 1; transform: translateY(0); }
}

@keyframes sector-signal {
  0% { opacity: 0.25; transform: scaleX(0.45); }
  100% { opacity: 0.85; transform: scaleX(1); }
}

@keyframes spine-rebuild {
  0% { opacity: 0; transform: translateX(18px); }
  100% { opacity: 1; transform: translateX(0); }
}

@keyframes node-lock {
  0% { opacity: 0; transform: scale(0.4) rotate(45deg); }
  60% { opacity: 1; transform: scale(1.5) rotate(45deg); }
  100% { opacity: 1; transform: scale(1) rotate(45deg); }
}

@keyframes node-bar {
  0% { opacity: 0; transform: scaleX(0.1); }
  100% { opacity: 1; transform: scaleX(1); }
}

@keyframes clock-pulse {
  0% { opacity: 0.35; transform: scale(0.6); }
  100% { opacity: 1; transform: scale(1.2); }
}

@keyframes digit-shift {
  0% { opacity: 0.2; transform: translateX(-7px); }
  100% { opacity: 1; transform: translateX(0); }
}

@keyframes fast-shift {
  0% { opacity: 0.4; transform: translateX(-3px); }
  100% { opacity: 1; transform: translateX(4px); }
}

@keyframes scene-select {
  0% { opacity: 0.5; transform: translateY(5px) scale(0.97); }
  58% { opacity: 1; transform: translateY(-4px) scale(1.02); }
  100% { opacity: 1; transform: translateY(0) scale(1); }
}

@keyframes scene-edge {
  0% { opacity: 0.25; transform: scaleY(0.45); }
  100% { opacity: 1; transform: scaleY(1); }
}

* { box-sizing: border-box; }
html, body, #app { width: 100%; height: 100%; margin: 0; pointer-events: none; }
body {
  overflow: hidden;
  color: #dbeef7;
  background-color: transparent;
  font-family: Roboto, Arial, sans-serif;
}
button { font-family: Roboto, Arial, sans-serif; }

.base-hud-grid {
  position: relative;
  width: 100%;
  height: 100%;
  min-width: 960px;
  min-height: 540px;
  overflow: hidden;
  pointer-events: none;
  background-color: transparent;
  animation: lattice-enter 0.35s ease-out;
}
.hud-interactive { pointer-events: auto; }
.hud-panel { background-color: rgba(4, 12, 19, 0.92); border: 1px solid #1d4c68; box-shadow: 0 0 24px rgba(1, 7, 11, 0.76); }

.grid-frame { position: absolute; display: flex; align-items: center; opacity: 0.78; }
.grid-frame span { flex: 1; height: 1px; background-color: #173b52; }
.grid-frame i { display: block; width: 48px; height: 2px; background-color: #50a8cc; transform-origin: center; animation: sector-signal 1.5s ease-in-out infinite alternate; }
.grid-frame i:last-child { animation: sector-signal 1.5s ease-in-out 0.55s infinite alternate; }
.grid-frame-top { top: 22px; left: 28px; right: 28px; height: 3px; }
.grid-frame-right { top: 52px; right: 20px; bottom: 190px; width: 3px; flex-direction: column; }
.grid-frame-right span { width: 1px; height: auto; }
.grid-frame-right i { width: 2px; height: 48px; }

.asset-beacon {
  position: absolute;
  top: 42px;
  left: 48px;
  width: 438px;
  height: 92px;
  padding: 10px 16px;
  display: flex;
  align-items: center;
  animation: beacon-enter 0.52s ease-out 0.05s both;
}
.beacon-brand { flex: 1; display: flex; align-items: center; }
.grid-sigil { position: relative; width: 48px; height: 48px; margin-right: 13px; border: 1px solid #2e7193; }
.grid-sigil span { position: absolute; display: block; top: 9px; left: 9px; width: 28px; height: 28px; border: 1px solid #3b86a8; transform: rotate(45deg); animation: sigil-pulse 1.7s ease-in-out infinite alternate; }
.grid-sigil i, .grid-sigil b { position: absolute; display: block; background-color: #72caeb; }
.grid-sigil i { top: 22px; left: 11px; width: 26px; height: 2px; }
.grid-sigil b { top: 11px; left: 22px; width: 2px; height: 26px; }
.beacon-brand > div:last-child strong { display: block; color: #b9d7e5; font-size: 8px; letter-spacing: 2px; font-weight: 500; }
.beacon-brand > div:last-child small { display: block; margin-top: 6px; color: #47768e; font-size: 6px; letter-spacing: 1.2px; }
.beacon-value { width: 150px; padding-left: 15px; text-align: right; border-left: 1px solid #1a4159; }
.beacon-value small { display: block; color: #44758e; font-size: 6px; letter-spacing: 1.1px; }
.beacon-value > strong { display: block; margin-top: 4px; color: #dff2fa; font-size: 20px; letter-spacing: 2px; font-weight: 400; animation: value-update 0.36s ease-out; }
.beacon-value > span { display: block; margin-top: 3px; color: #5ea87f; font-size: 6px; letter-spacing: 0.7px; }
.beacon-value > span.negative { color: #bf6d77; }

.grid-sector-tag { position: absolute; top: 50px; left: 50%; width: 320px; height: 28px; margin-left: -160px; display: flex; align-items: center; color: #48768e; font-size: 7px; letter-spacing: 2px; }
.grid-sector-tag i, .grid-sector-tag b { flex: 1; display: block; height: 1px; background-color: #24516b; transform-origin: center; animation: sector-signal 1.8s ease-in-out infinite alternate; }
.grid-sector-tag b { animation: sector-signal 1.8s ease-in-out 0.7s infinite alternate; }
.grid-sector-tag span { margin: 0 12px; }

.camera-spine {
  position: absolute;
  top: 188px;
  right: 42px;
  width: 326px;
  min-height: 350px;
  animation: spine-enter 0.55s ease-out 0.1s both;
}
.camera-spine > header { height: 92px; padding: 14px 16px 0 46px; text-align: right; }
.camera-spine > header > div { display: flex; align-items: center; justify-content: flex-end; }
.camera-spine > header > div i { display: block; width: 7px; height: 7px; margin-right: 9px; border: 1px solid #5daaca; transform: rotate(45deg); }
.camera-spine > header > div span { color: #66b5d5; font-size: 18px; letter-spacing: 3px; }
.camera-spine > header small { display: block; margin-top: 9px; color: #4d7e96; font-size: 7px; letter-spacing: 1.7px; }
.camera-spine > header strong { display: block; margin-top: 6px; color: #7d9aa9; font-size: 7px; letter-spacing: 1.1px; font-weight: 400; }
.spine-track { position: absolute; top: 86px; bottom: 24px; right: 24px; width: 1px; background-color: #24516b; }
.spine-options { position: relative; padding-right: 14px; animation: spine-rebuild 0.36s ease-out; }
.spine-node { position: relative; width: 100%; height: 68px; margin-bottom: 7px; padding: 0 40px 0 17px; display: flex; align-items: center; text-align: left; color: #718d9d; background-color: rgba(4, 13, 20, 0.91); border: 1px solid #173d54; transition: transform 0.14s, border-color 0.14s, background-color 0.14s; }
.spine-node > i { position: absolute; display: block; top: 29px; right: 7px; width: 9px; height: 9px; border: 1px solid #3d7897; background-color: #06131c; transform: rotate(45deg); }
.spine-node > span { width: 44px; color: #3b7695; font-size: 7px; letter-spacing: 1px; }
.spine-node > strong { flex: 1; color: #718d9d; font-size: 10px; letter-spacing: 1.4px; font-weight: 500; }
.spine-node > b { position: absolute; display: block; left: 0; top: 0; bottom: 0; width: 3px; background-color: #1e4c66; transform-origin: center; }
.spine-node:hover, .spine-node:focus { color: #d2ebf6; border-color: #53a6ca; background-color: #0a2331; transform: translateX(-8px); }
.spine-node:hover > strong, .spine-node:focus > strong { color: #d2ebf6; }
.spine-node:active { transform: translateX(-3px); }
.spine-node.active { color: #e0f4fc; border-color: #67bddd; background-color: #0b2939; transform: translateX(-12px); }
.spine-node.active > strong { color: #e0f4fc; }
.spine-node.active > i { border-color: #93dcf7; background-color: #5ebdde; animation: node-lock 0.36s ease-out; }
.spine-node.active > b { background-color: #6ac9ed; animation: node-bar 0.35s ease-out; }

.chronometer {
  position: absolute;
  left: 48px;
  bottom: 40px;
  width: 430px;
  height: 154px;
  padding: 14px 14px 12px;
  animation: chrono-enter 0.55s ease-out 0.12s both;
}
.chronometer > header { height: 32px; display: flex; align-items: center; border-bottom: 1px solid #1b435b; }
.chronometer > header > i { display: block; width: 7px; height: 7px; margin: 0 10px 0 3px; border-radius: 50%; background-color: #5bb9df; box-shadow: 0 0 8px #5bb9df; animation: clock-pulse 0.76s ease-in-out infinite alternate; }
.chronometer > header small { color: #3f7894; font-size: 6px; letter-spacing: 1.2px; }
.chronometer > header strong { margin-left: 9px; color: #8ba8b8; font-size: 7px; letter-spacing: 1px; font-weight: 500; }
.chronometer.paused > header > i { background-color: #697780; box-shadow: none; animation: none; }
.chrono-readout { height: 62px; display: flex; align-items: center; }
.chrono-date { width: 115px; display: flex; align-items: center; }
.chrono-date span { flex: 1; text-align: center; }
.chrono-date span strong { display: block; color: #cce4ef; font-size: 16px; font-weight: 400; }
.chrono-date span small { display: block; margin-top: 2px; color: #3e6c84; font-size: 6px; }
.chrono-date > i { display: block; width: 1px; height: 25px; background-color: #1d465e; }
.chrono-digits { flex: 1; padding-left: 18px; color: #e2f4fb; font-size: 30px; letter-spacing: 6px; font-weight: 400; animation: digit-shift 0.3s ease-out; }
.chrono-actions { height: 34px; display: flex; align-items: center; justify-content: flex-end; }
.chrono-actions button { height: 32px; color: #83aec2; background-color: #071721; border: 1px solid #285b77; display: flex; align-items: center; justify-content: center; }
.chrono-actions button:first-child { width: 50px; }
.chrono-actions button:last-child { width: 82px; margin-left: 7px; }
.chrono-actions button:hover, .chrono-actions button:focus { color: #dff6ff; border-color: #60b9df; background-color: #0b2636; transform: translateY(-2px); }
.chrono-actions button:active { transform: translateY(1px); }
.chrono-play { width: 0; height: 0; margin-left: 2px; border-top: 6px solid transparent; border-bottom: 6px solid transparent; border-left: 10px solid #8dd5f2; }
.chrono-pause { display: flex; }
.chrono-pause i { display: block; width: 4px; height: 13px; margin: 0 2px; background-color: #8dd5f2; }
.chrono-fast { margin-right: 6px; color: #4a91b4; font-size: 16px; }
.accelerated .chrono-fast { color: #79d2f4; animation: fast-shift 0.42s ease-in-out infinite alternate; }
.chrono-actions strong { font-size: 9px; font-weight: 500; }

.scene-deck {
  position: absolute;
  right: 392px;
  bottom: 40px;
  left: 518px;
  height: 128px;
  padding: 8px;
  display: flex;
  animation: deck-enter 0.56s ease-out 0.17s both;
}
.scene-deck > header { width: 164px; padding: 25px 17px 0; border-right: 1px solid #1b445d; }
.scene-deck > header small { display: block; color: #5594b2; font-size: 8px; letter-spacing: 1.8px; }
.scene-deck > header strong { display: block; margin-top: 9px; color: #748f9e; font-size: 7px; letter-spacing: 1px; font-weight: 400; }
.scene-deck-options { flex: 1; padding-left: 8px; display: flex; }
.scene-deck-button { position: relative; flex: 1; height: 110px; margin-left: 6px; padding: 0 20px; display: flex; align-items: center; text-align: left; color: #718d9d; background-color: #06131c; border: 1px solid #173d54; transition: transform 0.14s, border-color 0.14s, background-color 0.14s; }
.scene-deck-button > span { width: 48px; color: #3d7897; font-size: 8px; letter-spacing: 1px; }
.scene-deck-button > div { flex: 1; }
.scene-deck-button > div strong { display: block; color: #718d9d; font-size: 13px; letter-spacing: 2px; font-weight: 500; }
.scene-deck-button > div small { display: block; margin-top: 8px; color: #3c667b; font-size: 6px; letter-spacing: 1px; }
.scene-deck-button > i { position: absolute; display: block; top: 8px; right: 8px; width: 9px; height: 9px; border-top: 1px solid #356f8d; border-right: 1px solid #356f8d; }
.scene-deck-button > b { position: absolute; display: block; top: 20px; right: 8px; bottom: 20px; width: 3px; background-color: #1d4a64; transform-origin: center; }
.scene-deck-button:hover, .scene-deck-button:focus { color: #d3ecf7; border-color: #50a4c9; background-color: #0a2331; transform: translateY(-4px); }
.scene-deck-button:hover > div strong, .scene-deck-button:focus > div strong { color: #d3ecf7; }
.scene-deck-button:active { transform: translateY(1px); }
.scene-deck-button.active { color: #e4f6fd; border-color: #63bbdf; background-color: #0b293a; animation: scene-select 0.36s ease-out; }
.scene-deck-button.active > div strong { color: #e4f6fd; }
.scene-deck-button.active > b { background-color: #67c6ea; animation: scene-edge 0.68s ease-in-out infinite alternate; }

.overlay-host-grid { position: absolute; z-index: 100; top: 152px; right: 398px; bottom: 188px; left: 510px; pointer-events: none; }

@media (max-height: 760px) {
  .camera-spine { top: 136px; transform: scale(0.9); transform-origin: right top; }
  .chronometer { bottom: 26px; height: 142px; }
  .scene-deck { bottom: 26px; height: 116px; }
  .scene-deck-button { height: 98px; }
}
</style>
