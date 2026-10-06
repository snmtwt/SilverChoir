<script setup lang="ts">
import type { LocationId, LocationOption, SceneId } from '../types'

defineProps<{
  scene: SceneId
  locations: LocationOption[]
  activeLocation: LocationId
  heading: string
  hint: string
}>()

defineEmits<{
  select: [location: LocationId]
}>()
</script>

<template>
  <section class="camera-spine hud-interactive">
    <header>
      <div><i></i><span>{{ scene === 'haven' ? 'HVN' : 'UGB' }}</span></div>
      <small>{{ heading }}</small>
      <strong>{{ hint }}</strong>
    </header>
    <div class="spine-track"></div>
    <div class="spine-options">
      <button
        v-for="location in locations"
        :id="'location-' + location.id"
        :key="location.id"
        class="spine-node"
        :class="{ active: activeLocation === location.id }"
        type="button"
        @click="$emit('select', location.id)"
      >
        <i></i>
        <span>{{ location.code }}</span>
        <strong>{{ location.label }}</strong>
        <b></b>
      </button>
    </div>
  </section>
</template>
