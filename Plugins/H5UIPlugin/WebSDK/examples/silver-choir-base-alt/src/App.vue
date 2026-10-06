<script setup lang="ts">
import { computed, onMounted, onUnmounted, reactive, ref } from '@h5ui-plugin/vue'
import OrbitalSceneSelector from './components/OrbitalSceneSelector.vue'
import CameraCommandStrip from './components/CameraCommandStrip.vue'
import ResourceTelemetry from './components/ResourceTelemetry.vue'
import StrategicClock from './components/StrategicClock.vue'
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
const activeLocations = reactive<Record<SceneId, LocationId>>({
  haven: 'hall',
  underground: 'command-room'
})
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
  ;(window as H5UIWindow).ue?.emit(event, payload, 'base-control-alt')
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
  emitToUnreal('TimeControlChanged', {
    paused: paused.value,
    speed: paused.value ? 0 : speed.value
  })
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
  const nextLanguage = typeof detail === 'string'
    ? detail
    : (detail as { language?: string } | null)?.language
  if (nextLanguage === 'zh-CN' || nextLanguage === 'en-US') setLanguage(nextLanguage)
}

onMounted(() => {
  window.addEventListener(STATE_EVENT_NAME, handleStateEvent)
  window.addEventListener(LANGUAGE_EVENT_NAME, handleLanguageEvent)
  emitToUnreal('BaseHUDReady', {
    scene: activeScene.value,
    location: activeLocation.value,
    language: language.value,
    variant: 'orbital'
  })
})

onUnmounted(() => {
  window.removeEventListener(STATE_EVENT_NAME, handleStateEvent)
  window.removeEventListener(LANGUAGE_EVENT_NAME, handleLanguageEvent)
})
</script>

<template>
  <main id="base-hud-alt" class="base-hud-alt">
    <div class="top-rail" aria-hidden="true"><i></i><span></span><i></i></div>
    <div class="bottom-rail" aria-hidden="true"><i></i><span></span><i></i></div>
    <div class="frame-corner frame-left-top" aria-hidden="true"></div>
    <div class="frame-corner frame-right-top" aria-hidden="true"></div>

    <header class="command-identity">
      <div class="choir-sigil"><i></i><i></i><i></i><span></span></div>
      <div>
        <strong>{{ copy.status.channel }}</strong>
        <small>{{ copy.status.network }}</small>
      </div>
      <span class="identity-index">07</span>
    </header>

    <div class="sector-mark">
      <i></i>
      <span>{{ copy.status.sector }}</span>
    </div>

    <StrategicClock
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

    <ResourceTelemetry
      :heading="copy.currency.heading"
      :currency-name="copy.currency.name"
      :amount="currency"
      :delta="currencyDelta"
      :delta-label="copy.currency.deltaLabel"
    />

    <OrbitalSceneSelector
      :scenes="scenes"
      :active-scene="activeScene"
      :heading="copy.sceneHeading"
      :hint="copy.sceneHint"
      @select="selectScene"
    />

    <CameraCommandStrip
      :key="activeScene"
      :scene="activeScene"
      :locations="locations[activeScene]"
      :active-location="activeLocation"
      :heading="copy.locationHeading"
      :hint="copy.locationHint"
      @select="selectLocation"
    />

    <div id="base-overlay-host-alt" class="overlay-host-alt" :data-label="copy.status.overlayReady"></div>
  </main>
</template>

<style>
@keyframes alt-page-enter {
  0% { opacity: 0; }
  100% { opacity: 1; }
}

@keyframes identity-arrive {
  0% { opacity: 0; transform: translateY(-18px); }
  100% { opacity: 1; transform: translateY(0); }
}

@keyframes top-module-arrive {
  0% { opacity: 0; transform: translateY(-28px); }
  70% { opacity: 1; transform: translateY(4px); }
  100% { opacity: 1; transform: translateY(0); }
}

@keyframes orbit-arrive {
  0% { opacity: 0; transform: translateX(-44px) scale(0.9); }
  70% { opacity: 1; transform: translateX(5px) scale(1.02); }
  100% { opacity: 1; transform: translateX(0) scale(1); }
}

@keyframes strip-arrive {
  0% { opacity: 0; transform: translateY(42px); }
  70% { opacity: 1; transform: translateY(-5px); }
  100% { opacity: 1; transform: translateY(0); }
}

