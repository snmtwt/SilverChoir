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
  <section class="camera-strip hud-panel hud-interactive">
    <header>
      <span>{{ scene === 'haven' ? 'HVN' : 'UGB' }}</span>
      <small>{{ heading }}</small>
      <strong>{{ hint }}</strong>
    </header>
    <div class="camera-options">
      <button
        v-for="location in locations"
        :id="'location-' + location.id"
        :key="location.id"
        class="camera-node"
        :class="{ active: activeLocation === location.id }"
        type="button"
        @click="$emit('select', location.id)"
      >
        <span>{{ location.code }}</span>
        <strong>{{ location.label }}</strong>
        <i></i>
      </button>
    </div>
  </section>
</template>
