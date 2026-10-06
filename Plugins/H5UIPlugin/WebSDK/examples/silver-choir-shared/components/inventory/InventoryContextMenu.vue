<script setup lang="ts">
import type { ItemMenuEntryData } from './types'

defineProps<{
  entries: ItemMenuEntryData[]
  x: number
  y: number
}>()

const emit = defineEmits<{
  choose: [entry: ItemMenuEntryData]
}>()
</script>

<template>
  <div
    class="inventory-context-menu hud-interactive"
    :style="{ left: `${x}px`, top: `${y}px` }"
    role="menu"
  >
    <button
      v-for="entry in entries"
      :key="entry.functionName"
      type="button"
      role="menuitem"
      :disabled="entry.enabled === false"
      @mousedown.stop
      @click.stop="emit('choose', entry)"
    >
      {{ entry.menuText }}
    </button>
  </div>
</template>

<style scoped>
.inventory-context-menu {
  position: fixed;
  z-index: 12000;
  display: flex;
  flex-direction: column;
  width: 176px;
  padding: 5px;
  border: 1px solid rgba(71, 183, 231, 0.9);
  background: rgba(3, 17, 27, 0.98);
  box-shadow: 0 0 18px rgba(16, 146, 201, 0.28);
}

.inventory-context-menu button {
  min-height: 34px;
  padding: 7px 12px;
  color: #cbefff;
  border: 0;
  border-bottom: 1px solid rgba(47, 122, 156, 0.42);
  background: transparent;
  text-align: left;
  font-size: 12px;
}

.inventory-context-menu button:last-child {
  border-bottom: 0;
}

.inventory-context-menu button:hover,
.inventory-context-menu button:focus {
  color: #ffffff;
  background: rgba(32, 139, 185, 0.36);
}

.inventory-context-menu button:disabled {
  color: rgba(139, 177, 193, 0.45);
  background: transparent;
}
</style>
