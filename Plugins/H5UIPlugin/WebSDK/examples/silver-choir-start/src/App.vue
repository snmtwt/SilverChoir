<script setup lang="ts">
import { computed, onMounted, onUnmounted, reactive, ref } from '@h5ui-plugin/vue'
import MenuButton from './MenuButton.vue'
import localizationConfig from './localization.json'

type MenuItem = {
  index: string
  label: string
  sublabel: string
  action: string
  primary?: boolean
  danger?: boolean
}

type Language = keyof typeof localizationConfig.languages
type Page = 'menu' | 'settings'
type SettingsTab = 'general' | 'display' | 'audio' | 'controls'
type NumericSettingKey = 'brightness' | 'masterVolume' | 'musicVolume' | 'effectsVolume' | 'voiceVolume' | 'sensitivity'
type ScreenPhase = 'before-enter' | 'entering' | 'exiting' | 'after-leave'

const ACTION_CONFIRM_DELAY_MS = 240
const SCREEN_EXIT_DURATION_MS = 520
const ENTER_EVENT_NAME = 'SilverUIEnter'
const BEFORE_ENTER_EVENT_NAME = 'SilverUIBeforeEnter'
const AFTER_LEAVE_EVENT_NAME = 'SilverUIAfterLeave'

const defaultSettings = {
  difficulty: 1,
  subtitles: true,
  tutorials: true,
  windowMode: 1,
  resolution: 0,
  brightness: 65,
  vsync: true,
  masterVolume: 80,
  musicVolume: 70,
  effectsVolume: 85,
  voiceVolume: 80,
  sensitivity: 50,
  invertY: false,
  edgeScroll: true,
  cameraShake: true
}

const language = ref<Language>(localizationConfig.defaultLanguage as Language)
const copy = computed(() => localizationConfig.languages[language.value])
const currentPage = ref<Page>('menu')
const activeSettingsTab = ref<SettingsTab>('general')
const settings = reactive({ ...defaultSettings })
const settingsNotice = ref('')

const settingsTabs = computed(() => [
  { id: 'general' as const, index: '01', label: copy.value.settingsPage.tabs.general },
  { id: 'display' as const, index: '02', label: copy.value.settingsPage.tabs.display },
  { id: 'audio' as const, index: '03', label: copy.value.settingsPage.tabs.audio },
  { id: 'controls' as const, index: '04', label: copy.value.settingsPage.tabs.controls }
])
const currentSettingsTitle = computed(() => copy.value.settingsPage[activeSettingsTab.value].title)
const currentSettingsTabLabel = computed(() => settingsTabs.value.find(tab => tab.id === activeSettingsTab.value)?.label || '')
const audioSettings = computed<{ key: NumericSettingKey, label: string, hint: string }[]>(() => [
  { key: 'masterVolume', label: copy.value.settingsPage.audio.master, hint: copy.value.settingsPage.audio.masterHint },
  { key: 'musicVolume', label: copy.value.settingsPage.audio.music, hint: copy.value.settingsPage.audio.musicHint },
  { key: 'effectsVolume', label: copy.value.settingsPage.audio.effects, hint: copy.value.settingsPage.audio.effectsHint },
  { key: 'voiceVolume', label: copy.value.settingsPage.audio.voice, hint: copy.value.settingsPage.audio.voiceHint }
])

const menuItems = computed<MenuItem[]>(() => [
  { index: '01', ...copy.value.menu.newGame, action: 'new-game', primary: true },
  { index: '02', ...copy.value.menu.continue, action: 'continue' },
  { index: '03', ...copy.value.menu.settings, action: 'settings' },
  { index: '04', ...copy.value.menu.archives, action: 'credits' },
  { index: '05', ...copy.value.menu.quit, action: 'quit', danger: true }
])

const activeAction = ref('standby')
const pendingAction = ref('')
const screenPhase = ref<ScreenPhase>('before-enter')
const enterCycle = ref(0)
const screenTransitionClass = computed(() => {
  if (screenPhase.value === 'before-enter') return 'screen-before-enter'
  if (screenPhase.value === 'exiting') return 'screen-exiting'
  if (screenPhase.value === 'after-leave') return 'screen-after-leave'
  return enterCycle.value % 2 === 0 ? 'screen-entering-a' : 'screen-entering-b'
})
const activeActionLabel = computed(() => menuItems.value.find(item => item.action === activeAction.value)?.label || '')
const systemMessage = computed(() => activeAction.value === 'standby'
  ? copy.value.awaitingCommand
  : `${copy.value.directiveSent} / ${activeActionLabel.value}`)

type H5UIWindow = Window & {
  H5UI?: { emit: (eventType: string, functionName: string, ...arguments_: unknown[]) => boolean }
  ue?: { emit: (event: string, payload?: unknown, source?: string) => void }
}

let actionTimer: ReturnType<typeof setTimeout> | undefined
let transitionTimer: ReturnType<typeof setTimeout> | undefined

function clearTimer(timer: ReturnType<typeof setTimeout> | undefined): void {
  if (timer !== undefined) clearTimeout(timer)
}

