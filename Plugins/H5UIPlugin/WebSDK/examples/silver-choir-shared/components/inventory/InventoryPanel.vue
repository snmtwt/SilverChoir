<script setup lang="ts">
import { computed } from 'vue'
import InventoryPane from './InventoryPane.vue'
import type { EquipmentSlotData, InventoryPanelData, InventorySelection } from './types'

const props = defineProps<{
  playerData: Required<InventoryPanelData>
  characterData: Required<InventoryPanelData>
  equipmentSlots: EquipmentSlotData[]
  characterName?: string
  slotWidth: number
  slotHeight: number
  layoutScale: number
}>()

const equipmentSlotWidth = computed(() => 100 * props.layoutScale)
const equipmentSlotHeight = computed(() => 120 * props.layoutScale)
const equipmentWideWidth = computed(() => 200 * props.layoutScale)
const equipmentSlotGap = computed(() => 4 * props.layoutScale)
const equipmentHandRowWidth = computed(() => equipmentWideWidth.value * 2 + equipmentSlotGap.value)

function equipmentSlotStyle(slot: EquipmentSlotData) {
  const width = slot.shape === 'wide' ? equipmentWideWidth.value : equipmentSlotWidth.value
  return {
    width: `${width}px`,
    height: `${equipmentSlotHeight.value}px`,
    flexBasis: slot.shape === 'wide' ? `${width}px` : undefined
  }
}

const emit = defineEmits<{
  selectItem: [selection: InventorySelection]
  selectEquipment: [slot: EquipmentSlotData]
}>()

const leftSlots = computed(() => props.equipmentSlots.filter(slot => slot.side === 'left'))
const rightSlots = computed(() => props.equipmentSlots.filter(slot => slot.side === 'right'))
const rightSingleSlots = computed(() => rightSlots.value.filter(
  slot => slot.slotId !== 'left-hand' && slot.slotId !== 'right-hand'
))
const handSlots = computed(() => rightSlots.value.filter(
  slot => slot.slotId === 'left-hand' || slot.slotId === 'right-hand'
))
const mergedHandSlot = computed(() => {
  if (handSlots.value.length < 2) return undefined
  return handSlots.value.find(slot => slot.itemId && slot.requiresAllSlotsInGroup)
})
const mergedHandProxySlots = computed(() => {
  const mergedSlot = mergedHandSlot.value
  return mergedSlot
    ? handSlots.value.filter(slot => slot.slotId !== mergedSlot.slotId)
    : []
})
const mergedHandLabel = computed(() => handSlots.value.map(slot => slot.label).join(' / '))

function mergedHandSlotStyle() {
  return {
    width: `${equipmentHandRowWidth.value}px`,
    height: `${equipmentSlotHeight.value}px`,
    flexBasis: `${equipmentHandRowWidth.value}px`
  }
}

function selectPlayerItem(selection: InventorySelection['item']): void {
  emit('selectItem', { inventoryId: props.playerData.inventoryId, region: 'player', item: selection })
}

function selectCharacterItem(selection: InventorySelection['item']): void {
  emit('selectItem', { inventoryId: props.characterData.inventoryId, region: 'character', item: selection })
}
</script>

