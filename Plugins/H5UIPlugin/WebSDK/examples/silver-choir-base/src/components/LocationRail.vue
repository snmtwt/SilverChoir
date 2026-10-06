<script setup lang="ts">
import type { LocationId, LocationOption } from '../types'

const props = defineProps<{
  locations: LocationOption[]
  activeLocation: LocationId
  currentLabel: string
}>()

const emit = defineEmits<{
  select: [location: LocationId]
}>()

function selectLocation(location: LocationId): void {
  if (location !== props.activeLocation) emit('select', location)
}
</script>

<template>
  <section class="location-rail hud-interactive" :aria-label="currentLabel">
    <button
      v-for="location in locations"
      :id="'location-' + location.id"
      :key="location.id"
      class="location-button"
      :class="{ active: activeLocation === location.id }"
      type="button"
      @click="selectLocation(location.id)"
    >
      <span>{{ location.code }}</span>
      <strong>{{ location.label }}</strong>
      <i aria-hidden="true"></i>
    </button>
  </section>
</template>
