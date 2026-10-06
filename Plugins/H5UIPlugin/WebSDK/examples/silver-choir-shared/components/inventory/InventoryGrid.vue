<script setup lang="ts">
import { computed } from 'vue'
import InventoryItem from './InventoryItem.vue'
import type { InventoryGridData, InventoryItemData } from './types'

const props = defineProps<{
  grid: InventoryGridData
  items: InventoryItemData[]
  slotWidth: number
  slotHeight: number
  positioned?: boolean
  positionLeft?: number
  positionTop?: number
}>()
const emit = defineEmits<{ select: [item: InventoryItemData] }>()

const columnCount = computed(() => Math.max(1, props.grid.columns))
const rowCount = computed(() => Math.max(1, props.grid.rows))
const cells = computed(() => Array.from(
  { length: columnCount.value * rowCount.value },
  (_, index) => ({ column: index % columnCount.value, row: Math.floor(index / columnCount.value) })
))
const gridWidth = computed(() => columnCount.value * props.slotWidth)
const gridHeight = computed(() => rowCount.value * props.slotHeight)

// Each configured region is positioned inside one item-level container frame.
const containerStyle = computed(() => ({
  width: `${gridWidth.value}px`,
  height: `${gridHeight.value}px`,
  position: props.positioned ? 'absolute' : undefined,
  left: props.positioned
    ? `${props.positionLeft ?? (props.grid.layoutColumn || 0) * props.slotWidth}px`
    : undefined,
  top: props.positioned
    ? `${props.positionTop ?? (props.grid.layoutRow || 0) * props.slotHeight}px`
    : undefined
}))

const gridStyle = computed(() => ({
  width: `${gridWidth.value}px`,
  height: `${gridHeight.value}px`
}))

function cellStyle(column: number, row: number) {
  return {
    left: `${column * props.slotWidth}px`,
    top: `${row * props.slotHeight}px`,
    width: `${props.slotWidth}px`,
    height: `${props.slotHeight}px`
  }
}
</script>

<template>
  <section class="inventory-container" :style="containerStyle">
    <div
      class="inventory-grid"
      :style="gridStyle"
      :data-container-index="grid.containerIndex"
      :data-parent-item-id="grid.parentItemId || undefined"
      :data-columns="columnCount"
      :data-rows="rowCount"
    >
      <i
        v-for="cell in cells"
        :key="`cell-${cell.column}-${cell.row}`"
        class="inventory-grid-cell"
        :data-column="cell.column"
        :data-row="cell.row"
        :style="cellStyle(cell.column, cell.row)"
      />
      <InventoryItem
        v-for="item in items"
        :key="item.itemId"
        :item="item"
        :slot-width="slotWidth"
        :slot-height="slotHeight"
        @select="emit('select', $event)"
      />
    </div>
  </section>
</template>