function playEnterEffect(): void {
  clearTimer(actionTimer)
  clearTimer(transitionTimer)
  actionTimer = undefined
  transitionTimer = undefined
  pendingAction.value = ''
  activeAction.value = 'standby'
  enterCycle.value += 1
  screenPhase.value = 'entering'
}

function handleEnterEvent(): void {
  playEnterEffect()
}

function switchToBeforeEnterState(): void {
  clearTimer(actionTimer)
  clearTimer(transitionTimer)
  actionTimer = undefined
  transitionTimer = undefined
  pendingAction.value = ''
  activeAction.value = 'standby'
  screenPhase.value = 'before-enter'
}

function switchToAfterLeaveState(): void {
  clearTimer(actionTimer)
  clearTimer(transitionTimer)
  actionTimer = undefined
  transitionTimer = undefined
  pendingAction.value = ''
  screenPhase.value = 'after-leave'
}

function handleBeforeEnterEvent(): void {
  switchToBeforeEnterState()
}

function handleAfterLeaveEvent(): void {
  switchToAfterLeaveState()
}

function emitToUnreal(event: string, payload: unknown): void {
  ;(window as H5UIWindow).ue?.emit(event, payload, 'start-menu')
}

function emitStartGame(): void {
  const host = window as H5UIWindow
  if (host.H5UI?.emit('NewGame', 'StartGame', language.value)) return
  host.ue?.emit('NewGame', { action: 'new-game', language: language.value }, 'start-menu')
}

function toggleLanguage(): void {
  setLanguage(language.value === 'zh-CN' ? 'en-US' : 'zh-CN')
}

function setLanguage(nextLanguage: Language): void {
  language.value = nextLanguage
  document.querySelector('html')?.setAttribute('lang', copy.value.htmlLanguage)
  const title = document.querySelector('title')
  if (title) title.textContent = copy.value.documentTitle
  emitToUnreal('LanguageChanged', { language: language.value })
}

function setLanguageFromOption(nextLanguage: string): void {
  if (nextLanguage === 'zh-CN' || nextLanguage === 'en-US') setLanguage(nextLanguage)
}

function activate(action: string): void {
  if (pendingAction.value || screenPhase.value === 'exiting') return
  pendingAction.value = action
  activeAction.value = action
  clearTimer(actionTimer)
  actionTimer = setTimeout(() => {
    actionTimer = undefined
    if (action === 'new-game') {
      screenPhase.value = 'exiting'
      clearTimer(transitionTimer)
      transitionTimer = setTimeout(() => {
        transitionTimer = undefined
        switchToAfterLeaveState()
        emitStartGame()
      }, SCREEN_EXIT_DURATION_MS)
      return
    }
    if (action === 'settings') {
      settingsNotice.value = ''
      activeSettingsTab.value = 'general'
      currentPage.value = 'settings'
    }
    emitToUnreal('MainMenuAction', { action, language: language.value })
    pendingAction.value = ''
  }, ACTION_CONFIRM_DELAY_MS)
}

function backToMenu(): void {
  currentPage.value = 'menu'
  activeAction.value = 'standby'
  settingsNotice.value = ''
  emitToUnreal('SettingsClosed', { language: language.value })
}

function selectTab(tab: SettingsTab): void {
  activeSettingsTab.value = tab
  settingsNotice.value = ''
}

function updateNumber(key: NumericSettingKey, event: Event): void {
  settings[key] = Number((event.target as HTMLInputElement).value)
}

function updateSelect(key: 'difficulty' | 'windowMode' | 'resolution', event: Event): void {
  settings[key] = Number((event.target as HTMLSelectElement).value)
}

function applySettings(): void {
  settingsNotice.value = copy.value.settingsPage.applied
  emitToUnreal('SettingsApplied', { ...settings, language: language.value })
}

function resetSettings(): void {
  Object.assign(settings, defaultSettings)
  settingsNotice.value = copy.value.settingsPage.resetDone
  emitToUnreal('SettingsReset', { ...settings, language: language.value })
}

onMounted(() => {
  document.querySelector('#app')?.style.removeProperty('visibility')
  window.addEventListener(ENTER_EVENT_NAME, handleEnterEvent)
  window.addEventListener(BEFORE_ENTER_EVENT_NAME, handleBeforeEnterEvent)
  window.addEventListener(AFTER_LEAVE_EVENT_NAME, handleAfterLeaveEvent)
})

onUnmounted(() => {
  window.removeEventListener(ENTER_EVENT_NAME, handleEnterEvent)
  window.removeEventListener(BEFORE_ENTER_EVENT_NAME, handleBeforeEnterEvent)
  window.removeEventListener(AFTER_LEAVE_EVENT_NAME, handleAfterLeaveEvent)
  clearTimer(actionTimer)
  clearTimer(transitionTimer)
})
</script>

