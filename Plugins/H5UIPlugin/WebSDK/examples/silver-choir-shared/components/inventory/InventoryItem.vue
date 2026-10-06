<script setup lang="ts">
import { computed } from 'vue'
import type { InventoryItemData } from './types'

const GAP = 3
const props = defineProps<{ item: InventoryItemData; slotWidth: number; slotHeight: number }>()
const emit = defineEmits<{ select: [item: InventoryItemData] }>()

const itemStyle = computed(() => ({
  left: `${props.item.column * props.slotWidth + GAP}px`,
  top: `${props.item.row * props.slotHeight + GAP}px`,
  width: `${Math.max(1, props.item.columnSpan) * props.slotWidth - GAP * 2}px`,
  height: `${Math.max(1, props.item.rowSpan) * props.slotHeight - GAP * 2}px`
}))

const iconStyle = computed(() => {
  const itemWidth = Math.max(1, props.item.columnSpan) * props.slotWidth - GAP * 2
  const itemHeight = Math.max(1, props.item.rowSpan) * props.slotHeight - GAP * 2
  const maxWidth = Math.max(1, itemWidth * 0.96)
  const maxHeight = Math.max(1, itemHeight * 0.96)
  const isRotated = props.item.bRotated ?? props.item.rotated ?? false
  const usesSceneCapture =
    props.item.bUseSceneCapture ?? props.item.useSceneCapture ?? true
  const rotateStaticIcon = isRotated && !usesSceneCapture
  const sourceRatio = Number(props.item.iconAspectRatio) > 0
    ? Number(props.item.iconAspectRatio)
    : Number(props.item.iconWidth) > 0 && Number(props.item.iconHeight) > 0
      ? Number(props.item.iconWidth) / Number(props.item.iconHeight)
      : itemWidth / itemHeight
  const visualRatio = rotateStaticIcon ? 1 / sourceRatio : sourceRatio
  const visualWidth = Math.min(maxWidth, maxHeight * visualRatio)
  const visualHeight = visualWidth / visualRatio
  const width = rotateStaticIcon ? visualHeight : visualWidth
  const height = rotateStaticIcon ? visualWidth : visualHeight
  return {
    left: `${(itemWidth - width) * 0.5}px`,
    top: `${(itemHeight - height) * 0.5}px`,
    width: `${width}px`,
    height: `${height}px`,
    transform: rotateStaticIcon ? 'rotate(90deg)' : 'none',
    transformOrigin: 'center'
  }
})

const stackLabel = computed(() => {
  const stack = props.item.stackSize || 1
  return stack > 1 ? `×${stack}` : ''
})
</script>

<template>
  <button
    :id="`inventory-item-${item.itemId}`"
    class="inventory-item"
    type="button"
    :style="itemStyle"
    :title="item.displayName"
    :data-item-id="item.itemId"
    @click="emit('select', item)"
  >
    <img v-if="item.icon" class="item-icon" :src="item.icon" :style="iconStyle" alt="" />
    <span v-else class="item-fallback">◇</span>
    <span class="item-name">{{ item.displayName || '未命名物品' }}</span>
    <strong v-if="stackLabel" class="item-stack">{{ stackLabel }}</strong>
  </button>
</template>
