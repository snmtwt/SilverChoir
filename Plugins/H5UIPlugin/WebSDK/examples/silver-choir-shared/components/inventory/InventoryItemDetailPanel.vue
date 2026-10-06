<script setup lang="ts">
import { computed, onMounted, onUnmounted } from 'vue'
import type { ItemDetailData } from './types'
import { sisH5UIPointerSessions } from './SISH5UIPointerSessionCoordinator'

const props = defineProps<{
  detail: ItemDetailData
  baseSlotWidth: number
  baseSlotHeight: number
  layoutScale: number
}>()

const emit = defineEmits<{
  close: []
  rotate: [deltaX: number, deltaY: number]
  zoom: [delta: number]
}>()

let rotatingPreview = false
let lastRotationPointerX = 0
let lastRotationPointerY = 0
let rotationMoveCount = 0
let rotationPointerBaselineReady = false
const previewPointerOwner =
  `item-detail-preview-${Math.random().toString(36).slice(2)}`

const visibleAttachmentSlots = computed(() =>
  props.detail.supportsAttachments ? props.detail.attachmentSlots : []
)
const attachmentGap = computed(() => 14 * props.layoutScale)

const attachmentPlacements = computed(() => {
  const rowRightEdges = new Map<number, number>()
  return [...visibleAttachmentSlots.value]
    .sort((left, right) =>
      Number(left.row) - Number(right.row) ||
      Number(left.column) - Number(right.column) ||
      Number(left.slotIndex) - Number(right.slotIndex)
    )
    .map(slot => {
      const row = Number(slot.row)
      const desiredLeft = Number(slot.column) * (props.baseSlotWidth + attachmentGap.value)
      const previousRight = rowRightEdges.get(row)
      const left = previousRight === undefined
        ? desiredLeft
        : Math.max(desiredLeft, previousRight + attachmentGap.value)
      const top = row * (props.baseSlotHeight + attachmentGap.value)
      const width = Math.max(0.1, Number(slot.width)) * props.baseSlotWidth
      const height = Math.max(0.1, Number(slot.height)) * props.baseSlotHeight
      rowRightEdges.set(row, left + width)
      return { slot, left, top, width, height }
    })
})

const attachmentCanvasStyle = computed(() => {
  let width = 0
  let height = 0
  for (const placement of attachmentPlacements.value) {
    width = Math.max(width, placement.left + placement.width)
    height = Math.max(height, placement.top + placement.height)
  }
  return {
    width: `${Math.max(props.baseSlotWidth, width)}px`,
    height: `${Math.max(props.baseSlotHeight, height)}px`
  }
})

function attachmentSlotStyle(slotIndex: number) {
  const placement = attachmentPlacements.value.find(
    candidate => Number(candidate.slot.slotIndex) === Number(slotIndex)
  )
  if (!placement) return {}
  return {
    left: `${placement.left}px`,
    top: `${placement.top}px`,
    width: `${placement.width}px`,
    height: `${placement.height}px`
  }
}

function handlePreviewLoaded(event: Event): void {
  const image = event.target as HTMLImageElement | null
  console.log('[SISH5UI ItemDetail] preview loaded', {
    source: props.detail.previewTexture,
    reportedWidth: props.detail.previewWidth,
    reportedHeight: props.detail.previewHeight,
    naturalWidth: image?.naturalWidth || 0,
    naturalHeight: image?.naturalHeight || 0,
    interactive: props.detail.interactivePreview
  })
}

function beginPreviewRotation(event: MouseEvent): void {
  if (event.button !== 0 || !props.detail.interactivePreview) return
  if (!sisH5UIPointerSessions.begin(previewPointerOwner, event, {
    move: handlePointerMove,
    end: finishPreviewRotation
  })) return
  rotatingPreview = true
  rotationMoveCount = 0
  rotationPointerBaselineReady = false
  lastRotationPointerX = event.clientX
  lastRotationPointerY = event.clientY
  document.documentElement?.classList.add('item-detail-interaction-active')
  console.warn('[SISH5UI PointerTrace][Rotation] begin', {
    sessionId: props.detail.sessionId,
    clientX: event.clientX,
    clientY: event.clientY
  })
  event.preventDefault()
  event.stopPropagation()
}

function handlePointerMove(event: MouseEvent): void {
  if (!rotatingPreview) return
  // Element mousedown and document-level captured mousemove can be expressed
  // in different RmlUi coordinate spaces when the detail panel is translated.
  // Establish the document-space baseline from the first captured move so a
  // new gesture never starts with a large artificial rotation jump.
  if (!rotationPointerBaselineReady) {
    rotationPointerBaselineReady = true
    lastRotationPointerX = event.clientX
    lastRotationPointerY = event.clientY
    return
  }
  const deltaX = lastRotationPointerX - event.clientX
  const deltaY = lastRotationPointerY - event.clientY
  lastRotationPointerX = event.clientX
  lastRotationPointerY = event.clientY
  if (deltaX !== 0 || deltaY !== 0) {
    rotationMoveCount += 1
    if (rotationMoveCount <= 3 || rotationMoveCount % 15 === 0) {
      console.warn('[SISH5UI PointerTrace][Rotation] emit', {
        sessionId: props.detail.sessionId,
        moveCount: rotationMoveCount,
        deltaX,
        deltaY,
        clientX: event.clientX,
        clientY: event.clientY
      })
    }
    emit('rotate', deltaX, deltaY)
  }
}