<template>
  <main id="start-screen" class="start-screen" :class="screenTransitionClass">
    <div class="scan-line"></div>
    <div class="top-rule"></div>
    <div class="edge edge-left"></div>
    <div class="edge edge-right"></div>

    <header class="topbar">
      <div class="brand-lockup">
        <span class="brand-mark"><i></i><i></i><i></i></span>
        <div>
          <strong>{{ copy.brandName }}</strong>
          <small>{{ copy.brandSubtitle }}</small>
        </div>
      </div>
      <div class="network-state">
        <span class="pulse"></span>
        <div>
          <strong>{{ copy.networkStatus }}</strong>
          <small>{{ copy.networkDetail }}</small>
        </div>
        <button
          class="language-switch"
          type="button"
          :title="copy.languageSwitchHint"
          :aria-label="copy.languageSwitchHint"
          @click="toggleLanguage"
        >{{ copy.languageSwitchLabel }}</button>
      </div>
    </header>

    <section v-if="currentPage === 'menu'" class="hero page-view">
      <div class="identity-panel">
        <div class="chapter-row">
          <span>{{ copy.chapter }}</span>
          <i></i>
          <span>{{ copy.era }}</span>
        </div>
        <p class="kicker">{{ copy.kicker }}</p>
        <h1>
          <span class="title-silver">{{ copy.titleTop }}</span>
          <span class="title-choir">{{ copy.titleBottom }}</span>
        </h1>
        <div class="cn-title">{{ copy.titleCaption }}</div>
        <p class="tagline">{{ copy.tagline }}</p>

        <div class="mission-data">
          <div v-for="item in copy.missionData" :key="item.label">
            <small>{{ item.label }}</small><strong>{{ item.value }}</strong>
          </div>
        </div>
      </div>

      <nav class="menu-panel" :aria-label="copy.menuAriaLabel">
        <div class="menu-heading">
          <span>{{ copy.menuHeading }}</span>
          <small>{{ copy.menuInstruction }}</small>
        </div>
        <MenuButton
          v-for="item in menuItems"
          :key="item.action"
          v-bind="item"
          :activated="pendingAction === item.action"
          @activate="activate"
        />
      </nav>
    </section>

    <section v-else class="settings-view page-view">
      <header class="settings-header">
        <button class="back-button" type="button" @click="backToMenu">
          <span>‹</span>{{ copy.settingsPage.back }}
        </button>
        <div class="settings-title">
          <small>{{ copy.settingsPage.eyebrow }}</small>
          <h2>{{ copy.settingsPage.title }}</h2>
          <p>{{ copy.settingsPage.description }}</p>
        </div>
        <div class="settings-code">{{ copy.settingsPage.headerCode }}</div>
      </header>

      <div class="settings-shell">
        <nav class="settings-tabs" :aria-label="copy.settingsPage.title">
          <button
            v-for="tab in settingsTabs"
            :key="tab.id"
            class="settings-tab"
            :class="{ active: activeSettingsTab === tab.id }"
            type="button"
            @click="selectTab(tab.id)"
          >
            <span>{{ tab.index }}</span>
            <strong>{{ tab.label }}</strong>
          </button>
        </nav>

        <section class="settings-card">
          <div class="card-heading">
            <div>
              <small>{{ copy.settingsPage.cardCode }}</small>
              <h3>{{ currentSettingsTitle }}</h3>
            </div>
            <span>{{ currentSettingsTabLabel }}</span>
          </div>

          <div v-if="activeSettingsTab === 'general'" class="settings-page">
            <div class="setting-row">
              <div class="setting-copy">
                <strong>{{ copy.settingsPage.general.language }}</strong>
                <small>{{ copy.settingsPage.general.languageHint }}</small>
              </div>
              <div class="segmented-control">
                <button
                  v-for="option in copy.settingsPage.general.languageOptions"
                  :key="option.value"
                  type="button"
                  :class="{ active: language === option.value }"
                  @click="setLanguageFromOption(option.value)"
                >{{ option.label }}</button>
              </div>
            </div>
            <div class="setting-row">
              <div class="setting-copy">
                <strong>{{ copy.settingsPage.general.difficulty }}</strong>
                <small>{{ copy.settingsPage.general.difficultyHint }}</small>
              </div>
              <select :value="settings.difficulty" @change="updateSelect('difficulty', $event)">
                <option v-for="(option, index) in copy.settingsPage.general.difficultyOptions" :key="option" :value="index">{{ option }}</option>
              </select>
            </div>
            <div class="setting-row">
              <div class="setting-copy">
                <strong>{{ copy.settingsPage.general.subtitles }}</strong>
                <small>{{ copy.settingsPage.general.subtitlesHint }}</small>
              </div>
              <button class="toggle-control" :class="{ active: settings.subtitles }" type="button" @click="settings.subtitles = !settings.subtitles">
                {{ settings.subtitles ? copy.settingsPage.enabled : copy.settingsPage.disabled }}
              </button>
            </div>
            <div class="setting-row">
              <div class="setting-copy">
                <strong>{{ copy.settingsPage.general.tutorials }}</strong>
                <small>{{ copy.settingsPage.general.tutorialsHint }}</small>
              </div>
              <button class="toggle-control" :class="{ active: settings.tutorials }" type="button" @click="settings.tutorials = !settings.tutorials">
                {{ settings.tutorials ? copy.settingsPage.enabled : copy.settingsPage.disabled }}
              </button>
            </div>
          </div>

          <div v-else-if="activeSettingsTab === 'display'" class="settings-page">
            <div class="setting-row">
              <div class="setting-copy"><strong>{{ copy.settingsPage.display.windowMode }}</strong><small>{{ copy.settingsPage.display.windowModeHint }}</small></div>
              <select :value="settings.windowMode" @change="updateSelect('windowMode', $event)">
                <option v-for="(option, index) in copy.settingsPage.display.windowModeOptions" :key="option" :value="index">{{ option }}</option>
              </select>
            </div>
            <div class="setting-row">
              <div class="setting-copy"><strong>{{ copy.settingsPage.display.resolution }}</strong><small>{{ copy.settingsPage.display.resolutionHint }}</small></div>
              <select :value="settings.resolution" @change="updateSelect('resolution', $event)">
                <option v-for="(option, index) in copy.settingsPage.display.resolutionOptions" :key="option" :value="index">{{ option }}</option>
              </select>
            </div>
            <div class="setting-row">
              <div class="setting-copy"><strong>{{ copy.settingsPage.display.brightness }}</strong><small>{{ copy.settingsPage.display.brightnessHint }}</small></div>
              <div class="range-control"><input type="range" min="0" max="100" :value="settings.brightness" @input="updateNumber('brightness', $event)" /><span>{{ settings.brightness }}%</span></div>
            </div>
            <div class="setting-row">
              <div class="setting-copy"><strong>{{ copy.settingsPage.display.vsync }}</strong><small>{{ copy.settingsPage.display.vsyncHint }}</small></div>
              <button class="toggle-control" :class="{ active: settings.vsync }" type="button" @click="settings.vsync = !settings.vsync">{{ settings.vsync ? copy.settingsPage.enabled : copy.settingsPage.disabled }}</button>
            </div>
          </div>

          <div v-else-if="activeSettingsTab === 'audio'" class="settings-page">
            <div v-for="item in audioSettings" :key="item.key" class="setting-row">
              <div class="setting-copy"><strong>{{ item.label }}</strong><small>{{ item.hint }}</small></div>
              <div class="range-control"><input type="range" min="0" max="100" :value="settings[item.key]" @input="updateNumber(item.key, $event)" /><span>{{ settings[item.key] }}%</span></div>
            </div>
          </div>

          <div v-else class="settings-page">
            <div class="setting-row">
              <div class="setting-copy"><strong>{{ copy.settingsPage.controls.sensitivity }}</strong><small>{{ copy.settingsPage.controls.sensitivityHint }}</small></div>
              <div class="range-control"><input type="range" min="0" max="100" :value="settings.sensitivity" @input="updateNumber('sensitivity', $event)" /><span>{{ settings.sensitivity }}%</span></div>
            </div>
            <div class="setting-row">
              <div class="setting-copy"><strong>{{ copy.settingsPage.controls.invertY }}</strong><small>{{ copy.settingsPage.controls.invertYHint }}</small></div>
              <button class="toggle-control" :class="{ active: settings.invertY }" type="button" @click="settings.invertY = !settings.invertY">{{ settings.invertY ? copy.settingsPage.enabled : copy.settingsPage.disabled }}</button>
            </div>
            <div class="setting-row">
              <div class="setting-copy"><strong>{{ copy.settingsPage.controls.edgeScroll }}</strong><small>{{ copy.settingsPage.controls.edgeScrollHint }}</small></div>
              <button class="toggle-control" :class="{ active: settings.edgeScroll }" type="button" @click="settings.edgeScroll = !settings.edgeScroll">{{ settings.edgeScroll ? copy.settingsPage.enabled : copy.settingsPage.disabled }}</button>
            </div>
            <div class="setting-row">
              <div class="setting-copy"><strong>{{ copy.settingsPage.controls.cameraShake }}</strong><small>{{ copy.settingsPage.controls.cameraShakeHint }}</small></div>
              <button class="toggle-control" :class="{ active: settings.cameraShake }" type="button" @click="settings.cameraShake = !settings.cameraShake">{{ settings.cameraShake ? copy.settingsPage.enabled : copy.settingsPage.disabled }}</button>
            </div>
          </div>

          <footer class="settings-actions">
            <span>{{ settingsNotice }}</span>
            <button class="reset-button" type="button" @click="resetSettings">{{ copy.settingsPage.reset }}</button>
            <button class="apply-button" type="button" @click="applySettings">{{ copy.settingsPage.apply }}</button>
          </footer>
        </section>
      </div>
    </section>

    <div v-if="currentPage === 'menu'" class="signal-stage" aria-hidden="true">
      <div class="signal-core">
        <div class="orbit orbit-outer"><span></span></div>
        <div class="orbit orbit-mid"><span></span></div>
        <div class="orbit orbit-inner">
          <strong>{{ copy.signalCore }}</strong>
          <small>{{ copy.signalStatus }}</small>
        </div>
        <div class="axis axis-h"></div>
        <div class="axis axis-v"></div>
      </div>
    </div>

    <footer class="footerbar">
      <div class="footer-status">
        <span class="status-block"></span>
        <span>{{ systemMessage }}</span>
      </div>
      <div class="footer-meta">
        <span>{{ copy.build }}</span>
        <span>{{ copy.secureChannel }}</span>
        <span>{{ copy.copyright }}</span>
      </div>
    </footer>
    <div class="transition-veil" aria-hidden="true"></div>
  </main>