<template>
  <section class="inventory-panel">
    <div class="inventory-outer-frame" aria-hidden="true"></div>
    <div class="inventory-panel-signal" aria-hidden="true"><i></i><i></i></div>

    <InventoryPane
      class="inventory-side inventory-side-player"
      :data="playerData"
      eyebrow="SC // PLAYER STORAGE"
      fallback-title="玩家仓库"
      :slot-width="slotWidth"
      :slot-height="slotHeight"
      @select="selectPlayerItem"
    />

    <section class="character-stage">
      <header class="character-stage-heading">
        <small>SC // PERSONNEL LOADOUT</small>
        <strong>{{ characterName || '当前整备人员' }}</strong>
      </header>

      <div class="model-aperture" aria-hidden="true">
        <span class="model-corner model-corner-tl"></span>
        <span class="model-corner model-corner-tr"></span>
        <span class="model-corner model-corner-bl"></span>
        <span class="model-corner model-corner-br"></span>
        <div class="model-axis model-axis-horizontal"></div>
        <div class="model-axis model-axis-vertical"></div>
        <div class="model-label">
          <small>3D PERSONNEL VIEW</small>
          <strong>人物展示区域</strong>
        </div>
      </div>

      <div
        class="equipment-rail equipment-rail-left hud-interactive"
        :style="{ width: `${equipmentSlotWidth}px` }"
      >
        <button
          v-for="slot in leftSlots"
          :id="`equipment-${slot.slotId}`"
          :key="slot.slotId"
          class="equipment-slot"
          :class="{ equipped: !!slot.itemId, blocked: slot.blocked, 'equipment-slot-wide': slot.shape === 'wide' }"
          :style="equipmentSlotStyle(slot)"
          :data-item-id="slot.itemId || undefined"
          :data-inventory-id="slot.inventoryId || undefined"
          :data-inventory-component-id="slot.inventoryComponentId || slot.inventoryId || undefined"
          :data-presentation-slot="slot.presentationSlot || characterData.presentationSlot || undefined"
          :data-inventory-view-id="characterData.inventoryViewId || undefined"
          :data-region-role="slot.regionRole || characterData.regionRole || undefined"
          :data-equipment-view-id="slot.equipmentViewId || undefined"
          :data-equipment-slot-id="`equipment-${slot.slotId}`"
          :data-equipment-slot-type="slot.equipmentSlotType || undefined"
          :data-equipment-sub-slot-index="slot.subSlotIndex"
          type="button"
          :title="slot.itemName || slot.label"
          @click="emit('selectEquipment', slot)"
        >
          <img v-if="slot.icon" :src="slot.icon" alt="" />
          <span v-else class="equipment-slot-symbol">◇</span>
          <small>{{ slot.label }}</small>
          <strong v-if="slot.itemName">{{ slot.itemName }}</strong>
        </button>
      </div>

      <div
        class="equipment-rail equipment-rail-right hud-interactive"
        :style="{ width: `${equipmentHandRowWidth}px` }"
      >
        <button
          v-for="slot in rightSingleSlots"
          :id="`equipment-${slot.slotId}`"
          :key="slot.slotId"
          class="equipment-slot"
          :class="{ equipped: !!slot.itemId, blocked: slot.blocked, 'equipment-slot-wide': slot.shape === 'wide' }"
          :style="equipmentSlotStyle(slot)"
          :data-item-id="slot.itemId || undefined"
          :data-inventory-id="slot.inventoryId || undefined"
          :data-inventory-component-id="slot.inventoryComponentId || slot.inventoryId || undefined"
          :data-presentation-slot="slot.presentationSlot || characterData.presentationSlot || undefined"
          :data-inventory-view-id="characterData.inventoryViewId || undefined"
          :data-region-role="slot.regionRole || characterData.regionRole || undefined"
          :data-equipment-view-id="slot.equipmentViewId || undefined"
          :data-equipment-slot-id="`equipment-${slot.slotId}`"
          :data-equipment-slot-type="slot.equipmentSlotType || undefined"
          :data-equipment-sub-slot-index="slot.subSlotIndex"
          type="button"
          :title="slot.itemName || slot.label"
          @click="emit('selectEquipment', slot)"
        >
          <img v-if="slot.icon" :src="slot.icon" alt="" />
          <span v-else class="equipment-slot-symbol">◇</span>
          <small>{{ slot.label }}</small>
          <strong v-if="slot.itemName">{{ slot.itemName }}</strong>
        </button>

        <div
          class="equipment-hand-row"
          :style="{ width: `${equipmentHandRowWidth}px`, gap: `${equipmentSlotGap}px` }"
        >
          <template v-if="mergedHandSlot">
          <button
            :id="`equipment-${mergedHandSlot.slotId}`"
            :key="mergedHandSlot.slotId"
            class="equipment-slot equipment-slot-merged equipped"
            :style="mergedHandSlotStyle()"
            :data-item-id="mergedHandSlot.itemId || undefined"
            :data-inventory-id="mergedHandSlot.inventoryId || undefined"
            :data-inventory-component-id="mergedHandSlot.inventoryComponentId || mergedHandSlot.inventoryId || undefined"
            :data-presentation-slot="mergedHandSlot.presentationSlot || characterData.presentationSlot || undefined"
            :data-inventory-view-id="characterData.inventoryViewId || undefined"
            :data-region-role="mergedHandSlot.regionRole || characterData.regionRole || undefined"
            :data-equipment-view-id="mergedHandSlot.equipmentViewId || undefined"
            :data-equipment-slot-id="`equipment-${mergedHandSlot.slotId}`"
            :data-equipment-slot-type="mergedHandSlot.equipmentSlotType || undefined"
            :data-equipment-sub-slot-index="mergedHandSlot.subSlotIndex"
            type="button"
            :title="mergedHandSlot.itemName || mergedHandLabel"
            @click="emit('selectEquipment', mergedHandSlot)"
          >
            <img v-if="mergedHandSlot.icon" :src="mergedHandSlot.icon" alt="" />
            <span v-else class="equipment-slot-symbol">&#9671;</span>
            <small>{{ mergedHandLabel }}</small>
            <strong v-if="mergedHandSlot.itemName">{{ mergedHandSlot.itemName }}</strong>
          </button>
          <span
            v-for="slot in mergedHandProxySlots"
            :id="`equipment-${slot.slotId}`"
            :key="slot.slotId"
            class="equipment-slot-merged-proxy"
            aria-hidden="true"
          ></span>
          </template>
          <template v-else>
          <button
            v-for="slot in handSlots"
            :id="`equipment-${slot.slotId}`"
            :key="slot.slotId"
            class="equipment-slot"
            :class="{ equipped: !!slot.itemId, blocked: slot.blocked, 'equipment-slot-wide': slot.shape === 'wide' }"
            :style="equipmentSlotStyle(slot)"
            :data-item-id="slot.itemId || undefined"
            :data-inventory-id="slot.inventoryId || undefined"
            :data-inventory-component-id="slot.inventoryComponentId || slot.inventoryId || undefined"
            :data-presentation-slot="slot.presentationSlot || characterData.presentationSlot || undefined"
            :data-inventory-view-id="characterData.inventoryViewId || undefined"
            :data-region-role="slot.regionRole || characterData.regionRole || undefined"
            :data-equipment-view-id="slot.equipmentViewId || undefined"
            :data-equipment-slot-id="`equipment-${slot.slotId}`"
            :data-equipment-slot-type="slot.equipmentSlotType || undefined"
            :data-equipment-sub-slot-index="slot.subSlotIndex"
            type="button"
            :title="slot.itemName || slot.label"
            @click="emit('selectEquipment', slot)"
          >
            <img v-if="slot.icon" :src="slot.icon" alt="" />
            <span v-else class="equipment-slot-symbol">◇</span>
            <small>{{ slot.label }}</small>
            <strong v-if="slot.itemName">{{ slot.itemName }}</strong>
          </button>
          </template>
        </div>
      </div>
    </section>

    <InventoryPane
      class="inventory-side inventory-side-character"
      :data="characterData"
      eyebrow="SC // CHARACTER INVENTORY"
      fallback-title="人物库存"
      :slot-width="slotWidth"
      :slot-height="slotHeight"
      @select="selectCharacterItem"
    />
  </section>
