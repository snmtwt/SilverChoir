<script setup lang="ts">
defineProps<{
  label: string
}>()

defineEmits<{
  open: []
}>()
</script>

<template>
  <button
    id="base-menu-open"
    class="menu-button hud-interactive"
    type="button"
    :title="label"
    :aria-label="label"
    @click="$emit('open')"
  >
    <span class="menu-button-icon" aria-hidden="true"><i></i><i></i><i></i></span>
  </button>
</template>
