<script setup lang="ts">
import { computed } from 'vue'
import InventoryGrid from './InventoryGrid.vue'
import type { InventoryGridData, InventoryItemData, InventoryPanelData } from './types'

const props = defineProps<{
  data: Required<InventoryPanelData>
  eyebrow: string
  fallbackTitle: string
  slotWidth: number
  slotHeight: number
}>()

const emit = defineEmits<{ select: [item: InventoryItemData] }>()
const itemCount = computed(() => props.data.items.length)

type ContainerGroup = {
  parentItemId: string
  ownerDisplayName: string
  ownerWeight: number
  grids: InventoryGridData[]
  regions: ContainerRegion[]
  layoutWidth: number
  layoutHeight: number
}

type ContainerRegion = {
  grid: InventoryGridData
  left: number
  top: number
}

function rangesOverlap(startA: number, endA: number, startB: number, endB: number): boolean {
  return Math.min(endA, endB) > Math.max(startA, startB)
}

function arrangeRegions(grids: InventoryGridData[]): {
  regions: ContainerRegion[]
  width: number
  height: number
} {
  const gap = Math.max(4, Math.round(Math.min(props.slotWidth, props.slotHeight) / 6))
  const regions = grids.map(grid => ({
    grid,
    baseLeft: (grid.layoutColumn || 0) * props.slotWidth,
    baseTop: (grid.layoutRow || 0) * props.slotHeight,
    width: Math.max(1, grid.columns) * props.slotWidth,
    height: Math.max(1, grid.rows) * props.slotHeight,
    offsetX: 0,
    offsetY: 0
  }))

  const horizontalOrder = [...regions].sort((a, b) => a.baseLeft - b.baseLeft)
  for (const target of horizontalOrder) {
    for (const source of horizontalOrder) {
      if (source === target || source.baseLeft + source.width > target.baseLeft) continue
      if (!rangesOverlap(
        source.baseTop,
        source.baseTop + source.height,
        target.baseTop,
        target.baseTop + target.height
      )) continue
      target.offsetX = Math.max(target.offsetX, source.offsetX + gap)
    }
  }

  const verticalOrder = [...regions].sort((a, b) => a.baseTop - b.baseTop)
  for (const target of verticalOrder) {
    for (const source of verticalOrder) {
      if (source === target || source.baseTop + source.height > target.baseTop) continue
      if (!rangesOverlap(
        source.baseLeft,
        source.baseLeft + source.width,
        target.baseLeft,
        target.baseLeft + target.width
      )) continue
      target.offsetY = Math.max(target.offsetY, source.offsetY + gap)
    }
  }

  const positioned = regions.map(region => ({
    grid: region.grid,
    left: region.baseLeft + region.offsetX,
    top: region.baseTop + region.offsetY
  }))

  return {
    regions: positioned,
    width: Math.max(...regions.map(region => region.baseLeft + region.offsetX + region.width)),
    height: Math.max(...regions.map(region => region.baseTop + region.offsetY + region.height))
  }
}

const containerGroups = computed<ContainerGroup[]>(() => {
  const groups = new Map<string, Omit<ContainerGroup, 'regions' | 'layoutWidth' | 'layoutHeight'>>()
  for (const grid of props.data.containers) {
    const parentItemId = grid.parentItemId || ''
    let group = groups.get(parentItemId)
    if (!group) {
      group = {
        parentItemId,
        ownerDisplayName: grid.ownerDisplayName || '',
        ownerWeight: Number.isFinite(grid.ownerWeight) ? Number(grid.ownerWeight) : 0,
        grids: []
      }
      groups.set(parentItemId, group)
    }
    group.grids.push(grid)
  }
  return [...groups.values()].map(group => {
    const layout = arrangeRegions(group.grids)
    return {
      ...group,
      regions: layout.regions,
      layoutWidth: layout.width,
      layoutHeight: layout.height
    }
  })
})

function itemsFor(grid: InventoryGridData): InventoryItemData[] {
  const parentItemId = grid.parentItemId || ''
  return props.data.items.filter(item =>
    item.containerIndex === grid.containerIndex &&
    (item.parentItemId || '') === parentItemId
  )
}

function groupStyle(group: ContainerGroup) {
  return {
    width: `${group.layoutWidth}px`,
    height: `${group.layoutHeight}px`
  }
}
</script>

<template>
  <section
    class="inventory-pane hud-interactive"
    data-sis-drop-zone="inventory"
    :data-inventory-id="data.inventoryId || undefined"
    :data-inventory-component-id="data.inventoryComponentId || data.inventoryId || undefined"
    :data-presentation-slot="data.presentationSlot || undefined"
    :data-inventory-view-id="data.inventoryViewId || undefined"
    :data-region-role="data.regionRole || undefined"
  >
    <header class="inventory-pane-heading">
      <div class="inventory-pane-title">
        <small>{{ eyebrow }}</small>
        <h3>{{ data.inventoryName || fallbackTitle }}</h3>
        <p>{{ data.inventoryId }}</p>
      </div>
      <div class="inventory-pane-count">
        <small>ITEMS</small>
        <strong>{{ itemCount }}</strong>
      </div>
    </header>

    <div class="inventory-pane-metrics">
      <span><small>总重量</small><strong>{{ data.totalWeight.toFixed(1) }}</strong></span>
      <span><small>总价值</small><strong>{{ Math.round(data.totalPrice) }}</strong></span>
      <span><small>版本</small><strong>{{ String(data.revision).padStart(3, '0') }}</strong></span>
    </div>

    <div v-if="data.containers.length" class="inventory-containers">
      <section
        v-for="group in containerGroups"
        :key="group.parentItemId || 'top-level'"
        class="inventory-container-group"
      >
        <header v-if="group.ownerDisplayName" class="inventory-container-owner">
          <strong>{{ group.ownerDisplayName }}</strong>
          <span>{{ group.ownerWeight.toFixed(1) }} KG</span>
        </header>
        <div class="inventory-container-layout" :style="groupStyle(group)">
          <InventoryGrid
            v-for="region in group.regions"
            :key="`${group.parentItemId}:${region.grid.containerIndex}`"
            :grid="region.grid"
            :items="itemsFor(region.grid)"
            :slot-width="slotWidth"
            :slot-height="slotHeight"
            :position-left="region.left"
            :position-top="region.top"
            positioned
            @select="emit('select', $event)"
          />
        </div>
      </section>
    </div>
    <div v-else class="inventory-empty">该库存没有配置格子</div>

    <footer class="inventory-pane-footer">
      <span class="inventory-status-dot"></span>
      <strong>LINK ONLINE</strong>
    </footer>
  </section>
</template>