</template>

<style>
@keyframes inventory-slide-in {
  0% { opacity: 0; transform: translateY(34px); }
  70% { opacity: 1; transform: translateY(-4px); }
  100% { opacity: 1; transform: translateY(0); }
}
@keyframes inventory-side-enter-left {
  0% { opacity: 0; transform: translateX(-48px); }
  100% { opacity: 1; transform: translateX(0); }
}
@keyframes inventory-side-enter-right {
  0% { opacity: 0; transform: translateX(48px); }
  100% { opacity: 1; transform: translateX(0); }
}
@keyframes inventory-signal-drift {
  0% { opacity: 0.15; transform: translateX(-100px); }
  50% { opacity: 0.9; }
  100% { opacity: 0.1; transform: translateX(1100px); }
}
@keyframes inventory-status-pulse { 50% { opacity: 0.35; } }
@keyframes equipment-slot-pulse {
  0% { border-color: #285d7c; }
  100% { border-color: #4ca7d2; }
}
@keyframes model-axis-pulse { 50% { opacity: 0.18; } }

.inventory-panel {
  position: absolute;
  z-index: 45;
  top: 122px;
  right: 48px;
  bottom: 42px;
  left: 48px;
  min-width: 860px;
  padding: 18px;
  display: flex;
  overflow: hidden;
  pointer-events: none;
  background-color: rgba(2, 9, 15, 0.18);
}
.inventory-outer-frame { position: absolute; top: 0; right: 0; bottom: 0; left: 0; border: 1px solid rgba(55, 145, 193, 0.54); }
.inventory-panel-signal { position: absolute; z-index: 3; top: 0; left: 18px; right: 18px; height: 3px; overflow: hidden; }
.inventory-panel-signal i { position: absolute; top: 0; left: 0; width: 150px; height: 2px; background-color: #72d3f7; animation: inventory-signal-drift 4.8s linear infinite; }
.inventory-panel-signal i:nth-child(2) { animation: inventory-signal-drift 4.8s linear 2.4s infinite; }

.inventory-side {
  position: relative;
  z-index: 2;
  flex: 0 1 31%;
  width: 31%;
  min-width: 0;
  height: 100%;
  padding: 15px 14px 38px;
  overflow: hidden;
  pointer-events: auto;
  border: 1px solid rgba(46, 130, 177, 0.62);
  background-color: rgba(4, 17, 28, 0.86);
  box-shadow: 0 0 28px rgba(0, 9, 16, 0.72);
}
.inventory-side-player { animation: inventory-side-enter-left 0.38s ease-out 1 both; }
.inventory-side-character { animation: inventory-side-enter-right 0.38s ease-out 1 both; }
.inventory-pane-heading { height: 70px; display: flex; align-items: flex-start; border-bottom: 1px solid rgba(45, 128, 173, 0.36); }
.inventory-pane-title { flex: 1; min-width: 0; }
.inventory-pane-heading small, .inventory-pane-footer { color: #53b9ec; letter-spacing: 1.25px; font-size: 9px; }
.inventory-pane-heading h3 { margin: 6px 0 2px; overflow: hidden; color: #dceef7; font-size: 18px; letter-spacing: 2px; font-weight: 500; white-space: nowrap; }
.inventory-pane-heading p { margin: 0; color: #587d91; font-size: 9px; }
.inventory-pane-count { width: 54px; padding: 7px 5px; text-align: center; border-left: 2px solid #319dcd; background-color: rgba(13, 48, 68, 0.68); }
.inventory-pane-count small { display: block; color: #6595ad; }
.inventory-pane-count strong { display: block; margin-top: 4px; color: #e2f3fb; font-size: 18px; font-weight: 500; }
.inventory-pane-metrics { height: 48px; display: flex; align-items: center; border-bottom: 1px solid rgba(29, 83, 114, 0.42); }
.inventory-pane-metrics span { flex: 1; padding-left: 8px; border-left: 1px solid #205674; }
.inventory-pane-metrics span:first-child { border-left: 0 solid transparent; }
.inventory-pane-metrics small { display: block; color: #668da1; font-size: 8px; }
.inventory-pane-metrics strong { display: block; margin-top: 3px; color: #bdd8e5; font-size: 11px; font-weight: 500; }
.inventory-containers {
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  gap: 14px;
  padding-top: 12px;
  overflow: auto;
}
.inventory-container-group {
  flex: 0 0 auto;
  display: inline-flex;
  flex-direction: column;
  align-items: flex-start;
  width: fit-content;
  padding: 8px;
  border: 1px solid rgba(42, 144, 198, 0.34);
  background-color: rgba(1, 10, 17, 0.72);
}
.inventory-container-owner {
  align-self: stretch;
  width: 100%;
  height: 24px;
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  overflow: hidden;
  color: #78bad8;
  font-size: 9px;
  letter-spacing: 1px;
  white-space: nowrap;
}
.inventory-container-owner strong { overflow: hidden; font-weight: 500; text-overflow: ellipsis; }
.inventory-container-owner span { flex-shrink: 0; margin-left: 12px; color: #9bc6d9; }
.inventory-container-layout { position: relative; }
.inventory-container {
  display: inline-block;
  max-width: 100%;
  padding: 0;
  border: 0 solid transparent;
  background-color: transparent;
}
.inventory-grid { position: relative; max-width: 100%; overflow: hidden; border: 1px solid #246f98; background-color: rgba(7, 27, 39, 0.68); }
.inventory-grid-cell {
  position: absolute;
  display: block;
  border-right: 1px solid rgba(49, 122, 160, 0.34);
  border-bottom: 1px solid rgba(49, 122, 160, 0.34);
}
.inventory-grid.inventory-grid-drop-pending { border-color: #55bde8; }
.inventory-grid.inventory-grid-drop-valid { border-color: #55d69a; }
.inventory-grid.inventory-grid-drop-replace { border-color: #e3b75a; }
.inventory-grid.inventory-grid-drop-invalid { border-color: #e06b78; }
.inventory-grid-cell.inventory-grid-cell-drop-preview { z-index: 1; }
.inventory-grid-cell.inventory-grid-cell-drop-pending {
  border: 1px solid rgba(85, 189, 232, 0.9);
  background-color: rgba(50, 151, 196, 0.26);
}
.inventory-grid-cell.inventory-grid-cell-drop-valid {
  border: 1px solid rgba(85, 214, 154, 0.95);
  background-color: rgba(43, 164, 111, 0.34);
}
.inventory-grid-cell.inventory-grid-cell-drop-replace {
  border: 1px solid rgba(227, 183, 90, 0.95);
  background-color: rgba(190, 132, 38, 0.34);
}
.inventory-grid-cell.inventory-grid-cell-drop-replacement {
  border: 2px solid rgba(255, 210, 112, 0.98);
  background-color: rgba(230, 145, 34, 0.46);
}
.inventory-grid-cell.inventory-grid-cell-drop-invalid {
  border: 1px solid rgba(224, 107, 120, 0.95);
  background-color: rgba(190, 54, 72, 0.34);
}
.inventory-item { position: absolute; display: flex; align-items: center; justify-content: center; overflow: hidden; padding: 4px; color: #d9f3ff; border: 1px solid #2ba9e7; background-color: rgba(10, 55, 79, 0.94); }
.inventory-item:hover, .inventory-item:focus { transform: translateY(-2px); border-color: #93e2ff; background-color: rgba(16, 77, 107, 0.98); }
.inventory-item:active { transform: translateY(1px); }
.inventory-item.inventory-item-drag-source { opacity: 0.28; }
.inventory-item .item-icon, .inventory-item .item-fallback, .inventory-item .item-name, .inventory-item .item-stack { pointer-events: none; }
.item-icon { position: absolute; z-index: 2; max-width: none; max-height: none; image-color: #ffffff; }
.item-fallback { color: #58caff; font-size: 21px; }
.item-name { position: absolute; z-index: 3; left: 5px; right: 5px; bottom: 3px; overflow: hidden; color: #c8eafa; font-size: 9px; text-align: left; white-space: nowrap; }
.item-stack { position: absolute; z-index: 3; right: 4px; top: 3px; color: #f2fbff; font-size: 10px; }
.inventory-empty { margin-top: 12px; padding: 20px; border: 1px solid #285d78; color: #60889d; }
.inventory-pane-footer { position: absolute; left: 14px; right: 14px; bottom: 14px; display: flex; align-items: center; }
.inventory-pane-footer strong { margin-left: 7px; font-weight: 500; }
.inventory-status-dot { width: 6px; height: 6px; border-radius: 50%; background-color: #4bd4ff; box-shadow: 0 0 8px #4bd4ff; animation: inventory-status-pulse 1.8s ease-in-out infinite; }

.character-stage { position: relative; z-index: 1; flex: 1 1 0; height: 100%; min-width: 310px; pointer-events: none; }
.character-stage-heading { position: absolute; z-index: 3; top: 8px; left: 92px; right: 92px; height: 48px; text-align: center; }
.character-stage-heading small { display: block; color: #4e93b6; font-size: 9px; letter-spacing: 1.5px; }
.character-stage-heading strong { display: block; margin-top: 6px; color: #bcd7e4; font-size: 15px; letter-spacing: 2px; font-weight: 500; }
.model-aperture { position: absolute; top: 58px; right: 86px; bottom: 26px; left: 86px; background-color: rgba(2, 10, 17, 0.08); }
.model-corner { position: absolute; width: 25px; height: 25px; }
.model-corner-tl { top: 0; left: 0; border-top: 1px solid #388aae; border-left: 1px solid #388aae; }
.model-corner-tr { top: 0; right: 0; border-top: 1px solid #388aae; border-right: 1px solid #388aae; }
.model-corner-bl { bottom: 0; left: 0; border-bottom: 1px solid #388aae; border-left: 1px solid #388aae; }
.model-corner-br { right: 0; bottom: 0; border-right: 1px solid #388aae; border-bottom: 1px solid #388aae; }
.model-axis { position: absolute; opacity: 0.34; background-color: #286c8d; animation: model-axis-pulse 2.4s ease-in-out infinite; }
.model-axis-horizontal { top: 50%; left: 15%; right: 15%; height: 1px; }
.model-axis-vertical { top: 15%; bottom: 15%; left: 50%; width: 1px; }
.model-label { position: absolute; left: 0; right: 0; bottom: 20px; text-align: center; }
.model-label small { display: block; color: #356f8e; font-size: 8px; letter-spacing: 1.5px; }
.model-label strong { display: block; margin-top: 5px; color: #507f95; font-size: 11px; letter-spacing: 1.4px; font-weight: 400; }
.equipment-rail { position: absolute; z-index: 5; top: 62px; bottom: 24px; width: 100px; display: flex; flex-direction: column; justify-content: space-between; pointer-events: auto; }
.equipment-rail-left { left: 8px; }
.equipment-rail-right { right: 8px; width: 324px; align-items: flex-end; }
.equipment-slot { position: relative; flex-shrink: 0; width: 100px; height: 120px; padding: 4px; overflow: hidden; color: #8db5c8; border: 1px solid #285d7c; background-color: rgba(5, 20, 30, 0.82); animation: equipment-slot-pulse 1.8s ease-in-out infinite alternate; }
.equipment-slot-wide { width: 200px; }
.equipment-hand-row { display: flex; width: 404px; gap: 4px; justify-content: flex-end; }
.equipment-hand-row .equipment-slot-wide { flex: 0 0 200px; }
.equipment-hand-row .equipment-slot-merged { flex-shrink: 0; }
.equipment-slot-merged-proxy { display: none; }
.equipment-slot:hover, .equipment-slot:focus { color: #d8eff9; border-color: #7ad2f4; background-color: rgba(10, 42, 59, 0.94); }
.equipment-slot:active { transform: translateY(2px); }
.equipment-slot.equipped { border-color: #54b8e2; background-color: rgba(11, 49, 69, 0.94); }
.equipment-slot.blocked:not(.equipped) { opacity: 0.52; background-color: rgba(18, 25, 31, 0.9); }
.equipment-slot.inventory-item-drag-source { opacity: 0.28; }
.equipment-slot.equipment-slot-drop-pending { border-color: #55bde8; background-color: rgba(50, 151, 196, 0.32); }
.equipment-slot.equipment-slot-drop-valid { border-color: #55d69a; background-color: rgba(43, 164, 111, 0.38); }
.equipment-slot.equipment-slot-drop-replace { border-color: #e3b75a; background-color: rgba(190, 132, 38, 0.42); }
.equipment-slot.equipment-slot-drop-invalid { border-color: #e06b78; background-color: rgba(190, 54, 72, 0.38); }
.equipment-slot.equipment-slot-replacement-preview { box-shadow: inset 0 0 0 2px rgba(255, 210, 112, 0.92); }
.equipment-slot img { display: block; max-width: 82%; max-height: 70%; margin: 0 auto; }
.equipment-slot-symbol { display: block; color: #4da3c9; font-size: 22px; }
.equipment-slot small { position: absolute; right: 3px; bottom: 3px; left: 3px; display: block; overflow: hidden; color: #6e9aaf; font-size: 8px; white-space: nowrap; }
.equipment-slot strong { position: absolute; top: 3px; right: 3px; left: 3px; overflow: hidden; color: #cce8f4; font-size: 8px; font-weight: 500; white-space: nowrap; }

@media (max-width: 1500px) {
  .inventory-panel { left: 48px; min-width: 820px; }
  .inventory-side { flex-basis: 30%; width: 30%; }
  .character-stage { min-width: 286px; }
  .model-aperture { right: 72px; left: 72px; }
}
</style>
