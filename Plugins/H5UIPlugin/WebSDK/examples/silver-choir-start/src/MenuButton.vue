<script setup lang="ts">
defineProps<{
  index: string
  label: string
  sublabel: string
  action: string
  primary?: boolean
  danger?: boolean
  activated?: boolean
}>()

defineEmits<{
  activate: [action: string]
}>()
</script>

<template>
  <button
    :id="'menu-' + action"
    class="menu-button"
    :class="{ primary, danger, activated }"
    type="button"
    :data-action="action"
    :aria-busy="activated ? 'true' : 'false'"
    @click="$emit('activate', action)"
  >
    <span class="button-rail"></span>
    <span class="button-index">{{ index }}</span>
    <span class="button-copy">
      <strong>{{ label }}</strong>
      <small>{{ sublabel }}</small>
    </span>
    <span class="button-arrow">›</span>
  </button>
</template>
