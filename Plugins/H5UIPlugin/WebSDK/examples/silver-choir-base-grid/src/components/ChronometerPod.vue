<script setup lang="ts">
import type { CalendarTime } from '../types'

defineProps<{
  time: CalendarTime
  paused: boolean
  speed: number
  monthLabel: string
  dayLabel: string
  heading: string
  pausedLabel: string
  runningLabel: string
  pauseHint: string
  fastHint: string
}>()

defineEmits<{
  togglePause: []
  cycleSpeed: []
}>()

function pad(value: number): string {
  return String(Math.max(0, Math.floor(value))).padStart(2, '0')
}
</script>

<template>
  <section class="chronometer hud-panel hud-interactive" :class="{ paused, accelerated: speed > 1 && !paused }">
    <header>
      <i></i>
      <div><small>{{ heading }}</small><strong id="time-state-label">{{ paused ? pausedLabel : runningLabel }}</strong></div>
    </header>
    <div class="chrono-readout">
      <div class="chrono-date">
        <span><strong>{{ pad(time.month) }}</strong><small>{{ monthLabel }}</small></span>
        <i></i>
        <span><strong>{{ pad(time.day) }}</strong><small>{{ dayLabel }}</small></span>
      </div>
      <strong id="strategic-clock" :key="time.hour + '-' + time.minute" class="chrono-digits">{{ pad(time.hour) }}:{{ pad(time.minute) }}</strong>
    </div>
    <div class="chrono-actions">
      <button id="time-play" type="button" :title="pauseHint" @click="$emit('togglePause')">
        <span v-if="paused" class="chrono-play"></span>
        <span v-else class="chrono-pause"><i></i><i></i></span>
      </button>
      <button id="time-fast" type="button" :title="fastHint" @click="$emit('cycleSpeed')">
        <span class="chrono-fast">»</span><strong id="time-speed-label" :key="speed">×{{ speed > 1 ? speed : 5 }}</strong>
      </button>
    </div>
  </section>
</template>