@keyframes orbit-spin {
  0% { transform: rotate(0deg); }
  100% { transform: rotate(360deg); }
}

@keyframes orbit-spin-reverse {
  0% { transform: rotate(360deg); }
  100% { transform: rotate(0deg); }
}

@keyframes ring-node-pulse {
  0% { opacity: 0.25; transform: scale(0.6); }
  100% { opacity: 1; transform: scale(1.15); }
}

@keyframes core-pulse {
  0% { border-color: #214f70; transform: scale(0.96); }
  100% { border-color: #5eb6df; transform: scale(1.02); }
}

@keyframes scene-lock {
  0% { opacity: 0.45; transform: scale(0.86); }
  58% { opacity: 1; transform: scale(1.08); }
  100% { opacity: 1; transform: scale(1); }
}

@keyframes scene-beacon {
  0% { opacity: 0.35; transform: scale(0.55); }
  100% { opacity: 1; transform: scale(1.25); }
}

@keyframes camera-rebuild {
  0% { opacity: 0; transform: translateY(18px); }
  100% { opacity: 1; transform: translateY(0); }
}

@keyframes camera-lock {
  0% { opacity: 0; transform: scaleX(0.1); }
  100% { opacity: 1; transform: scaleX(1); }
}

@keyframes clock-signal {
  0% { opacity: 0.35; transform: scale(0.6); }
  100% { opacity: 1; transform: scale(1.15); }
}

@keyframes digit-update {
  0% { opacity: 0.25; transform: translateY(-5px); }
  100% { opacity: 1; transform: translateY(0); }
}

@keyframes fast-drive {
  0% { opacity: 0.4; transform: translateX(-3px); }
  100% { opacity: 1; transform: translateX(3px); }
}

@keyframes reserve-update {
  0% { opacity: 0.35; transform: translateX(8px); }
  100% { opacity: 1; transform: translateX(0); }
}

@keyframes sigil-turn {
  0% { transform: rotate(0deg); opacity: 0.45; }
  100% { transform: rotate(90deg); opacity: 1; }
}

@keyframes rail-pulse {
  0% { opacity: 0.18; transform: scaleX(0.35); }
  100% { opacity: 0.8; transform: scaleX(1); }
}

* { box-sizing: border-box; }
html, body, #app { width: 100%; height: 100%; margin: 0; pointer-events: none; }
body {
  overflow: hidden;
  color: #d9edf8;
  background-color: transparent;
  font-family: Roboto, Arial, sans-serif;
}
button { font-family: Roboto, Arial, sans-serif; }

.base-hud-alt {
  position: relative;
  width: 100%;
  height: 100%;
  min-width: 960px;
  min-height: 540px;
  overflow: hidden;
  pointer-events: none;
  background-color: transparent;
  animation: alt-page-enter 0.35s ease-out;
}

.hud-interactive { pointer-events: auto; }
.hud-panel {
  background-color: rgba(4, 12, 19, 0.9);
  border: 1px solid #1c4e6d;
  box-shadow: 0 0 22px rgba(1, 6, 10, 0.74);
}

.top-rail, .bottom-rail {
  position: absolute;
  left: 28px;
  right: 28px;
  height: 5px;
  display: flex;
  align-items: center;
}
.top-rail { top: 22px; }
.bottom-rail { bottom: 18px; }
.top-rail span, .bottom-rail span { flex: 1; height: 1px; background-color: #183f58; }
.top-rail i, .bottom-rail i { display: block; width: 74px; height: 2px; background-color: #4ca9d2; transform-origin: center; animation: rail-pulse 1.6s ease-in-out infinite alternate; }
.top-rail i:last-child, .bottom-rail i:last-child { animation: rail-pulse 1.6s ease-in-out 0.65s infinite alternate; }

.frame-corner { position: absolute; top: 22px; width: 38px; height: 38px; border-top: 2px solid #3c93bc; }
.frame-left-top { left: 28px; border-left: 2px solid #3c93bc; }
.frame-right-top { right: 28px; border-right: 2px solid #3c93bc; }

.command-identity {
  position: absolute;
  top: 42px;
  left: 48px;
  width: 286px;
  height: 56px;
  display: flex;
  align-items: center;
  animation: identity-arrive 0.42s ease-out 0.04s both;
}
.choir-sigil {
  position: relative;
  width: 46px;
  height: 46px;
  margin-right: 13px;
  border: 1px solid #2a7399;
  border-radius: 50%;
}
.choir-sigil i { position: absolute; display: block; bottom: 11px; width: 3px; background-color: #61b8dd; }
.choir-sigil i:first-child { left: 12px; height: 9px; }
.choir-sigil i:nth-child(2) { left: 20px; height: 19px; background-color: #c1e4f4; }
.choir-sigil i:nth-child(3) { left: 28px; height: 13px; }
.choir-sigil span { position: absolute; display: block; top: 5px; left: 5px; width: 34px; height: 34px; border: 1px solid #173f58; border-radius: 50%; }
.command-identity > div:nth-child(2) { flex: 1; }
.command-identity strong { display: block; color: #b7d4e3; font-size: 9px; letter-spacing: 2px; font-weight: 500; }
.command-identity small { display: block; margin-top: 6px; color: #46758f; font-size: 7px; letter-spacing: 1.3px; }
.identity-index { color: #397b9e; font-size: 18px; letter-spacing: 2px; }

.sector-mark {
  position: absolute;
  right: 50px;
  top: 122px;
  display: flex;
  align-items: center;
  color: #315f7a;
  font-size: 7px;
  letter-spacing: 1.8px;
}
.sector-mark i { display: block; width: 6px; height: 6px; margin-right: 9px; border: 1px solid #4389ac; transform: rotate(45deg); }

.strategic-clock {
  position: absolute;
  top: 35px;
  left: 50%;
  width: 590px;
  height: 78px;
  margin-left: -295px;
  padding: 0 10px 0 18px;
  display: flex;
  align-items: center;
  animation: top-module-arrive 0.52s ease-out 0.08s both;
}
.clock-status { width: 122px; display: flex; align-items: center; }
.clock-status > i { display: block; width: 7px; height: 7px; margin-right: 10px; border-radius: 50%; background-color: #55b8e3; box-shadow: 0 0 8px #55b8e3; animation: clock-signal 0.75s ease-in-out infinite alternate; }
.clock-status small { display: block; color: #3f7897; font-size: 6px; letter-spacing: 1.2px; }
.clock-status strong { display: block; margin-top: 5px; color: #8fb1c2; font-size: 8px; letter-spacing: 1px; font-weight: 500; }
.paused .clock-status > i { background-color: #687782; box-shadow: none; animation: none; }
.clock-calendar { width: 116px; height: 44px; padding: 0 10px; display: flex; align-items: center; border-left: 1px solid #1a4058; border-right: 1px solid #1a4058; }
.clock-calendar span { flex: 1; text-align: center; }
.clock-calendar span strong { display: block; color: #d4e9f3; font-size: 16px; font-weight: 400; }
.clock-calendar span small { display: block; margin-top: 2px; color: #416b82; font-size: 6px; }
.clock-calendar > i { display: block; width: 1px; height: 21px; background-color: #1a4058; }
.clock-digits { flex: 1; padding-left: 20px; color: #e0f2fa; font-size: 28px; letter-spacing: 5px; font-weight: 400; animation: digit-update 0.3s ease-out; }
.clock-actions { display: flex; align-items: center; }
.clock-actions button { height: 46px; color: #8ebbd0; background-color: #071722; border: 1px solid #285c79; }
.clock-actions button:hover, .clock-actions button:focus { color: #ddf5ff; border-color: #62bde5; background-color: #0c293a; transform: translateY(-2px); }
.clock-actions button:active { transform: translateY(1px); }
.clock-actions button:first-child { width: 46px; display: flex; align-items: center; justify-content: center; }
.clock-actions button:last-child { width: 72px; margin-left: 6px; display: flex; align-items: center; justify-content: center; }
.clock-play { width: 0; height: 0; margin-left: 2px; border-top: 7px solid transparent; border-bottom: 7px solid transparent; border-left: 11px solid #8fd5f3; }
.clock-pause { display: flex; }
.clock-pause i { display: block; width: 4px; height: 14px; margin: 0 2px; background-color: #8fd5f3; }
.clock-fast { margin-right: 5px; color: #4a91b4; font-size: 17px; }
.accelerated .clock-fast { color: #7cd5fa; animation: fast-drive 0.42s ease-in-out infinite alternate; }
.clock-actions button strong { font-size: 9px; font-weight: 500; }

.resource-telemetry {
  position: absolute;
  top: 42px;
  right: 48px;
  width: 292px;
  height: 68px;
  padding: 0 17px;
  display: flex;
  align-items: center;
  justify-content: flex-end;
  text-align: right;
  animation: top-module-arrive 0.5s ease-out 0.14s both;
}
.resource-sigil { position: relative; width: 35px; height: 35px; margin-right: 14px; border: 1px solid #285f7e; transform: rotate(45deg); animation: sigil-turn 1.8s ease-in-out infinite alternate; }
.resource-sigil i { position: absolute; display: block; top: 8px; left: 8px; width: 17px; height: 17px; border: 1px solid #3c87aa; }
.resource-sigil span { position: absolute; display: block; top: 15px; left: 15px; width: 3px; height: 3px; background-color: #70cef2; }
.resource-telemetry small { display: block; color: #48778f; font-size: 6px; letter-spacing: 1.5px; }
.resource-telemetry > div:last-child > strong { display: block; margin-top: 3px; color: #dceef7; font-size: 21px; letter-spacing: 2px; font-weight: 400; animation: reserve-update 0.35s ease-out; }
.resource-telemetry > div:last-child > span { display: block; margin-top: 2px; color: #5ea77f; font-size: 6px; letter-spacing: 1px; }
.resource-telemetry > div:last-child > span.negative { color: #c06e79; }

.orbital-scene {
  position: absolute;
  top: 50%;
  left: 34px;
  width: 320px;
  height: 330px;
  margin-top: -150px;
  animation: orbit-arrive 0.6s ease-out 0.12s both;
}
.orbit-heading { position: absolute; top: 0; left: 80px; width: 168px; height: 38px; text-align: center; }
.orbit-heading small { display: block; color: #6ea6c0; font-size: 9px; letter-spacing: 3px; }
.orbit-heading strong { display: block; margin-top: 6px; color: #315f78; font-size: 6px; letter-spacing: 1.4px; font-weight: 400; }
.orbit-ring { position: absolute; top: 46px; left: 32px; width: 256px; height: 256px; border: 1px solid #24536e; border-radius: 50%; animation: orbit-spin 16s linear infinite; }
.orbit-ring > i { position: absolute; display: block; width: 7px; height: 7px; border: 1px solid #58aace; background-color: #071722; transform: rotate(45deg); animation: ring-node-pulse 0.9s ease-in-out infinite alternate; }
.orbit-ring > i:first-child { top: -4px; left: 123px; }
.orbit-ring > i:nth-child(2) { top: 123px; right: -4px; animation: ring-node-pulse 0.9s ease-in-out 0.2s infinite alternate; }
.orbit-ring > i:nth-child(3) { bottom: -4px; left: 123px; animation: ring-node-pulse 0.9s ease-in-out 0.4s infinite alternate; }
.orbit-ring > i:nth-child(4) { top: 123px; left: -4px; animation: ring-node-pulse 0.9s ease-in-out 0.6s infinite alternate; }
.orbit-ring-inner { top: 77px; left: 63px; width: 194px; height: 194px; border-color: #163b52; animation: orbit-spin-reverse 11s linear infinite; }
.orbit-ring-inner > i:first-child { top: 25px; left: 17px; }
.orbit-ring-inner > i:nth-child(2) { top: auto; right: 17px; bottom: 25px; left: auto; }
.orbit-core { position: absolute; top: 125px; left: 112px; width: 96px; height: 96px; display: flex; flex-direction: column; align-items: center; justify-content: center; border: 1px solid #285a77; border-radius: 50%; background-color: rgba(5, 15, 23, 0.88); box-shadow: 0 0 22px rgba(15, 71, 101, 0.5); animation: core-pulse 1.6s ease-in-out infinite alternate; }
.orbit-core span { color: #cce7f3; font-size: 22px; letter-spacing: 3px; }
.orbit-core small { margin-top: 6px; color: #427793; font-size: 6px; letter-spacing: 1px; }
.orbit-scene-button { position: absolute; width: 112px; height: 112px; padding: 0 10px; display: flex; flex-direction: column; align-items: center; justify-content: center; color: #6d8b9d; background-color: rgba(4, 12, 19, 0.94); border: 1px solid #1e4d69; border-radius: 50%; transition: transform 0.16s, border-color 0.16s, background-color 0.16s; }
.orbit-haven { top: 60px; right: 0; }
.orbit-underground { bottom: 2px; left: 2px; }
.orbit-scene-button > span { color: #397999; font-size: 7px; letter-spacing: 1px; }
.orbit-scene-button > strong { margin-top: 6px; color: #6d8b9d; font-size: 12px; letter-spacing: 1.5px; font-weight: 500; }
.orbit-scene-button > small { margin-top: 6px; color: #375e73; font-size: 5px; letter-spacing: 0.6px; }
.orbit-scene-button > i { display: block; width: 6px; height: 6px; margin-top: 7px; border-radius: 50%; border: 1px solid #397b9d; }
.orbit-scene-button:hover, .orbit-scene-button:focus { color: #cce8f5; border-color: #61b8de; background-color: #0a2230; transform: scale(1.06); }
.orbit-scene-button:hover > strong, .orbit-scene-button:focus > strong { color: #cce8f5; }
.orbit-scene-button:active { transform: scale(0.96); }
.orbit-scene-button.active { color: #e3f6ff; border-color: #6bc7ec; background-color: #0c2c3f; animation: scene-lock 0.36s ease-out; }
.orbit-scene-button.active > strong { color: #e3f6ff; }
.orbit-scene-button.active > i { border-color: #9be3ff; background-color: #67c8ef; box-shadow: 0 0 8px #4eb7e1; animation: scene-beacon 0.65s ease-in-out infinite alternate; }

.camera-strip {
  position: absolute;
  right: 48px;
  bottom: 34px;
  left: 374px;
  height: 112px;
  padding: 8px;
  display: flex;
  animation: strip-arrive 0.58s ease-out 0.18s both;
}
.camera-strip > header { width: 164px; height: 94px; padding: 18px 18px 0; border-right: 1px solid #1d4965; }
.camera-strip > header > span { display: block; color: #59a8ca; font-size: 15px; letter-spacing: 3px; }
.camera-strip > header > small { display: block; margin-top: 8px; color: #4b7f98; font-size: 7px; letter-spacing: 1.5px; }
.camera-strip > header > strong { display: block; margin-top: 7px; color: #829fac; font-size: 7px; letter-spacing: 1px; font-weight: 400; }
.camera-options { flex: 1; padding-left: 8px; display: flex; animation: camera-rebuild 0.36s ease-out; }
.camera-node { position: relative; flex: 1; height: 94px; margin-left: 6px; padding: 0 14px; overflow: hidden; display: flex; flex-direction: column; align-items: flex-start; justify-content: center; text-align: left; color: #708d9d; background-color: #06131c; border: 1px solid #173d55; transition: transform 0.14s, border-color 0.14s, background-color 0.14s; }
.camera-node > span { color: #397999; font-size: 7px; letter-spacing: 1.3px; }
.camera-node > strong { margin-top: 9px; color: #708d9d; font-size: 10px; letter-spacing: 1.3px; font-weight: 500; }
.camera-node > i { position: absolute; display: block; left: 0; right: 0; bottom: 0; height: 3px; background-color: #24536d; transform-origin: left center; }
.camera-node:hover, .camera-node:focus { color: #c8e4f1; border-color: #4e9fc5; background-color: #0a2230; transform: translateY(-5px); }
.camera-node:hover > strong, .camera-node:focus > strong { color: #c8e4f1; }
.camera-node:active { transform: translateY(1px); }
.camera-node.active { color: #e0f3fb; border-color: #5ab3db; background-color: #0b293b; }
.camera-node.active > strong { color: #e0f3fb; }
.camera-node.active > i { background-color: #6fcdf2; animation: camera-lock 0.34s ease-out; }

.overlay-host-alt { position: absolute; z-index: 100; top: 142px; right: 62px; bottom: 164px; left: 374px; pointer-events: none; }

@media (max-height: 760px) {
  .strategic-clock { top: 28px; }
  .resource-telemetry { top: 34px; }
  .orbital-scene { transform: scale(0.88); transform-origin: left center; }
  .camera-strip { bottom: 24px; height: 102px; }
  .camera-strip > header, .camera-node { height: 84px; }
}
</style>