</template>

<style>
@keyframes scan {
  0% { top: 6%; opacity: 0; }
  12% { opacity: 0.32; }
  88% { opacity: 0.18; }
  100% { top: 94%; opacity: 0; }
}

@keyframes pulse {
  0% { transform: scale(0.72); opacity: 0.35; }
  100% { transform: scale(1); opacity: 1; }
}

@keyframes orbit {
  0% { transform: rotate(0deg); }
  100% { transform: rotate(360deg); }
}

@keyframes reveal {
  0% { transform: translateY(16px); opacity: 0; }
  100% { transform: translateY(0); opacity: 1; }
}

@keyframes button-confirm {
  0% { opacity: 0.72; }
  55% { opacity: 1; }
  100% { opacity: 0.92; }
}

@keyframes screen-enter-a {
  0% { transform: translateY(10px) scale(1.018); opacity: 0; }
  42% { opacity: 0.72; }
  100% { transform: translateY(0) scale(1); opacity: 1; }
}

@keyframes screen-enter-b {
  0% { transform: translateY(10px) scale(1.018); opacity: 0; }
  42% { opacity: 0.72; }
  100% { transform: translateY(0) scale(1); opacity: 1; }
}

@keyframes screen-exit {
  0% { transform: translateY(0) scale(1); opacity: 1; }
  32% { transform: translateY(0) scale(1.004); opacity: 1; }
  100% { transform: translateY(0) scale(1); opacity: 1; }
}