function finishPreviewRotation(): void {
  if (rotatingPreview) {
    console.warn('[SISH5UI PointerTrace][Rotation] end', {
      sessionId: props.detail.sessionId,
      moveCount: rotationMoveCount
    })
  }
  rotatingPreview = false
  rotationPointerBaselineReady = false
  document.documentElement?.classList.remove('item-detail-interaction-active')
}

function handlePreviewWheel(event: WheelEvent): void {
  if (!props.detail.interactivePreview) return
  event.preventDefault()
  event.stopPropagation()
  const normalizedDelta = event.deltaY === 0
    ? 0
    : -Math.max(-1, Math.min(1, event.deltaY))
  if (normalizedDelta !== 0) emit('zoom', normalizedDelta)
}

function handleCloseMouseDown(event: MouseEvent): void {
  if (event.button !== 0) return
  event.preventDefault()
  event.stopPropagation()
  emit('close')
}

onMounted(() => {
  sisH5UIPointerSessions.retain(window)
})

onUnmounted(() => {
  sisH5UIPointerSessions.releaseHost(previewPointerOwner)
  document.documentElement?.classList.remove('item-detail-interaction-active')
})
</script>

<template>
  <section
    class="item-detail-panel hud-interactive"
    role="dialog"
    aria-modal="true"
  >
    <header class="item-detail-header">
      <div>
        <small>ITEM // DETAIL</small>
        <strong>{{ detail.displayName }}</strong>
      </div>
      <button
        id="item-detail-close"
        class="item-detail-close"
        type="button"
        aria-label="关闭"
        @mousedown="handleCloseMouseDown"
        @click="emit('close')"
      >×</button>
    </header>

    <div
      v-if="visibleAttachmentSlots.length"
      class="item-detail-attachments"
      :style="attachmentCanvasStyle"
    >
      <div
        v-for="slot in visibleAttachmentSlots"
        :key="slot.slotIndex"
        class="item-detail-attachment-slot"
        :class="{ occupied: !!slot.itemId }"
        :style="attachmentSlotStyle(slot.slotIndex)"
        :data-inventory-id="detail.inventoryId || undefined"
        :data-inventory-component-id="detail.inventoryComponentId || detail.inventoryId || undefined"
        :data-presentation-slot="detail.presentationSlot || undefined"
        :data-parent-item-id="detail.itemId"
        :data-attachment-slot-index="slot.slotIndex"
        :data-item-id="slot.itemId || undefined"
        :title="slot.itemName || slot.name || `附件槽 ${slot.slotIndex + 1}`"
      >
        <img v-if="slot.itemIcon || slot.slotTexture" :src="slot.itemIcon || slot.slotTexture" alt="" />
        <span v-else>◇</span>
        <small>{{ slot.itemName || slot.name || `附件 ${slot.slotIndex + 1}` }}</small>
        <b v-if="(slot.stackSize || 0) > 1">×{{ slot.stackSize }}</b>
      </div>
    </div>

    <div
      class="item-detail-preview"
      :class="{ interactive: detail.interactivePreview }"
      @mousedown="beginPreviewRotation"
      @wheel="handlePreviewWheel"
    >
      <img
        v-if="detail.previewTexture"
        :src="detail.previewTexture"
        alt=""
        @load="handlePreviewLoaded"
      />
      <span v-else class="item-detail-preview-empty">无可用预览</span>
      <small v-if="detail.interactivePreview" class="item-detail-preview-hint">
        按住左键旋转 · 滚轮缩放
      </small>
      <strong v-if="(detail.maxStackSize || 1) > 1" class="item-detail-ammo">
        {{ detail.stackSize || 0 }}/{{ detail.maxStackSize }}
      </strong>
    </div>

    <div class="item-detail-information">
      <p>{{ detail.description || '暂无物品描述。' }}</p>
      <dl>
        <div><dt>价格</dt><dd>{{ Number(detail.price || 0).toLocaleString() }}</dd></div>
        <div><dt>重量</dt><dd>{{ Number(detail.weight || 0).toLocaleString() }}</dd></div>
        <div
          v-for="property in detail.properties"
          :key="property.name"
          :class="{ modified: property.modified }"
        >
          <dt>{{ property.name }}</dt>
          <dd>{{ property.value }}{{ property.unit || '' }}</dd>
        </div>
      </dl>
    </div>
  </section>
</template>

<style scoped>
.item-detail-panel {
  position: fixed;
  top: 50%;
  left: 50%;
  transform: translate(-50%, -50%);
  z-index: 11000;
  display: flex;
  flex-direction: column;
  width: 760px;
  max-height: calc(100vh - 36px);
  padding: 0;
  overflow: hidden;
  color: #cceaf7;
  border: 1px solid #3a9bc5;
  background: rgba(3, 17, 27, 0.985);
  box-shadow: 0 0 34px rgba(0, 9, 16, 0.82);
}

