<script setup lang="ts">
import { computed, ref } from '@h5ui-plugin/vue'
import StatusCard from './StatusCard.vue'

const count = ref(0)
const pilotName = ref('H5UI Pilot')
const detailsVisible = ref(true)
const statuses = [
  { label: 'Renderer', value: 'Native Slate', active: true },
  { label: 'Framework', value: 'Vue 3', active: true },
  { label: 'Module', value: 'Custom renderer', active: false }
]

const greeting = computed(() => `Welcome, ${pilotName.value || 'Pilot'}`)

function updatePilot(event: Event): void {
  pilotName.value = String((event.target as HTMLInputElement).value || '')
}
</script>

<template>
  <main class="workspace">
    <header class="topbar">
      <div>
        <span class="eyebrow">H5 UI PLUGIN</span>
        <h1>Vue 3 runtime</h1>
      </div>
      <span id="vue-runtime-status" class="runtime-status">Ready</span>
    </header>

    <section class="status-row">
      <StatusCard
        v-for="status in statuses"
        :key="status.label"
        :label="status.label"
        :value="status.value"
        :active="status.active"
      />
    </section>

    <section class="control-panel">
      <div class="control-copy">
        <span class="section-label">Reactive state</span>
        <h2>{{ greeting }}</h2>
        <p v-if="detailsVisible">This panel is rendered by a Vue single-file component inside Unreal Engine.</p>
        <p v-else>Details are hidden by Vue conditional rendering.</p>
      </div>

      <label for="vue-name">Pilot name</label>
      <input id="vue-name" :value="pilotName" @input="updatePilot" />

      <div class="actions">
        <button id="vue-increment" type="button" @click="count += 1">
          Increment <span id="vue-count">{{ count }}</span>
        </button>
        <button id="vue-toggle" class="secondary" type="button" @click="detailsVisible = !detailsVisible">
          Toggle details
        </button>
      </div>
    </section>
  </main>
</template>

<style>
* { box-sizing: border-box; }
html, body { width: 100%; height: 100%; margin: 0; background-color: transparent; color: #e8edf5; font-family: Roboto; }
body { overflow: auto; }
.workspace { width: 100%; min-height: 100%; padding: 24px; background-color: #10161f; }
.topbar { display: flex; justify-content: space-between; align-items: center; min-height: 72px; padding-bottom: 16px; border-bottom: 1px #344052; }
.eyebrow, .section-label { color: #65d7b7; font-size: 10px; }
h1 { margin: 6px 0 0; font-size: 26px; }
.runtime-status { padding: 7px 12px; border: 1px #65d7b7; color: #65d7b7; font-size: 11px; }
.status-row { display: flex; width: 100%; margin-top: 16px; }
.status-card { flex: 1; min-width: 180px; margin-right: 12px; padding: 14px; border: 1px #344052; background-color: #161f2a; }
.status-card.active { border-color: #65d7b7; }
.status-label { display: block; margin-bottom: 8px; color: #9eabba; font-size: 10px; }
.status-card strong { font-size: 14px; }
.control-panel { width: 100%; max-width: 720px; margin-top: 18px; padding: 18px; border: 1px #344052; background-color: #161f2a; }
.control-copy h2 { margin: 7px 0; font-size: 20px; }
.control-copy p { min-height: 20px; margin: 0 0 18px; color: #aeb9c8; font-size: 12px; }
label { display: block; margin-bottom: 6px; color: #aeb9c8; font-size: 10px; }
input { width: 100%; height: 38px; padding: 0 11px; border: 1px #435168; background-color: #0d131b; color: #e8edf5; }
.actions { display: flex; margin-top: 14px; }
button { min-width: 130px; height: 38px; margin-right: 10px; padding: 0 14px; border: 1px #65d7b7; background-color: #65d7b7; color: #0c1715; }
button.secondary { border-color: #435168; background-color: #151e29; color: #d6deea; }
#vue-count { margin-left: 5px; }
</style>