@keyframes chrome-enter {
  0% { transform: translateY(-18px); opacity: 0; }
  100% { transform: translateY(0); opacity: 1; }
}

@keyframes content-exit {
  0% { opacity: 1; }
  100% { opacity: 0; }
}

@keyframes topbar-exit {
  0% { transform: translateY(0); opacity: 1; }
  100% { transform: translateY(-20px); opacity: 0; }
}

@keyframes footer-exit {
  0% { transform: translateY(0); opacity: 1; }
  100% { transform: translateY(18px); opacity: 0; }
}

@keyframes veil-open {
  0% { transform: scaleX(1); opacity: 0.78; }
  100% { transform: scaleX(0.04); opacity: 0; }
}

@keyframes veil-close {
  0% { transform: scaleX(0.04); opacity: 0; }
  65% { opacity: 0.58; }
  100% { transform: scaleX(1); opacity: 1; }
}

* { box-sizing: border-box; }
html, body, #app { width: 100%; height: 100%; margin: 0; }
body { overflow: hidden; background-color: #020407; color: #dce9f5; font-family: Roboto, Arial, sans-serif; }
button { align-items: center; justify-content: center; text-align: center; font-family: Roboto, Arial, sans-serif; }

.start-screen {
  position: relative;
  width: 100%;
  height: 100%;
  min-width: 960px;
  min-height: 540px;
  overflow: hidden;
  background-color: #020407;
  border: 1px solid #102b43;
}

.start-screen.screen-entering-a { animation: screen-enter-a 0.62s ease-out; }
.start-screen.screen-entering-b { animation: screen-enter-b 0.62s ease-out; }
.start-screen.screen-before-enter {
  pointer-events: none;
  visibility: hidden;
  transform: translateY(10px) scale(1.018);
  opacity: 0;
}
.start-screen.screen-exiting {
  pointer-events: none;
  animation: screen-exit 0.52s ease-in forwards;
}
.start-screen.screen-after-leave {
  pointer-events: none;
  visibility: visible;
  transform: translateY(0) scale(1);
  opacity: 1;
  background-color: #020407;
  border-color: #102b43;
}
.screen-before-enter .topbar { transform: translateY(-18px); opacity: 0; }
.screen-before-enter .hero, .screen-before-enter .settings-view { transform: translateY(16px); opacity: 0; }
.screen-before-enter .footerbar { transform: translateY(18px); opacity: 0; }
.screen-before-enter .transition-veil { transform: scaleX(1); opacity: 0.78; }
.screen-entering-a .topbar, .screen-entering-b .topbar { animation: chrome-enter 0.48s ease-out; }
.screen-entering-a .transition-veil, .screen-entering-b .transition-veil { animation: veil-open 0.44s ease-out forwards; }
.screen-exiting .hero, .screen-exiting .settings-view { animation: content-exit 0.46s ease-in forwards; }
.screen-exiting .topbar { animation: topbar-exit 0.4s ease-in forwards; }
.screen-exiting .footerbar { animation: footer-exit 0.4s ease-in forwards; }
.screen-exiting .transition-veil { animation: veil-close 0.52s ease-in forwards; }
.screen-exiting .signal-stage, .screen-after-leave .signal-stage { z-index: 41; }
.screen-after-leave .scan-line, .screen-after-leave .top-rule,
.screen-after-leave .edge { visibility: hidden; }
.screen-after-leave .hero, .screen-after-leave .settings-view { transform: translateX(0); opacity: 0; }
.screen-after-leave .topbar { transform: translateY(-20px); opacity: 0; }
.screen-after-leave .footerbar { transform: translateY(18px); opacity: 0; }
.screen-after-leave .transition-veil { transform: scaleX(1); opacity: 0.68; }

.transition-veil {
  position: absolute;
  z-index: 40;
  left: 0;
  top: 0;
  right: 0;
  bottom: 0;
  pointer-events: none;
  background-color: #020407;
  opacity: 0;
}

.scan-line {
  position: absolute;
  z-index: 20;
  left: 4%;
  top: 6%;
  width: 92%;
  height: 1px;
  background-color: #258bd0;
  opacity: 0;
  pointer-events: none;
  animation: scan 7s linear infinite;
}

.top-rule { position: absolute; left: 0; top: 76px; width: 100%; height: 1px; background-color: #102b43; }
.edge { position: absolute; top: 76px; bottom: 52px; width: 1px; background-color: #0a2032; }
.edge-left { left: 28px; }
.edge-right { right: 28px; }

.topbar {
  position: relative;
  z-index: 2;
  display: flex;
  align-items: center;
  justify-content: space-between;
  height: 76px;
  padding: 0 48px;
  background-color: #03070b;
}

.brand-lockup, .network-state { display: flex; align-items: center; }
.brand-lockup strong, .network-state strong { display: block; color: #dce9f5; font-size: 12px; letter-spacing: 2px; }
.brand-lockup small, .network-state small { display: block; margin-top: 5px; color: #48667f; font-size: 8px; letter-spacing: 1.5px; }
.brand-mark { display: flex; align-items: flex-end; width: 38px; height: 32px; margin-right: 14px; padding: 5px; border: 1px solid #174a70; }
.brand-mark i { display: block; width: 4px; margin-right: 4px; background-color: #277fba; }
.brand-mark i:nth-child(1) { height: 9px; }
.brand-mark i:nth-child(2) { height: 19px; background-color: #9fc6df; }
.brand-mark i:nth-child(3) { height: 14px; }
.network-state { text-align: right; }
.pulse { width: 9px; height: 9px; margin-right: 12px; border: 1px solid #398fd0; background-color: #164e76; border-radius: 50%; animation: pulse 0.9s ease-in-out infinite alternate; }
.language-switch {
  min-width: 48px;
  height: 30px;
  margin-left: 18px;
  padding: 0 10px;
  border: 1px solid #245a7d;
  background-color: #07131d;
  color: #6fa7ca;
  font-size: 9px;
  letter-spacing: 1px;
  transition: transform 0.16s, border-color 0.16s, background-color 0.16s, color 0.16s;
}
.language-switch:hover, .language-switch:focus {
  transform: translateY(-2px);
  border-color: #479fd2;
  background-color: #0d2434;
  color: #e0f3ff;
}
.language-switch:active { transform: translateY(1px); }

.hero {
  position: absolute;
  left: 0;
  top: 77px;
  right: 0;
  bottom: 53px;
  display: flex;
  align-items: center;
  padding: 4vh 7vw;
}
.page-view { animation: reveal 0.32s ease-out; }

.settings-view {
  position: absolute;
  left: 0;
  top: 77px;
  right: 0;
  bottom: 53px;
  display: flex;
  flex-direction: column;
  padding: 28px 6vw 24px;
  background-color: #03070b;
}
.settings-header { display: flex; align-items: center; min-height: 92px; padding-bottom: 20px; border-bottom: 1px solid #17364e; }
.back-button {
  order: 3;
  display: flex;
  align-items: center;
  justify-content: flex-start;
  width: 190px;
  height: 42px;
  margin-left: 34px;
  padding: 0 15px;
  border: 1px solid #214d6d;
  background-color: #07131d;
  color: #7c9db4;
  font-size: 10px;
  letter-spacing: 1.5px;
  transition: transform 0.15s, border-color 0.15s, color 0.15s, background-color 0.15s;
}
.back-button span { margin-right: 13px; color: #4b9acd; font-size: 25px; }
.back-button:hover, .back-button:focus { transform: translateX(5px); border-color: #4098cc; background-color: #0c2030; color: #e0eff8; }
.settings-title { order: 1; flex: 1; }
.settings-title small { color: #347aa6; font-size: 8px; letter-spacing: 2.5px; }
.settings-title h2 { margin: 6px 0 4px; color: #d8e9f4; font-size: 25px; font-weight: 500; letter-spacing: 4px; }
.settings-title p { margin: 0; color: #557083; font-size: 10px; letter-spacing: 0.7px; }
.settings-code { order: 2; color: #2d5873; font-size: 9px; letter-spacing: 2px; }

.settings-shell { display: flex; flex: 1; min-height: 0; padding-top: 20px; }
.settings-tabs { order: 2; display: flex; flex-direction: column; width: 210px; margin-left: 18px; padding-top: 8px; }
.settings-tab {
  display: flex;
  align-items: center;
  justify-content: flex-start;
  width: 100%;
  height: 54px;
  margin-bottom: 7px;
  padding: 0 15px;
  border: 1px solid #102b40;
  background-color: #050d14;
  color: #537086;
  text-align: left;
  transition: transform 0.15s, border-color 0.15s, background-color 0.15s, color 0.15s;
}
.settings-tab span { width: 36px; color: #285472; font-size: 8px; }
.settings-tab strong { font-size: 11px; font-weight: 500; letter-spacing: 2px; }
.settings-tab:hover, .settings-tab:focus { transform: translateX(-5px); border-color: #2c739c; color: #b8d6e8; }
.settings-tab.active { transform: translateX(-8px); border-color: #398cb9; background-color: #0c2232; color: #d9effb; }
.settings-tab.active span { color: #52a9d8; }

.settings-card { order: 1; display: flex; flex: 1; flex-direction: column; min-width: 0; padding: 0 24px; border: 1px solid #173a54; background-color: #071019; }
.card-heading { display: flex; align-items: center; justify-content: space-between; min-height: 66px; border-bottom: 1px solid #17374f; }
.card-heading small { color: #316c91; font-size: 7px; letter-spacing: 2px; }
.card-heading h3 { margin: 5px 0 0; color: #c7dce9; font-size: 16px; font-weight: 500; letter-spacing: 2px; }
.card-heading > span { padding: 6px 10px; border: 1px solid #1c4e6d; color: #3f81a8; font-size: 8px; letter-spacing: 1.5px; }
.settings-page { flex: 1; animation: reveal 0.22s ease-out; }
.setting-row { display: flex; align-items: center; justify-content: space-between; min-height: 68px; border-bottom: 1px solid #112b3e; }
.setting-copy { flex: 1; padding-right: 24px; }
.setting-copy strong { display: block; color: #adc4d3; font-size: 12px; font-weight: 500; letter-spacing: 1px; }
.setting-copy small { display: block; margin-top: 5px; color: #405f73; font-size: 8px; letter-spacing: 0.6px; }
.setting-row select { width: 210px; height: 36px; padding: 0 10px; border: 1px solid #285673; background-color: #07131d; color: #9db8ca; font-size: 10px; }

.toggle-control { width: 82px; height: 34px; border: 1px solid #294a60; background-color: #09131b; color: #526d7e; font-size: 9px; letter-spacing: 1px; transition: border-color 0.15s, background-color 0.15s, color 0.15s; }
.toggle-control:hover, .toggle-control:focus { border-color: #3d83aa; color: #bcd9e8; }
.toggle-control.active { border-color: #398dba; background-color: #0d2b3d; color: #8bd0f1; }
.segmented-control { display: flex; }
.segmented-control button { min-width: 84px; height: 34px; margin-left: 5px; border: 1px solid #294a60; background-color: #09131b; color: #526d7e; font-size: 9px; }
.segmented-control button.active { border-color: #398dba; background-color: #0d2b3d; color: #9cd7f2; }
.range-control { display: flex; align-items: center; width: 260px; }
.range-control input { flex: 1; }
.range-control span { width: 48px; margin-left: 12px; color: #6aaed2; font-size: 10px; text-align: right; }

.settings-actions { display: flex; align-items: center; justify-content: flex-end; min-height: 58px; border-top: 1px solid #17374f; }
.settings-actions > span { flex: 1; color: #4d9ac2; font-size: 9px; letter-spacing: 1px; }
.settings-actions button { height: 34px; margin-left: 9px; padding: 0 16px; font-size: 9px; letter-spacing: 1px; }
.reset-button { border: 1px solid #2c4a5e; background-color: #09131b; color: #607f92; }
.apply-button { border: 1px solid #378ebc; background-color: #12344a; color: #bde6fa; }
.settings-actions button:hover, .settings-actions button:focus { transform: translateY(-2px); border-color: #64b8df; color: #ecf9ff; }

.identity-panel { position: relative; z-index: 3; width: 42%; animation: reveal 0.5s ease-out; }
.chapter-row { display: flex; align-items: center; width: 100%; max-width: 430px; color: #4e7897; font-size: 9px; letter-spacing: 2px; }
.chapter-row i { flex: 1; height: 1px; margin: 0 16px; background-color: #174261; }
.kicker { margin: 24px 0 8px; color: #428ebd; font-size: 11px; letter-spacing: 7px; }
h1 { margin: 0; line-height: 0.78; }
h1 span { display: block; }
.title-silver { color: #dce9f5; font-size: 7vw; font-weight: 300; letter-spacing: 1.2vw; }
.title-choir { color: #1d5c87; font-size: 7vw; font-weight: 800; letter-spacing: 0.65vw; }
.cn-title { margin-top: 24px; color: #7999b0; font-size: 15px; letter-spacing: 13px; }
.tagline { max-width: 460px; margin: 17px 0 0; color: #5f7588; font-size: 12px; letter-spacing: 1px; }

.mission-data { display: flex; margin-top: 38px; padding-top: 15px; border-top: 1px solid #17344b; }
.mission-data div { min-width: 112px; margin-right: 22px; }
.mission-data small { display: block; color: #31536c; font-size: 8px; letter-spacing: 2px; }
.mission-data strong { display: block; margin-top: 7px; color: #7899b1; font-size: 11px; font-weight: 400; letter-spacing: 1px; }

.signal-stage {
  position: absolute;
  z-index: 1;
  top: 77px;
  right: 0;
  bottom: 53px;
  left: 0;
  pointer-events: none;
}
.signal-core {
  position: absolute;
  top: 50%;
  left: 42%;
  width: 25vw;
  height: 25vw;
  min-width: 240px;
  min-height: 240px;
  transform: translateY(-50%);
  opacity: 0.62;
}
.orbit { position: absolute; border: 1px solid #17405d; border-radius: 50%; }
.orbit-outer { left: 0; top: 0; width: 100%; height: 100%; animation: orbit 24s linear infinite; }
.orbit-mid { left: 13%; top: 13%; width: 74%; height: 74%; border-color: #26668f; animation: orbit 15s linear infinite reverse; }
.orbit-inner { left: 31%; top: 31%; display: flex; flex-direction: column; align-items: center; justify-content: center; width: 38%; height: 38%; border-color: #4e9aca; background-color: #06121d; }
.orbit span { position: absolute; left: 48%; top: -4px; width: 8px; height: 8px; background-color: #4c9bd0; border-radius: 50%; }
.orbit-inner strong { color: #75b2d7; font-size: 2.2vw; letter-spacing: 4px; }
.orbit-inner small { margin-top: 5px; color: #397299; font-size: 7px; letter-spacing: 2px; }
.axis { position: absolute; background-color: #12364f; }
.axis-h { left: -8%; top: 50%; width: 116%; height: 1px; }
.axis-v { left: 50%; top: -8%; width: 1px; height: 116%; }

.menu-panel { position: relative; z-index: 4; width: 330px; margin-left: auto; animation: reveal 0.65s ease-out; }
.menu-heading { display: flex; justify-content: space-between; align-items: flex-end; height: 34px; margin-bottom: 9px; border-bottom: 1px solid #1d4d6f; color: #538eb5; }
.menu-heading span { font-size: 10px; letter-spacing: 2px; }
.menu-heading small { padding-bottom: 7px; color: #2e5874; font-size: 7px; letter-spacing: 1.5px; }

.menu-button {
  position: relative;
  display: flex;
  align-items: center;
  justify-content: flex-start;
  width: 100%;
  height: 62px;
  margin: 0 0 7px;
  padding: 0 15px 0 0;
  overflow: hidden;
  border: 1px solid #102f47;
  background-color: #06101a;
  color: #8aa6ba;
  text-align: left;
  transition: transform 0.16s, border-color 0.16s, background-color 0.16s, color 0.16s;
}
.button-rail { width: 3px; height: 100%; margin-right: 15px; background-color: #173e59; transition: width 0.16s, background-color 0.16s; }
.button-index { width: 34px; color: #2c5874; font-size: 9px; letter-spacing: 1px; }
.button-copy { display: block; flex: 1; }
.button-copy strong { display: block; color: #9cb4c5; font-size: 15px; font-weight: 500; letter-spacing: 3px; transition: color 0.16s; }
.button-copy small { display: block; margin-top: 5px; color: #37576c; font-size: 7px; letter-spacing: 1.8px; }
.button-arrow { color: #2b5f80; font-size: 25px; transition: transform 0.16s, color 0.16s; }
.menu-button:hover, .menu-button:focus {
  transform: translateX(-9px);
  border-color: #307da8;
  background-color: #0b1c2a;
  color: #d5e9f6;
}
.menu-button:hover .button-rail, .menu-button:focus .button-rail { width: 8px; background-color: #3b9bd3; }
.menu-button:hover .button-copy strong, .menu-button:focus .button-copy strong { color: #e4f3fc; }
.menu-button:hover .button-arrow, .menu-button:focus .button-arrow { color: #70b9e2; transform: translateX(4px); }
.menu-button:active { transform: translateX(-5px) scale(0.99); background-color: #12324a; }
.menu-button.activated {
  transform: translateX(-10px);
  border-color: #70bde3;
  background-color: #153a52;
  color: #effaff;
  animation: button-confirm 0.24s ease-out;
}
.menu-button.activated .button-rail { width: 12px; background-color: #8dd8f7; }
.menu-button.activated .button-copy strong { color: #ffffff; }
.menu-button.activated .button-copy small { color: #78b9da; }
.menu-button.activated .button-arrow { color: #c8edff; transform: translateX(6px); }
.menu-button.primary { border-color: #286e98; background-color: #0a1c2a; }
.menu-button.primary .button-rail { background-color: #3c97cb; }
.menu-button.danger:hover, .menu-button.danger:focus { border-color: #78404a; background-color: #1c1015; }
.menu-button.danger:hover .button-rail, .menu-button.danger:focus .button-rail { background-color: #a95362; }

.footerbar {
  position: absolute;
  z-index: 5;
  left: 0;
  right: 0;
  bottom: 0;
  display: flex;
  align-items: center;
  justify-content: space-between;
  height: 52px;
  padding: 0 48px;
  border-top: 1px solid #102b43;
  background-color: #03070b;
  color: #365970;
  font-size: 8px;
  letter-spacing: 1.4px;
}
.footer-status, .footer-meta { display: flex; align-items: center; }
.status-block { width: 5px; height: 14px; margin-right: 10px; background-color: #2d83b8; animation: pulse 0.8s ease-in-out infinite alternate; }
.footer-meta span { margin-left: 27px; }

@media (max-width: 1180px) {
  .hero { padding-left: 5vw; padding-right: 5vw; }
  .signal-core { left: 36%; opacity: 0.62; }
  .menu-panel { width: 300px; }
  .title-silver, .title-choir { font-size: 72px; }
}
</style>
