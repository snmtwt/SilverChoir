<script setup lang="ts">
defineProps<{
  label: string
  value: string
  active?: boolean
}>()
</script>

<template>
  <article class="status-card" :class="{ active }">
    <span class="status-label">{{ label }}</span>
    <strong>{{ value }}</strong>
  </article>
</template>
