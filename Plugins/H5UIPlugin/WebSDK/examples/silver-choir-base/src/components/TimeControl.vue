<script setup lang="ts">
import type { CalendarTime } from '../types'

defineProps<{
  time: CalendarTime
  paused: boolean
  speed: number
  monthLabel: string
  dayLabel: string
  timeLabel: string
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
  <section class="time-control hud-panel hud-interactive" :class="{ paused, accelerated: speed > 1 && !paused }">
    <div class="time-summary">
      <div class="time-state-line">
        <span class="time-pulse"></span>
        <small>{{ timeLabel }}</small>
        <strong id="time-state-label">{{ paused ? pausedLabel : runningLabel }}</strong>
      </div>
      <div class="compact-calendar">
        <span><strong>{{ pad(time.month) }}</strong><small>{{ monthLabel }}</small></span>
        <i></i>
        <span><strong>{{ pad(time.day) }}</strong><small>{{ dayLabel }}</small></span>
        <b id="strategic-clock" :key="time.hour + '-' + time.minute" class="clock-value">{{ pad(time.hour) }}:{{ pad(time.minute) }}</b>
      </div>
    </div>

    <div class="time-actions">
      <button id="time-play" type="button" :title="pauseHint" @click="$emit('togglePause')">
        <span v-if="paused" class="play-icon"></span>
        <span v-else class="pause-icon"><i></i><i></i></span>
      </button>
      <button id="time-fast" class="fast-button" type="button" :title="fastHint" @click="$emit('cycleSpeed')">
        <span class="fast-arrows">»</span>
        <strong id="time-speed-label" :key="speed">×{{ speed > 1 ? speed : 5 }}</strong>
      </button>
    </div>
  </section>
</template>