.item-detail-header {
  display: flex;
  flex: 0 0 54px;
  align-items: center;
  justify-content: space-between;
  padding: 0 12px 0 16px;
  border-bottom: 1px solid rgba(66, 165, 207, 0.52);
  background: rgba(10, 48, 66, 0.94);
  cursor: move;
  user-select: none;
}

.item-detail-header > div {
  pointer-events: none;
}

.item-detail-header small,
.item-detail-header strong {
  display: block;
}

.item-detail-header small {
  color: #4e9fbe;
  font-size: 8px;
  letter-spacing: 1.6px;
}

.item-detail-header strong {
  margin-top: 3px;
  color: #e1f6ff;
  font-size: 16px;
  font-weight: 500;
}

.item-detail-close {
  position: relative;
  z-index: 1;
  flex: 0 0 30px;
  width: 30px;
  height: 30px;
  pointer-events: auto;
  color: #bdeaff;
  border: 1px solid #397d9b;
  background: rgba(4, 24, 35, 0.86);
  font-size: 21px;
  transition: color 100ms linear, border-color 100ms linear, background-color 100ms linear;
}

.item-detail-close:hover {
  color: #ffffff;
  border-color: #71d4fa;
  background: rgba(24, 102, 133, 0.96);
}

.item-detail-attachments {
  position: relative;
  flex: 0 0 auto;
  align-self: center;
  margin: 10px auto 8px;
}

.item-detail-attachment-slot {
  position: absolute;
  display: flex;
  align-items: center;
  justify-content: center;
  overflow: hidden;
  border: 1px solid #346f8b;
  background: rgba(5, 27, 39, 0.92);
}

.item-detail-attachment-slot.occupied {
  border-color: #56b8df;
  background: rgba(10, 56, 76, 0.94);
}

.item-detail-attachment-slot-drop-pending {
  border-color: #d4b75a;
  background: rgba(105, 82, 18, 0.82);
}

.item-detail-attachment-slot-drop-valid {
  border-color: #55e6a5;
  background: rgba(16, 100, 67, 0.86);
}

.item-detail-attachment-slot-drop-replace {
  border-color: #efb452;
  background: rgba(112, 70, 13, 0.88);
}

.item-detail-attachment-slot-drop-invalid {
  border-color: #ef6070;
  background: rgba(105, 22, 35, 0.88);
}

.item-detail-attachment-slot img {
  display: block;
  max-width: 82%;
  max-height: 74%;
}

.item-detail-attachment-slot span {
  color: #4b9ebe;
  font-size: 20px;
}

.item-detail-attachment-slot small {
  position: absolute;
  right: 3px;
  bottom: 2px;
  left: 3px;
  overflow: hidden;
  color: #91bed0;
  font-size: 8px;
  text-align: center;
  white-space: nowrap;
}

.item-detail-attachment-slot b {
  position: absolute;
  top: 2px;
  right: 3px;
  font-size: 9px;
}

.item-detail-preview {
  position: relative;
  flex: 0 0 320px;
  display: flex;
  align-items: center;
  justify-content: center;
  margin: 0 12px;
  overflow: hidden;
  border: 1px solid rgba(63, 151, 189, 0.72);
  background: rgba(1, 8, 13, 0.78);
}

.item-detail-preview.interactive {
  cursor: grab;
}

.item-detail-preview img {
  display: block;
  width: auto;
  height: auto;
  max-width: 100%;
  max-height: 100%;
  pointer-events: none;
}

.item-detail-preview.interactive img {
  opacity: 0.72;
}

.item-detail-preview-empty {
  position: absolute;
  top: 46%;
  right: 0;
  left: 0;
  color: #527a8c;
  text-align: center;
}

.item-detail-preview-hint {
  position: absolute;
  right: 10px;
  bottom: 8px;
  color: rgba(151, 207, 230, 0.72);
  font-size: 9px;
  pointer-events: none;
}

.item-detail-ammo {
  position: absolute;
  bottom: 8px;
  left: 10px;
  color: #d8f4ff;
  font-size: 12px;
}

.item-detail-information {
  display: flex;
  flex-direction: row;
  min-height: 126px;
  margin: 4px 12px 12px;
  border: 1px solid rgba(63, 151, 189, 0.72);
}

.item-detail-information p {
  flex: 1 1 68%;
  width: 68%;
  margin: 0;
  padding: 10px;
  overflow: auto;
  border-right: 1px solid rgba(63, 151, 189, 0.54);
  color: #bdd8e4;
  font-size: 11px;
  line-height: 1.35;
}

.item-detail-information dl {
  flex: 0 0 32%;
  width: 32%;
  margin: 0;
  padding: 8px 10px;
}

.item-detail-information dl div {
  display: flex;
  justify-content: space-between;
  min-height: 20px;
  border-bottom: 1px dotted rgba(83, 145, 171, 0.38);
}

.item-detail-information dt,
.item-detail-information dd {
  margin: 0;
  font-size: 11px;
}

.item-detail-information dd {
  color: #e4f6fd;
}

.item-detail-information .modified dd {
  color: #e6c858;
}
</style>
