<script setup lang="ts">
import { computed, nextTick, onMounted, onUnmounted, reactive, ref } from '@h5ui-plugin/vue'
import SceneSwitcher from './components/SceneSwitcher.vue'
import LocationRail from './components/LocationRail.vue'
import CurrencyPanel from './components/CurrencyPanel.vue'
import TimeControl from './components/TimeControl.vue'
import MenuLauncher from './components/MenuLauncher.vue'
import InventoryPanel from '../../silver-choir-shared/components/inventory/InventoryPanel.vue'
import InventoryContextMenu from '../../silver-choir-shared/components/inventory/InventoryContextMenu.vue'
import InventoryItemDetailPanel from '../../silver-choir-shared/components/inventory/InventoryItemDetailPanel.vue'
import { SISH5UIInventoryDragController } from '../../silver-choir-shared/components/inventory/SISH5UIInventoryDragController'
import { sisH5UIPointerSessions } from '../../silver-choir-shared/components/inventory/SISH5UIPointerSessionCoordinator'
import localizationConfig from './localization.json'
import type { CalendarTime, LocationId, LocationOption, SceneId, SceneOption } from './types'
import type { EquipmentSlotData, EquipmentSlotElementData, EquipmentSnapshotData, InventoryItemData, InventoryPanelData, InventorySelection, ItemDetailData, ItemMenuEntryData, ItemMenuShowData } from '../../silver-choir-shared/components/inventory/types'

type Language = keyof typeof localizationConfig.languages
type H5UIWindow = Window & {
	H5UI?: {
		emit: (eventType: string, functionName: string, ...arguments_: unknown[]) => boolean
	}
  ue?: { getData?: (name: string) => string }
}

type BaseHudState = {
  language?: Language
  scene?: SceneId
  location?: LocationId
  currency?: number
  currencyDelta?: number
  currencyIcon?: string
  paused?: boolean
  speed?: number
  time?: Partial<CalendarTime>
  inventory?: InventoryPanelData
  characterInventory?: InventoryPanelData
  equipmentSlots?: EquipmentSlotData[]
  characterName?: string
}

const STATE_EVENT_NAME = 'SilverBaseHUDState'
const LANGUAGE_EVENT_NAME = 'SilverBaseHUDLanguage'
const PAGE_DATA_EVENT_NAME = 'H5UIPageData'
const INVENTORY_EVENT_NAME = 'SISH5UI.Inventory.Snapshot'
const INVENTORY_BINDING_AVAILABLE_EVENT_NAME = 'SISH5UI.Inventory.BindingAvailable'
const INVENTORY_VIEW_BOUND_EVENT_NAME = 'SISH5UI.Inventory.ViewBound'
const INVENTORY_VIEW_UNBOUND_EVENT_NAME = 'SISH5UI.Inventory.ViewUnbound'
const INVENTORY_LAYOUT_CONFIGURATION_EVENT_NAME = 'SISH5UI.Inventory.LayoutConfiguration'
const EQUIPMENT_EVENT_NAME = 'SISH5UI.Equipment.Snapshot'
const EQUIPMENT_CLEARED_EVENT_NAME = 'SISH5UI.Equipment.ViewCleared'
const EQUIPMENT_SLOT_CONFIGURATION_REQUEST_EVENT_NAME = 'SISH5UI.Equipment.RequestSlotConfiguration'
const ITEM_MENU_SHOW_EVENT_NAME = 'SISH5UI.ItemMenu.Show'
const ITEM_DETAIL_SHOW_EVENT_NAME = 'SISH5UI.ItemDetail.Show'
// RmlUi/Coherent reports the secondary mouse button as 1 (standard browsers
// commonly report it as 2).
const ITEM_MENU_MOUSE_BUTTON = 1
const INVENTORY_HANDLER_EVENT_TYPE = 'SISH5UI'
const HAVEN_HANDLER_EVENT_TYPE = 'Haven'
const HAVEN_SCENE_SWITCH_FUNCTION = 'RequestHavenSceneChange'
const HAVEN_ROOM_FUNCTIONS: Partial<Record<LocationId, string>> = {
  'command-room': 'RequestHavenCombatCommandRoom',
  'readiness-room': 'RequestHavenReadinessRoom',
  'squad-room': 'RequestHavenSquadMeetingRoom',
  'commander-office': 'RequestHavenCommanderOffice'
}
const INVENTORY_HANDLER_FUNCTIONS: Record<string, string> = {
  'SISH5UI.Inventory.ViewCreated': 'InventoryViewCreated',
  'SISH5UI.Inventory.ViewDestroyed': 'InventoryViewDestroyed',
  'SISH5UI.Inventory.EquipmentSlotsCreated': 'EquipmentSlotsCreated',
  'SISH5UI.Drag.Begin': 'DragBegin',
  'SISH5UI.Drag.End': 'DragEnd',
  'SISH5UI.Drag.Cancel': 'DragCancel',
  'SISH5UI.Drag.ProbeRequest': 'DragProbeRequest',
  'SISH5UI.Drag.DropRequest': 'DragDropRequest',
  'SISH5UI.ItemMenu.Request': 'RequestItemMenu',
  'SISH5UI.ItemMenu.Action': 'ItemMenuAction',
  'SISH5UI.ItemDetail.Rotate': 'RotateItemDetail',
  'SISH5UI.ItemDetail.Zoom': 'ZoomItemDetail',
  'SISH5UI.ItemDetail.Close': 'CloseItemDetail'
}
const DEFAULT_CURRENCY_ICON = 'ueasset:///Game/Resources/Texture/Currency/val-currency-symbol-white.val-currency-symbol-white'
const fastSpeeds = [5, 10, 20]
const DEFAULT_LOCATIONS: Record<SceneId, LocationId> = {
  haven: 'hall',
  underground: 'command-room'
}
type InventoryPresentationSlot = 'inventory' | 'characterInventory'
const INVENTORY_VIEW_IDS: Record<InventoryPresentationSlot, string> = {
  inventory: 'readiness-player-storage',
  characterInventory: 'readiness-operated-unit-inventory'
}
const INVENTORY_REGION_ROLES: Record<InventoryPresentationSlot, string> = {
  inventory: 'warehouse',
  characterInventory: 'operated-unit'
}

const language = ref<Language>(localizationConfig.defaultLanguage as Language)
const copy = computed(() => localizationConfig.languages[language.value])
const activeScene = ref<SceneId>('haven')
const activeLocations = reactive<Record<SceneId, LocationId>>({ ...DEFAULT_LOCATIONS })
const currency = ref(128460)
const currencyDelta = ref(2340)
const currencyIcon = ref(DEFAULT_CURRENCY_ICON)
const paused = ref(false)
const speed = ref(1)
const time = reactive<CalendarTime>({ month: 9, day: 18, hour: 21, minute: 40 })
const baseSlotWidth = ref(60)
const baseSlotHeight = ref(60)
const referenceViewportHeight = ref(1080)
const viewportHeight = ref(window.innerHeight > 1 ? window.innerHeight : 1080)
// BaseSlotSize is authored for a 1080-high reference surface. Keep that authored
// size on smaller windows so slots remain readable, and only enlarge the layout
// proportionally when the viewport is taller than the reference surface.
const inventoryLayoutScale = computed(() => Math.max(1, viewportHeight.value / referenceViewportHeight.value))
const scaledSlotWidth = computed(() => baseSlotWidth.value * inventoryLayoutScale.value)
const scaledSlotHeight = computed(() => baseSlotHeight.value * inventoryLayoutScale.value)
const inventory = reactive<Required<InventoryPanelData>>({
  inventoryId: 'readiness-inventory',
  inventoryComponentId: '',
  presentationSlot: 'inventory',
  inventoryViewId: INVENTORY_VIEW_IDS.inventory,
  regionRole: INVENTORY_REGION_ROLES.inventory,
  inventoryName: '玩家仓库',
  revision: 1,
  totalPrice: 12460,
  totalWeight: 37.5,
  containers: [{ containerIndex: 0, columns: 8, rows: 5, layoutColumn: 0, layoutRow: 0 }],
  items: [
    { itemId: 'sample-rifle', displayName: '制式步枪', itemType: 'Weapon', containerIndex: 0, row: 0, column: 0, rowSpan: 2, columnSpan: 3, stackSize: 1, maxStackSize: 1 },
    { itemId: 'sample-ammo', displayName: '步枪弹药', itemType: 'Ammo', containerIndex: 0, row: 0, column: 4, rowSpan: 1, columnSpan: 1, stackSize: 24, maxStackSize: 60 },
    { itemId: 'sample-kit', displayName: '医疗包', itemType: 'Consumable', containerIndex: 0, row: 3, column: 1, rowSpan: 2, columnSpan: 2, stackSize: 3, maxStackSize: 5 }
  ]
})
const characterInventory = reactive<Required<InventoryPanelData>>({
  inventoryId: '',
  inventoryComponentId: '',
  presentationSlot: 'characterInventory',
  inventoryViewId: INVENTORY_VIEW_IDS.characterInventory,
  regionRole: INVENTORY_REGION_ROLES.characterInventory,
  inventoryName: '人物库存',
  revision: 0,
  totalPrice: 0,
  totalWeight: 0,
  containers: [],
  items: []
})
const equipmentSlots = reactive<EquipmentSlotData[]>([
  { slotId: 'head', label: '头部', side: 'left' },
  { slotId: 'upper-body', label: '上衣', side: 'left' },
  { slotId: 'belt', label: '腰带', side: 'left' },
  { slotId: 'lower-body', label: '下衣', side: 'left' },
  { slotId: 'feet', label: '鞋子', side: 'left' },
  { slotId: 'face', label: '面部', side: 'right' },
  { slotId: 'back', label: '背部', side: 'right' },
  { slotId: 'hands', label: '手部', side: 'right' },
  { slotId: 'leg', label: '腿部', side: 'right' },
  { slotId: 'left-hand', label: '左手', side: 'right', shape: 'wide' },
  { slotId: 'right-hand', label: '右手', side: 'right', shape: 'wide' }
])
const characterName = ref('当前整备人员')
const entranceActive = ref(true)
let entranceTimer: number | undefined
const activeItemMenu = ref<ItemMenuShowData | null>(null)
const activeItemDetail = ref<ItemDetailData | null>(null)
let pendingItemMenuRequestId = ''
let itemMenuRequestSerial = 0
let itemDetailPanelDrag: {
  panel: HTMLElement
  startMouseX: number
  startMouseY: number
  startPanelLeft: number
  startPanelTop: number
  panelWidth: number
  panelHeight: number
} | null = null
const detailPanelPointerOwner =
  `item-detail-panel-${Math.random().toString(36).slice(2)}`

const scenes = computed<SceneOption[]>(() => [
  { id: 'haven', code: '01', ...copy.value.scenes.haven },
  { id: 'underground', code: '02', ...copy.value.scenes.underground }
])

const locations = computed<Record<SceneId, LocationOption[]>>(() => ({
  haven: [
    { id: 'hall', code: 'A1', label: copy.value.locations.hall },
    { id: 'kitchen', code: 'A2', label: copy.value.locations.kitchen },
    { id: 'office', code: 'A3', label: copy.value.locations.office }
  ],
  underground: [
    { id: 'command-room', code: 'B1', label: copy.value.locations.commandRoom },
    { id: 'readiness-room', code: 'B2', label: copy.value.locations.readinessRoom },
    { id: 'squad-room', code: 'B3', label: copy.value.locations.squadRoom },
    { id: 'commander-office', code: 'B4', label: copy.value.locations.commanderOffice }
  ]
}))

const activeLocation = computed(() => activeLocations[activeScene.value])
const showTimeControl = computed(() => activeScene.value === 'underground' && activeLocation.value === 'command-room')
const showInventory = computed(() => activeScene.value === 'underground' && activeLocation.value === 'readiness-room')
const showCommanderOffice = computed(() => activeScene.value === 'underground' && activeLocation.value === 'commander-office')

function emitToUnreal(event: string, payload: unknown): boolean {
  const host = window as H5UIWindow
  const handlerFunction = INVENTORY_HANDLER_FUNCTIONS[event]
  if (handlerFunction && host.H5UI?.emit) {
    return host.H5UI.emit(INVENTORY_HANDLER_EVENT_TYPE, handlerFunction, JSON.stringify(payload))
  }
  return false
}

function createSceneRoomPayload(scene: SceneId, room: LocationId) {
  return {
    scene,
    room,
    location: room,
    sceneName: scenes.value.find(item => item.id === scene)?.label || scene,
    roomName: locations.value[scene].find(item => item.id === room)?.label || room
  }
}

function emitHavenSceneSwitch(scene: SceneId): void {
  const host = window as H5UIWindow
  host.H5UI?.emit?.(HAVEN_HANDLER_EVENT_TYPE, HAVEN_SCENE_SWITCH_FUNCTION, scene)
}

function emitHavenUndergroundRoomRequest(location: LocationId): void {
  const handlerFunction = HAVEN_ROOM_FUNCTIONS[location]
  if (!handlerFunction) return

  const host = window as H5UIWindow
  host.H5UI?.emit?.(HAVEN_HANDLER_EVENT_TYPE, handlerFunction)
}

function selectScene(scene: SceneId): void {
  const defaultRoom = DEFAULT_LOCATIONS[scene]
  activeScene.value = scene
  activeLocations[scene] = defaultRoom
  emitHavenSceneSwitch(scene)
  if (scene === 'underground') emitHavenUndergroundRoomRequest(defaultRoom)
}

function selectLocation(location: LocationId): void {
  activeLocations[activeScene.value] = location
  if (activeScene.value === 'underground') emitHavenUndergroundRoomRequest(location)
}

function openBaseMenu(): void {
  emitToUnreal('OpenBaseMenuRequested', createSceneRoomPayload(activeScene.value, activeLocation.value))
}

function togglePause(): void {
  paused.value = !paused.value
  speed.value = paused.value ? 0 : 1
  emitTimeControl()
}

function cycleSpeed(): void {
  const currentIndex = fastSpeeds.indexOf(speed.value)
  speed.value = fastSpeeds[(currentIndex + 1) % fastSpeeds.length]
  paused.value = false
  emitTimeControl()
}

function emitTimeControl(): void {
  emitToUnreal('TimeControlChanged', {
    paused: paused.value,
    speed: paused.value ? 0 : speed.value
  })
}

function applyInventoryData(data: InventoryPanelData, target: Required<InventoryPanelData>): void {
  if (typeof data.inventoryId === 'string') target.inventoryId = data.inventoryId
  if (typeof data.inventoryComponentId === 'string') {
    target.inventoryComponentId = data.inventoryComponentId
  }
  if (typeof data.presentationSlot === 'string') target.presentationSlot = data.presentationSlot
  if (typeof data.inventoryViewId === 'string') target.inventoryViewId = data.inventoryViewId
  if (typeof data.regionRole === 'string') target.regionRole = data.regionRole
  if (typeof data.inventoryName === 'string') target.inventoryName = data.inventoryName
  if (typeof data.revision === 'number') target.revision = data.revision
  if (typeof data.totalPrice === 'number') target.totalPrice = data.totalPrice
  if (typeof data.totalWeight === 'number') target.totalWeight = data.totalWeight
  if (Array.isArray(data.containers)) target.containers = data.containers
  if (Array.isArray(data.items)) target.items = data.items
}

function selectInventoryItem(selection: InventorySelection): void {
  if (inventoryDragController.isSelectionSuppressed()) return
  const item = selection.item
  emitToUnreal('InventoryItemSelected', {
    inventoryId: selection.inventoryId,
    region: selection.region,
    itemId: item.itemId,
    containerIndex: item.containerIndex,
    row: item.row,
    column: item.column
  })
}

function inventoryDataById(inventoryId: string, itemId?: string): Required<InventoryPanelData> | null {
  if (inventory.inventoryId === inventoryId &&
      (!itemId || inventory.items.some(item => item.itemId === itemId))) return inventory
  if (characterInventory.inventoryId === inventoryId &&
      (!itemId || characterInventory.items.some(item => item.itemId === itemId))) return characterInventory
  return null
}

function inventoryDataBySlot(slot: string): Required<InventoryPanelData> | null {
  switch (slot) {
    case 'inventory':
    case 'player':
    case 'playerInventory':
      return inventory
    case 'character':
    case 'characterInventory':
      return characterInventory
    default:
      return null
  }
}

const inventoryDragController = new SISH5UIInventoryDragController({
  emitToUnreal,
  resolveInventory: inventoryDataById,
  resolveBaseSlotSize: () => ({
    width: scaledSlotWidth.value,
    height: scaledSlotHeight.value
  }),
  resolveEquipmentItem(equipmentSlotId) {
    const slot = equipmentSlots.find(candidate => `equipment-${candidate.slotId}` === equipmentSlotId)
    if (!slot?.itemId || !slot.inventoryId) return null
    const item: InventoryItemData = {
      itemId: slot.itemId,
      displayName: slot.itemName || slot.label,
      itemType: slot.itemType,
      containerIndex: 0,
      row: 0,
      column: 0,
      stackSize: slot.stackSize || 1,
      rowSpan: slot.rowSpan || 1,
      columnSpan: slot.columnSpan || 1,
      iconWidth: slot.iconWidth,
      iconHeight: slot.iconHeight,
      iconAspectRatio: slot.iconAspectRatio,
      icon: slot.icon
    }
    return {
      inventoryId: slot.inventoryId,
      inventoryComponentId:
        slot.inventoryComponentId || characterInventory.inventoryComponentId || slot.inventoryId,
      presentationSlot:
        slot.presentationSlot || characterInventory.presentationSlot || 'characterInventory',
      item
    }
  }
})

function selectEquipmentSlot(slot: EquipmentSlotData): void {
  emitToUnreal('EquipmentSlotSelected', {
    characterName: characterName.value,
    slotId: slot.slotId,
    side: slot.side,
    itemId: slot.itemId || '',
    itemName: slot.itemName || ''
  })
}

async function notifyEquipmentSlotsCreated(): Promise<void> {
  await nextTick()
  const slots = equipmentSlots
    .map<EquipmentSlotElementData | null>(slot => {
      const elementId = `equipment-${slot.slotId}`
      return document.getElementById(elementId)
        ? { slotId: slot.slotId, elementId, side: slot.side }
        : null
    })
    .filter((slot): slot is EquipmentSlotElementData => slot !== null)

  // This handler accepts TArray<FSISH5UI_EquipmentSlotElementConfig>, so pass
  // the JavaScript array itself. emitToUnreal intentionally stringifies payloads
  // for the inventory protocol's FString handlers and must not be used here.
  const host = window as H5UIWindow
  const handlerFunction = INVENTORY_HANDLER_FUNCTIONS['SISH5UI.Inventory.EquipmentSlotsCreated']
  if (handlerFunction && host.H5UI?.emit) {
    host.H5UI.emit(INVENTORY_HANDLER_EVENT_TYPE, handlerFunction, slots)
  }
}

function setLanguage(nextLanguage: Language): void {
  language.value = nextLanguage
  document.querySelector('html')?.setAttribute('lang', copy.value.htmlLanguage)
  const title = document.querySelector('title')
  if (title) title.textContent = copy.value.documentTitle
}

function readEventDetail(event: Event): unknown {
  const raw = (event as CustomEvent).detail
  if (typeof raw !== 'string') return raw
  try {
    return JSON.parse(raw)
  } catch {
    return raw
  }
}

function applyState(nextState: BaseHudState): void {
  if (nextState.language === 'zh-CN' || nextState.language === 'en-US') setLanguage(nextState.language)
  if (nextState.scene === 'haven' || nextState.scene === 'underground') activeScene.value = nextState.scene
  if (nextState.location && locations.value[activeScene.value].some(item => item.id === nextState.location)) {
    activeLocations[activeScene.value] = nextState.location
  }
  if (typeof nextState.currency === 'number') currency.value = nextState.currency
  if (typeof nextState.currencyDelta === 'number') currencyDelta.value = nextState.currencyDelta
  if (typeof nextState.currencyIcon === 'string' && nextState.currencyIcon.length > 0) {
    currencyIcon.value = nextState.currencyIcon
  }
  if (typeof nextState.paused === 'boolean') paused.value = nextState.paused
  if (typeof nextState.speed === 'number') speed.value = nextState.speed
  if (nextState.time) {
    if (typeof nextState.time.month === 'number') time.month = nextState.time.month
    if (typeof nextState.time.day === 'number') time.day = nextState.time.day
    if (typeof nextState.time.hour === 'number') time.hour = nextState.time.hour
    if (typeof nextState.time.minute === 'number') time.minute = nextState.time.minute
  }
  if (nextState.inventory) applyInventoryData(nextState.inventory, inventory)
  if (nextState.characterInventory) applyInventoryData(nextState.characterInventory, characterInventory)
  if (Array.isArray(nextState.equipmentSlots)) {
    equipmentSlots.splice(0, equipmentSlots.length, ...nextState.equipmentSlots)
  }
  if (typeof nextState.characterName === 'string') characterName.value = nextState.characterName
}

function handleStateEvent(event: Event): void {
  const detail = readEventDetail(event)
  if (detail && typeof detail === 'object') applyState(detail as BaseHudState)
}

function handleLanguageEvent(event: Event): void {
  const detail = readEventDetail(event)
  const nextLanguage = typeof detail === 'string'
    ? detail
    : (detail as { language?: string } | null)?.language
  if (nextLanguage === 'zh-CN' || nextLanguage === 'en-US') setLanguage(nextLanguage)
}

type InventoryBindingEventData = {
  protocolVersion?: number
  slot?: string
  presentationSlot?: string
  inventoryId?: string
  inventoryComponentId?: string
  inventoryViewId?: string
  regionRole?: string
}

function normalizePresentationSlot(value: unknown): InventoryPresentationSlot | null {
  switch (value) {
    case 'inventory':
    case 'player':
    case 'playerInventory':
      return 'inventory'
    case 'character':
    case 'characterInventory':
      return 'characterInventory'
    default:
      return null
  }
}

function inventoryDataByViewId(inventoryViewId: string): Required<InventoryPanelData> | null {
  if (inventoryViewId === INVENTORY_VIEW_IDS.inventory) return inventory
  if (inventoryViewId === INVENTORY_VIEW_IDS.characterInventory) return characterInventory
  return null
}

function resetInventoryContents(target: Required<InventoryPanelData>): void {
  target.inventoryName = ''
  target.revision = 0
  target.totalPrice = 0
  target.totalWeight = 0
  target.containers = []
  target.items = []
}

function setInventoryRegionIdentity(
  target: Required<InventoryPanelData>,
  slot: InventoryPresentationSlot,
  inventoryComponentId: string,
  inventoryViewId = INVENTORY_VIEW_IDS[slot],
  regionRole = INVENTORY_REGION_ROLES[slot]
): void {
  target.inventoryId = inventoryComponentId
  target.inventoryComponentId = inventoryComponentId
  target.presentationSlot = slot
  target.inventoryViewId = inventoryViewId
  target.regionRole = regionRole
}

function notifyInventoryViewCreated(
  slot: InventoryPresentationSlot,
  inventoryComponentId = ''
): void {
  emitToUnreal('SISH5UI.Inventory.ViewCreated', {
    protocolVersion: 1,
    inventoryViewId: INVENTORY_VIEW_IDS[slot],
    slot,
    presentationSlot: slot,
    regionRole: INVENTORY_REGION_ROLES[slot],
    ...(inventoryComponentId
      ? {
          inventoryId: inventoryComponentId,
          inventoryComponentId
        }
      : {})
  })
}

function notifyInventoryViewsCreated(): void {
  notifyInventoryViewCreated('inventory', inventory.inventoryComponentId)
  notifyInventoryViewCreated('characterInventory', characterInventory.inventoryComponentId)
}

function notifyInventoryViewsDestroyed(): void {
  for (const slot of Object.keys(INVENTORY_VIEW_IDS) as InventoryPresentationSlot[]) {
    const target = inventoryDataBySlot(slot)
    emitToUnreal('SISH5UI.Inventory.ViewDestroyed', {
      protocolVersion: 1,
      inventoryViewId: INVENTORY_VIEW_IDS[slot],
      slot,
      presentationSlot: slot,
      inventoryId: target?.inventoryId || '',
      inventoryComponentId: target?.inventoryComponentId || '',
      regionRole: target?.regionRole || INVENTORY_REGION_ROLES[slot]
    })
  }
}

function handleInventoryBindingAvailable(event: Event): void {
  const payload = readEventDetail(event) as InventoryBindingEventData | null
  if (payload?.protocolVersion !== 1) return
  const slot = normalizePresentationSlot(payload.presentationSlot || payload.slot)
  if (!slot) return

  const target = inventoryDataBySlot(slot)
  const inventoryComponentId = payload.inventoryComponentId || payload.inventoryId || ''
  if (!target || !inventoryComponentId) return

  resetInventoryContents(target)
  if (slot === 'characterInventory') resetOperatedUnitPresentation()
  setInventoryRegionIdentity(
    target,
    slot,
    inventoryComponentId,
    INVENTORY_VIEW_IDS[slot],
    payload.regionRole || INVENTORY_REGION_ROLES[slot]
  )
  notifyInventoryViewCreated(slot, inventoryComponentId)
}

function handleInventoryViewBound(event: Event): void {
  const payload = readEventDetail(event) as InventoryBindingEventData | null
  if (payload?.protocolVersion !== 1 || !payload.inventoryViewId) return
  const target = inventoryDataByViewId(payload.inventoryViewId)
  const slot = normalizePresentationSlot(payload.presentationSlot || payload.slot)
  if (!target || !slot || payload.inventoryViewId !== INVENTORY_VIEW_IDS[slot]) return

  const inventoryComponentId = payload.inventoryComponentId || payload.inventoryId || ''
  if (!inventoryComponentId) return
  if (target.inventoryComponentId && target.inventoryComponentId !== inventoryComponentId) {
    resetInventoryContents(target)
    if (slot === 'characterInventory') resetOperatedUnitPresentation()
  }
  setInventoryRegionIdentity(
    target,
    slot,
    inventoryComponentId,
    payload.inventoryViewId,
    payload.regionRole || INVENTORY_REGION_ROLES[slot]
  )
}

function handleInventoryViewUnbound(event: Event): void {
  const payload = readEventDetail(event) as InventoryBindingEventData | null
  if (payload?.protocolVersion !== 1 || !payload.inventoryViewId) return
  const target = inventoryDataByViewId(payload.inventoryViewId)
  const slot = normalizePresentationSlot(payload.presentationSlot || payload.slot)
  if (!target || !slot || payload.inventoryViewId !== INVENTORY_VIEW_IDS[slot]) return

  const unboundInventoryId = payload.inventoryComponentId || payload.inventoryId || ''
  if (unboundInventoryId &&
      target.inventoryComponentId &&
      target.inventoryComponentId !== unboundInventoryId) {
    return
  }
  resetInventoryContents(target)
  setInventoryRegionIdentity(target, slot, '', payload.inventoryViewId)
  if (slot === 'characterInventory') resetOperatedUnitPresentation()
}

function handleInventoryEvent(event: Event): void {
  const detail = readEventDetail(event)
  if (!detail || typeof detail !== 'object') return
  const payload = detail as InventoryBindingEventData & {
    inventory?: InventoryPanelData
  }

  if (payload.protocolVersion !== 1 || !payload.inventory) return
  const slot = normalizePresentationSlot(payload.presentationSlot || payload.slot)
  const target = payload.inventoryViewId
    ? inventoryDataByViewId(payload.inventoryViewId)
    : (slot ? inventoryDataBySlot(slot) : null)
  if (!target || (slot && payload.inventoryViewId &&
      payload.inventoryViewId !== INVENTORY_VIEW_IDS[slot])) {
    return
  }

  const targetSlot = slot ?? normalizePresentationSlot(target.presentationSlot)
  if (!targetSlot) return
  const inventoryComponentId =
    payload.inventoryComponentId ||
    payload.inventory.inventoryComponentId ||
    payload.inventory.inventoryId ||
    ''
  if (target.inventoryComponentId &&
      inventoryComponentId &&
      target.inventoryComponentId !== inventoryComponentId) {
    return
  }
  applyInventoryData(payload.inventory, target)
  setInventoryRegionIdentity(
    target,
    targetSlot,
    inventoryComponentId || target.inventoryId,
    payload.inventoryViewId || INVENTORY_VIEW_IDS[targetSlot],
    payload.regionRole || INVENTORY_REGION_ROLES[targetSlot]
  )
}

function handleInventoryLayoutConfiguration(event: Event): void {
  const detail = readEventDetail(event) as {
    protocolVersion?: number
    baseSlotWidth?: number
    baseSlotHeight?: number
    referenceViewportHeight?: number
  } | null
  if (detail?.protocolVersion !== 1) return

  const width = Number(detail.baseSlotWidth)
  const height = Number(detail.baseSlotHeight)
  const referenceHeight = Number(detail.referenceViewportHeight)
  if (Number.isFinite(width) && width > 0) baseSlotWidth.value = width
  if (Number.isFinite(height) && height > 0) baseSlotHeight.value = height
  if (Number.isFinite(referenceHeight) && referenceHeight > 0) {
    referenceViewportHeight.value = referenceHeight
  }
  viewportHeight.value = window.innerHeight > 1
    ? window.innerHeight
    : referenceViewportHeight.value
}

function handleInventoryViewportResize(): void {
  if (window.innerHeight <= 1) return
  const nextViewportHeight = window.innerHeight
  if (nextViewportHeight === viewportHeight.value) return
  viewportHeight.value = nextViewportHeight
}

const activeEquipmentViewId = ref('')

function resetEquipmentSlots(): void {
  for (const slot of equipmentSlots) {
    const element = document.getElementById(`equipment-${slot.slotId}`)
    element?.removeAttribute('data-item-id')
    element?.removeAttribute('data-inventory-id')
    element?.removeAttribute('data-inventory-component-id')
    element?.removeAttribute('data-presentation-slot')
    element?.removeAttribute('data-inventory-view-id')
    element?.removeAttribute('data-region-role')
    element?.removeAttribute('data-equipment-view-id')
    slot.itemId = undefined
    slot.itemName = undefined
    slot.itemType = undefined
    slot.icon = undefined
    slot.stackSize = undefined
    slot.rowSpan = undefined
    slot.columnSpan = undefined
    slot.iconWidth = undefined
    slot.iconHeight = undefined
    slot.iconAspectRatio = undefined
    slot.inventoryId = undefined
    slot.inventoryComponentId = undefined
    slot.presentationSlot = undefined
    slot.regionRole = undefined
    slot.equipmentViewId = undefined
    slot.equipmentSlotType = undefined
    slot.subSlotIndex = undefined
    slot.blocked = false
    slot.requiresAllSlotsInGroup = false
  }
}

function resetOperatedUnitPresentation(): void {
  activeEquipmentViewId.value = ''
  resetEquipmentSlots()
  resetInventoryContents(characterInventory)
}

function syncEquipmentDomIdentity(): void {
  for (const slot of equipmentSlots) {
    const element = document.getElementById(`equipment-${slot.slotId}`)
    if (!element) continue
    const inventoryComponentId =
      slot.inventoryComponentId || characterInventory.inventoryComponentId || slot.inventoryId
    if (slot.equipmentViewId) {
      element.setAttribute('data-equipment-view-id', slot.equipmentViewId)
    }
    if (slot.inventoryId) element.setAttribute('data-inventory-id', slot.inventoryId)
    if (inventoryComponentId) {
      element.setAttribute('data-inventory-component-id', inventoryComponentId)
    }
    if (characterInventory.inventoryViewId) {
      element.setAttribute('data-inventory-view-id', characterInventory.inventoryViewId)
    }
    element.setAttribute(
      'data-presentation-slot',
      slot.presentationSlot || characterInventory.presentationSlot || 'characterInventory'
    )
    element.setAttribute(
      'data-region-role',
      slot.regionRole || characterInventory.regionRole || INVENTORY_REGION_ROLES.characterInventory
    )
  }
}

function handleEquipmentEvent(event: Event): void {
  const detail = readEventDetail(event) as {
    protocolVersion?: number
    equipmentViewId?: string
    presentationSlot?: string
    inventoryComponentId?: string
    regionRole?: string
    equipment?: EquipmentSnapshotData
  } | null
  if (detail?.protocolVersion !== 1 || !detail.equipmentViewId || !detail.equipment) return
  const inventoryComponentId =
    detail.inventoryComponentId || detail.equipment.inventoryId
  if (characterInventory.inventoryComponentId &&
      characterInventory.inventoryComponentId !== inventoryComponentId) {
    return
  }

  resetEquipmentSlots()
  resetInventoryContents(characterInventory)
  activeEquipmentViewId.value = detail.equipmentViewId
  setInventoryRegionIdentity(
    characterInventory,
    'characterInventory',
    inventoryComponentId,
    characterInventory.inventoryViewId || INVENTORY_VIEW_IDS.characterInventory,
    detail.regionRole || INVENTORY_REGION_ROLES.characterInventory
  )
  characterInventory.inventoryName = detail.equipment.inventoryName || ''
  characterInventory.revision = detail.equipment.revision
  characterInventory.totalPrice = Number(detail.equipment.totalPrice) || 0
  const hasAuthoritativeTotalWeight =
    typeof detail.equipment.totalWeight === 'number' &&
    Number.isFinite(detail.equipment.totalWeight)
  characterInventory.totalWeight = hasAuthoritativeTotalWeight
    ? Number(detail.equipment.totalWeight)
    : 0

  const containerByKey = new Map<
    string,
    NonNullable<InventoryPanelData['containers']>[number]
  >()
  const itemById = new Map<string, InventoryItemData>()
  const aggregatedRootItemIds = new Set<string>()
  // Every HTML equipment container belongs to this view, including currently
  // empty or temporarily unconfigured slots. Slot-specific snapshot entries
  // below add type, sub-slot and item state where available.
  for (const slot of equipmentSlots) {
    slot.equipmentViewId = detail.equipmentViewId
    slot.inventoryId = detail.equipment.inventoryId
    slot.inventoryComponentId = inventoryComponentId
    slot.presentationSlot = detail.presentationSlot || 'characterInventory'
    slot.regionRole = detail.regionRole || INVENTORY_REGION_ROLES.characterInventory
  }
  for (const snapshotSlot of detail.equipment.slots) {
    const slot = equipmentSlots.find(
      candidate => `equipment-${candidate.slotId}` === snapshotSlot.equipmentSlotId
    )
    if (!slot) continue
    slot.equipmentSlotType = snapshotSlot.equipmentSlotType
    slot.subSlotIndex = snapshotSlot.subSlotIndex
    slot.blocked = snapshotSlot.blocked === true || snapshotSlot.bBlocked === true
    slot.requiresAllSlotsInGroup =
      snapshotSlot.requiresAllSlotsInGroup === true ||
      snapshotSlot.bRequiresAllSlotsInGroup === true
    slot.itemId = snapshotSlot.itemId || undefined
    slot.itemName = snapshotSlot.displayName || undefined
    slot.itemType = snapshotSlot.itemType || undefined
    slot.stackSize = snapshotSlot.stackSize
    slot.rowSpan = snapshotSlot.rowSpan
    slot.columnSpan = snapshotSlot.columnSpan
    slot.iconWidth = snapshotSlot.iconWidth
    slot.iconHeight = snapshotSlot.iconHeight
    slot.iconAspectRatio = snapshotSlot.iconAspectRatio
    slot.icon = snapshotSlot.icon || undefined

    if ((snapshotSlot.isInventoryContainer === true ||
          snapshotSlot.bIsInventoryContainer === true) &&
        snapshotSlot.itemId &&
        !aggregatedRootItemIds.has(snapshotSlot.itemId) &&
        Array.isArray(snapshotSlot.inventoryContainers) &&
        snapshotSlot.inventoryContainers.length > 0) {
      aggregatedRootItemIds.add(snapshotSlot.itemId)
      for (const container of snapshotSlot.inventoryContainers) {
        const key = `${container.parentItemId || ''}:${container.containerIndex}`
        if (!containerByKey.has(key)) containerByKey.set(key, container)
      }
      for (const item of snapshotSlot.inventoryItems ?? []) {
        if (!itemById.has(item.itemId)) itemById.set(item.itemId, item)
      }
    }
  }
  characterInventory.containers = [...containerByKey.values()]
  characterInventory.items = [...itemById.values()]
  if (!hasAuthoritativeTotalWeight) {
    const ownerWeightByItemId = new Map<string, number>()
    for (const container of characterInventory.containers) {
      const parentItemId = container.parentItemId || ''
      if (!parentItemId || ownerWeightByItemId.has(parentItemId)) continue
      ownerWeightByItemId.set(parentItemId, Number(container.ownerWeight) || 0)
    }
    characterInventory.totalWeight = [...ownerWeightByItemId.values()]
      .reduce((total, weight) => total + weight, 0)
  }
  syncEquipmentDomIdentity()
  void nextTick(syncEquipmentDomIdentity)
}

function handleEquipmentViewCleared(event: Event): void {
  const detail = readEventDetail(event) as { protocolVersion?: number, equipmentViewId?: string } | null
  if (detail?.protocolVersion !== 1 ||
      !detail.equipmentViewId ||
      detail.equipmentViewId !== activeEquipmentViewId.value) return
  resetOperatedUnitPresentation()
}

function closeItemMenu(): void {
  activeItemMenu.value = null
}

function handleInventoryItemMouseDown(event: MouseEvent): void {
  const eventTarget = event.target
  console.log('[SISH5UI ItemMenu] mousedown', {
    button: event.button,
    target: eventTarget instanceof Element
      ? `${eventTarget.tagName}.${eventTarget.className}`
      : String(eventTarget)
  })
  if (!(eventTarget instanceof Element)) {
    console.warn('[SISH5UI ItemMenu] ignored: event target is not an Element')
    closeItemMenu()
    return
  }

  const detailHeader = eventTarget.closest('.item-detail-header')
  const detailCloseButton = eventTarget.closest('.item-detail-close')
  if (event.button === 0 && detailHeader && !detailCloseButton) {
    const detailPanel = detailHeader.closest('.item-detail-panel') as HTMLElement | null
    if (detailPanel) {
      lockItemDetailPanelPosition(detailPanel)
      const bounds = detailPanel.getBoundingClientRect()
      itemDetailPanelDrag = {
        panel: detailPanel,
        startMouseX: event.clientX,
        startMouseY: event.clientY,
        startPanelLeft: bounds.left,
        startPanelTop: bounds.top,
        panelWidth: bounds.width,
        panelHeight: bounds.height
      }
      if (!sisH5UIPointerSessions.begin(detailPanelPointerOwner, event, {
        move: handleItemDetailPanelMouseMove,
        end: (_event, reason) => finishItemDetailPanelDrag(reason)
      })) {
        itemDetailPanelDrag = null
        return
      }
      document.documentElement?.classList.add('item-detail-drag-active')
      console.log('[SISH5UI ItemDetail] panel drag begin', {
        clientX: event.clientX,
        clientY: event.clientY,
        panelLeft: bounds.left,
        panelTop: bounds.top,
        panelWidth: bounds.width,
        panelHeight: bounds.height
      })
      event.preventDefault()
      event.stopPropagation()
      return
    }
  }

  if (eventTarget.closest('.inventory-context-menu')) {
    console.log('[SISH5UI ItemMenu] menu element interaction')
    return
  }

  if (event.button !== ITEM_MENU_MOUSE_BUTTON) {
    closeItemMenu()
    return
  }

  const itemElement = eventTarget.closest(
    '.inventory-item, .equipment-slot, .item-detail-attachment-slot.occupied'
  ) as HTMLElement | null
  const identityElement = itemElement?.closest('[data-inventory-component-id]') as HTMLElement | null
  const itemId = itemElement?.dataset.itemId || ''
  const inventoryComponentId = identityElement?.dataset.inventoryComponentId || ''
  const presentationSlot = identityElement?.dataset.presentationSlot || ''
  if (!itemElement || !itemId || !inventoryComponentId) {
    if (event.button === ITEM_MENU_MOUSE_BUTTON) {
      console.warn('[SISH5UI ItemMenu] ignored: missing DOM identity', {
        hasItemElement: !!itemElement,
        itemId,
        inventoryComponentId,
        presentationSlot
      })
    }
    closeItemMenu()
    return
  }

  event.preventDefault()
  event.stopPropagation()
  closeItemMenu()
  pendingItemMenuRequestId = `${Date.now()}-${++itemMenuRequestSerial}`
  const emitted = emitToUnreal('SISH5UI.ItemMenu.Request', {
    protocolVersion: 1,
    requestId: pendingItemMenuRequestId,
    itemId,
    inventoryComponentId,
    presentationSlot,
    clientX: event.clientX,
    clientY: event.clientY
  })
  console.log('[SISH5UI ItemMenu] request emitted', {
    emitted,
    requestId: pendingItemMenuRequestId,
    itemId,
    inventoryComponentId,
    presentationSlot,
    clientX: event.clientX,
    clientY: event.clientY
  })
}

function lockItemDetailPanelPosition(panel: HTMLElement): void {
  if (panel.dataset.positionLocked === 'true') return
  const dimensions = panel.getBoundingClientRect()
  // RmlUi reports left/top before CSS translate(), so the initial centered
  // visual position must be derived from the viewport and panel dimensions.
  const centeredLeft = Math.max(0, (window.innerWidth - dimensions.width) * 0.5)
  const centeredTop = Math.max(0, (window.innerHeight - dimensions.height) * 0.5)
  panel.style.transform = 'none'
  panel.style.left = `${centeredLeft}px`
  panel.style.top = `${centeredTop}px`
  panel.dataset.positionLocked = 'true'
}

function handleItemDetailPanelMouseMove(event: MouseEvent): void {
  if (!itemDetailPanelDrag) return
  const drag = itemDetailPanelDrag
  const maxLeft = Math.max(0, window.innerWidth - drag.panelWidth)
  const maxTop = Math.max(0, window.innerHeight - drag.panelHeight)
  const left = Math.max(
    0,
    Math.min(drag.startPanelLeft + event.clientX - drag.startMouseX, maxLeft)
  )
  const top = Math.max(
    0,
    Math.min(drag.startPanelTop + event.clientY - drag.startMouseY, maxTop)
  )
  const { panel } = drag
  panel.style.left = `${left}px`
  panel.style.top = `${top}px`
  panel.style.transform = 'none'
  event.preventDefault()
}

function finishItemDetailPanelDrag(reason = 'mouseup'): void {
  if (!itemDetailPanelDrag) return
  const bounds = itemDetailPanelDrag.panel.getBoundingClientRect()
  console.log('[SISH5UI ItemDetail] panel drag end', {
    reason,
    panelLeft: bounds.left,
    panelTop: bounds.top
  })
  itemDetailPanelDrag = null
  document.documentElement?.classList.remove('item-detail-drag-active')
}

function handleItemMenuShow(event: Event): void {
  const detail = readEventDetail(event) as ItemMenuShowData | null
  console.log('[SISH5UI ItemMenu] show response', detail)
  if (!detail ||
      detail.protocolVersion !== 1 ||
      detail.requestId !== pendingItemMenuRequestId ||
      !detail.itemId ||
      !detail.inventoryComponentId ||
      !Array.isArray(detail.entries) ||
      detail.entries.length === 0) {
    console.warn('[SISH5UI ItemMenu] response rejected', {
      pendingRequestId: pendingItemMenuRequestId,
      responseRequestId: detail?.requestId,
      protocolVersion: detail?.protocolVersion,
      entryCount: Array.isArray(detail?.entries) ? detail.entries.length : -1
    })
    closeItemMenu()
    return
  }

  const menuWidth = 176
  const menuHeight = 12 + detail.entries.length * 35
  detail.clientX = Math.max(0, Math.min(detail.clientX, window.innerWidth - menuWidth))
  detail.clientY = Math.max(0, Math.min(detail.clientY, window.innerHeight - menuHeight))
  activeItemMenu.value = detail
  console.log('[SISH5UI ItemMenu] menu opened', {
    x: detail.clientX,
    y: detail.clientY,
    entryCount: detail.entries.length
  })
}

function chooseItemMenuEntry(entry: ItemMenuEntryData): void {
  const menu = activeItemMenu.value
  if (!menu || entry.enabled === false || !entry.functionName) return

  const emitted = emitToUnreal('SISH5UI.ItemMenu.Action', {
    protocolVersion: 1,
    requestId: menu.requestId,
    itemId: menu.itemId,
    inventoryComponentId: menu.inventoryComponentId,
    presentationSlot: menu.presentationSlot || '',
    functionName: entry.functionName
  })
  console.log('[SISH5UI ItemMenu] action emitted', {
    emitted,
    itemId: menu.itemId,
    functionName: entry.functionName
  })
  closeItemMenu()
}

function handleItemMenuKeyDown(event: KeyboardEvent): void {
  if (event.key !== 'Escape') return
  if (activeItemDetail.value) {
    closeItemDetail()
    return
  }
  closeItemMenu()
}

function handleItemDetailShow(event: Event): void {
  const detail = readEventDetail(event) as ItemDetailData | null
  console.log('[SISH5UI ItemDetail] show response', detail)
  if (!detail ||
      detail.protocolVersion !== 1 ||
      !detail.sessionId ||
      !detail.itemId ||
      !Array.isArray(detail.properties) ||
      !Array.isArray(detail.attachmentSlots)) return
  console.log('[SISH5UI ItemDetail] layout parameters', {
    previewTexture: detail.previewTexture,
    previewWidth: detail.previewWidth,
    previewHeight: detail.previewHeight,
    interactivePreview: detail.interactivePreview,
    baseSlotWidth: scaledSlotWidth.value,
    baseSlotHeight: scaledSlotHeight.value,
    layoutScale: inventoryLayoutScale.value,
    attachmentSlots: detail.attachmentSlots.map(slot => ({
      slotIndex: slot.slotIndex,
      name: slot.name,
      column: slot.column,
      row: slot.row,
      width: slot.width,
      height: slot.height
    }))
  })
  closeItemMenu()
  activeItemDetail.value = detail
  // RmlUi can paint a translated fixed element at its visual position while
  // retaining the pre-transform hit-test geometry. Resolve the centered
  // position into explicit viewport coordinates before the first interaction
  // so the close button and title bar are clickable where they are rendered.
  void nextTick(() => {
    const detailPanel = document.querySelector('.item-detail-panel') as HTMLElement | null
    if (detailPanel) lockItemDetailPanelPosition(detailPanel)
  })
}

function rotateItemDetail(deltaX: number, deltaY: number): void {
  const detail = activeItemDetail.value
  if (!detail?.interactivePreview) return
  emitToUnreal('SISH5UI.ItemDetail.Rotate', {
    protocolVersion: 1,
    sessionId: detail.sessionId,
    deltaX,
    deltaY
  })
}

function zoomItemDetail(delta: number): void {
  const detail = activeItemDetail.value
  if (!detail?.interactivePreview) return
  emitToUnreal('SISH5UI.ItemDetail.Zoom', {
    protocolVersion: 1,
    sessionId: detail.sessionId,
    delta
  })
}

function closeItemDetail(): void {
  const detail = activeItemDetail.value
  if (!detail) return
  sisH5UIPointerSessions.end(detailPanelPointerOwner, 'owner-release')
  finishItemDetailPanelDrag('detail-close')
  activeItemDetail.value = null
  emitToUnreal('SISH5UI.ItemDetail.Close', {
    protocolVersion: 1,
    sessionId: detail.sessionId
  })
}

onMounted(() => {
  sisH5UIPointerSessions.retain(window)
  window.addEventListener(STATE_EVENT_NAME, handleStateEvent)
  window.addEventListener(LANGUAGE_EVENT_NAME, handleLanguageEvent)
  window.addEventListener(PAGE_DATA_EVENT_NAME, handleStateEvent)
  window.addEventListener(INVENTORY_EVENT_NAME, handleInventoryEvent)
  window.addEventListener(INVENTORY_BINDING_AVAILABLE_EVENT_NAME, handleInventoryBindingAvailable)
  window.addEventListener(INVENTORY_VIEW_BOUND_EVENT_NAME, handleInventoryViewBound)
  window.addEventListener(INVENTORY_VIEW_UNBOUND_EVENT_NAME, handleInventoryViewUnbound)
  window.addEventListener(INVENTORY_LAYOUT_CONFIGURATION_EVENT_NAME, handleInventoryLayoutConfiguration)
  window.addEventListener(EQUIPMENT_EVENT_NAME, handleEquipmentEvent)
  window.addEventListener(EQUIPMENT_CLEARED_EVENT_NAME, handleEquipmentViewCleared)
  window.addEventListener(EQUIPMENT_SLOT_CONFIGURATION_REQUEST_EVENT_NAME, notifyEquipmentSlotsCreated)
  window.addEventListener(ITEM_MENU_SHOW_EVENT_NAME, handleItemMenuShow)
  window.addEventListener(ITEM_DETAIL_SHOW_EVENT_NAME, handleItemDetailShow)
  window.addEventListener('mousedown', handleInventoryItemMouseDown, true)
  window.addEventListener('keydown', handleItemMenuKeyDown)
  window.addEventListener('resize', handleInventoryViewportResize)
  document.documentElement?.addEventListener('resize', handleInventoryViewportResize)
  inventoryDragController.mount()
  void nextTick(() => {
    notifyInventoryViewsCreated()
    void notifyEquipmentSlotsCreated()
  })
  const initialPageData = (window as H5UIWindow).ue?.getData?.('pageData')
  if (initialPageData) {
    const parsedPageData = readEventDetail({ detail: initialPageData } as CustomEvent)
    if (parsedPageData && typeof parsedPageData === 'object') applyState(parsedPageData as BaseHudState)
  }
  entranceTimer = window.setTimeout(() => {
    entranceActive.value = false
    entranceTimer = undefined
  }, 900)
  emitToUnreal('BaseHUDReady', {
    scene: activeScene.value,
    location: activeLocation.value,
    language: language.value
  })
})

onUnmounted(() => {
  notifyInventoryViewsDestroyed()
  window.removeEventListener(STATE_EVENT_NAME, handleStateEvent)
  window.removeEventListener(LANGUAGE_EVENT_NAME, handleLanguageEvent)
  window.removeEventListener(PAGE_DATA_EVENT_NAME, handleStateEvent)
  window.removeEventListener(INVENTORY_EVENT_NAME, handleInventoryEvent)
  window.removeEventListener(INVENTORY_BINDING_AVAILABLE_EVENT_NAME, handleInventoryBindingAvailable)
  window.removeEventListener(INVENTORY_VIEW_BOUND_EVENT_NAME, handleInventoryViewBound)
  window.removeEventListener(INVENTORY_VIEW_UNBOUND_EVENT_NAME, handleInventoryViewUnbound)
  window.removeEventListener(INVENTORY_LAYOUT_CONFIGURATION_EVENT_NAME, handleInventoryLayoutConfiguration)
  window.removeEventListener(EQUIPMENT_EVENT_NAME, handleEquipmentEvent)
  window.removeEventListener(EQUIPMENT_CLEARED_EVENT_NAME, handleEquipmentViewCleared)
  window.removeEventListener(EQUIPMENT_SLOT_CONFIGURATION_REQUEST_EVENT_NAME, notifyEquipmentSlotsCreated)
  window.removeEventListener(ITEM_MENU_SHOW_EVENT_NAME, handleItemMenuShow)
  window.removeEventListener(ITEM_DETAIL_SHOW_EVENT_NAME, handleItemDetailShow)
  window.removeEventListener('mousedown', handleInventoryItemMouseDown, true)
  window.removeEventListener('keydown', handleItemMenuKeyDown)
  window.removeEventListener('resize', handleInventoryViewportResize)
  document.documentElement?.removeEventListener('resize', handleInventoryViewportResize)
  inventoryDragController.destroy()
  sisH5UIPointerSessions.releaseHost(detailPanelPointerOwner)
  closeItemDetail()
  if (entranceTimer !== undefined) window.clearTimeout(entranceTimer)
})
</script>

<template>
  <main id="base-hud" class="base-hud" :class="{ 'hud-entering': entranceActive }">
    <div class="top-frame"></div>
    <div class="top-signal" aria-hidden="true"><span></span><i></i></div>
    <div class="corner corner-left-top"></div>
    <div class="corner corner-right-top"></div>
    <div class="edge-telemetry edge-telemetry-left" aria-hidden="true"><i></i><i></i><i></i><i></i></div>
    <div class="edge-telemetry edge-telemetry-right" aria-hidden="true"><i></i><i></i><i></i><i></i></div>
    <div class="edge-stream edge-stream-left" aria-hidden="true"><i></i><i></i></div>
    <div class="edge-stream edge-stream-right" aria-hidden="true"><i></i><i></i></div>

    <SceneSwitcher
      :scenes="scenes"
      :active-scene="activeScene"
      @select="selectScene"
    />

    <LocationRail
      :key="activeScene"
      :locations="locations[activeScene]"
      :active-location="activeLocation"
      :current-label="copy.currentRoom"
      @select="selectLocation"
    />

    <div class="resource-panel hud-panel">
      <CurrencyPanel
        :heading="copy.currency.heading"
        :currency-name="copy.currency.name"
        :amount="currency"
        :icon-source="currencyIcon"
      />

      <MenuLauncher
        :label="copy.menu.heading"
        @open="openBaseMenu"
      />
    </div>

    <TimeControl
      v-show="showTimeControl"
      :time="time"
      :paused="paused"
      :speed="speed"
      :month-label="copy.time.month"
      :day-label="copy.time.day"
      :time-label="copy.time.heading"
      :paused-label="copy.time.paused"
      :running-label="copy.time.running"
      :pause-hint="copy.time.pauseHint"
      :fast-hint="copy.time.fastHint"
      @toggle-pause="togglePause"
      @cycle-speed="cycleSpeed"
    />

    <InventoryPanel
      v-show="showInventory"
      class="readiness-inventory"
      :player-data="inventory"
      :character-data="characterInventory"
      :equipment-slots="equipmentSlots"
      :character-name="characterName"
      :slot-width="scaledSlotWidth"
      :slot-height="scaledSlotHeight"
      :layout-scale="inventoryLayoutScale"
      @select-item="selectInventoryItem"
      @select-equipment="selectEquipmentSlot"
    />

    <InventoryContextMenu
      v-if="activeItemMenu"
      :entries="activeItemMenu.entries"
      :x="activeItemMenu.clientX"
      :y="activeItemMenu.clientY"
      @choose="chooseItemMenuEntry"
    />

    <InventoryItemDetailPanel
      v-if="activeItemDetail"
      :detail="activeItemDetail"
      :base-slot-width="scaledSlotWidth"
      :base-slot-height="scaledSlotHeight"
      :layout-scale="inventoryLayoutScale"
      @close="closeItemDetail"
      @rotate="rotateItemDetail"
      @zoom="zoomItemDetail"
    />

    <div id="base-overlay-host" class="overlay-host" :data-label="copy.status.overlayReady">
      <iframe
        v-show="showCommanderOffice"
        id="commander-office-frame"
        class="commander-office-frame"
        src="coui://uiresources/CommanderOS/V2/commander-os2.html"
        title="Commander Operating System"
      ></iframe>
    </div>
  </main>
</template>

<style>
@keyframes hud-enter {
  0% { opacity: 0; transform: translateY(8px); }
  100% { opacity: 1; transform: translateY(0); }
}

@keyframes signal-pulse {
  0% { opacity: 0.35; }
  100% { opacity: 1; }
}

@keyframes identity-enter {
  0% { opacity: 0; transform: translateX(-24px); }
  100% { opacity: 1; transform: translateX(0); }
}

@keyframes panel-enter-left {
  0% { opacity: 0; transform: translateX(-36px); }
  65% { opacity: 1; transform: translateX(5px); }
  100% { opacity: 1; transform: translateX(0); }
}

@keyframes panel-enter-right {
  0% { opacity: 0; transform: translateX(36px); }
  65% { opacity: 1; transform: translateX(-5px); }
  100% { opacity: 1; transform: translateX(0); }
}

@keyframes panel-enter-bottom {
  0% { opacity: 0; transform: translateY(28px); }
  65% { opacity: 1; transform: translateY(-4px); }
  100% { opacity: 1; transform: translateY(0); }
}

@keyframes signal-travel {
  0% { opacity: 0; transform: translateX(-100px); }
  10% { opacity: 1; }
  88% { opacity: 0.75; }
  100% { opacity: 0; transform: translateX(1680px); }
}

@keyframes signal-travel-reverse {
  0% { opacity: 0; transform: translateX(80px); }
  12% { opacity: 0.7; }
  100% { opacity: 0; transform: translateX(-1680px); }
}

@keyframes telemetry-step {
  0% { opacity: 0.18; transform: scaleX(0.45); }
  100% { opacity: 0.8; transform: scaleX(1); }
}

@keyframes edge-stream-drift {
  0% { opacity: 0; transform: translateY(-62px); }
  18% { opacity: 0.7; }
  78% { opacity: 0.28; }
  100% { opacity: 0; transform: translateY(250px); }
}

@keyframes switch-drift {
  0% { opacity: 0.45; transform: translateX(-2px); }
  100% { opacity: 1; transform: translateX(2px); }
}

@keyframes scene-switch-confirm {
  0% { border-color: #347da4; background-color: rgba(5, 17, 26, 0.86); }
  34% { border-color: #a5e4ff; background-color: rgba(19, 65, 89, 0.94); }
  100% { border-color: #347da4; background-color: rgba(5, 17, 26, 0.86); }
}

@keyframes scene-switch-scan {
  0% { transform: translateX(-140%); opacity: 0; }
  18% { opacity: 0.78; }
  82% { opacity: 0.42; }
  100% { transform: translateX(430%); opacity: 0; }
}

@keyframes scene-switch-text-out {
  0% { opacity: 1; transform: translateX(0); }
  56% { opacity: 0.42; transform: translateX(-7px); }
  100% { opacity: 0; transform: translateX(-17px); }
}

@keyframes scene-switch-text-in {
  0% { opacity: 0; transform: translateX(20px); }
  44% { opacity: 0.52; transform: translateX(7px); }
  100% { opacity: 1; transform: translateX(0); }
}

@keyframes scene-switch-target-out {
  0% { opacity: 1; transform: translateX(0); }
  100% { opacity: 0; transform: translateX(10px); }
}

@keyframes room-code-pulse {
  0% { opacity: 0.52; }
  100% { opacity: 1; }
}

@keyframes corner-breathe {
  0% { opacity: 0.35; }
  100% { opacity: 0.95; }
}

@keyframes bar-scan {
  0% { opacity: 0.4; transform: scaleY(0.45); }
  100% { opacity: 1; transform: scaleY(1); }
}

@keyframes scene-selected {
  0% { transform: translateX(2px); border-color: #246185; }
  55% { transform: translateX(12px); border-color: #8bdcff; }
  100% { transform: translateX(8px); border-color: #4aa3d4; }
}

@keyframes active-beacon {
  0% { opacity: 0.4; transform: scaleY(0.55); }
  100% { opacity: 1; transform: scaleY(1); }
}

@keyframes rail-rebuild {
  0% { opacity: 0; transform: translateX(-18px); }
  45% { opacity: 0.45; transform: translateX(5px); }
  100% { opacity: 1; transform: translateX(0); }
}

@keyframes target-lock {
  0% { opacity: 0.5; transform: rotate(45deg) scale(0.45); }
  55% { opacity: 1; transform: rotate(45deg) scale(1.65); }
  100% { opacity: 1; transform: rotate(45deg) scale(1); }
}

@keyframes amount-signal {
  0% { opacity: 0.3; transform: translateY(5px); }
  55% { opacity: 1; transform: translateY(-2px); }
  100% { opacity: 1; transform: translateY(0); }
}

@keyframes currency-pulse {
  0% { opacity: 0.62; transform: scale(0.9); }
  100% { opacity: 1; transform: scale(1.04); }
}

@keyframes clock-refresh {
  0% { opacity: 0.4; transform: translateY(4px); }
  100% { opacity: 1; transform: translateY(0); }
}

@keyframes fast-echo {
  0% { opacity: 0.45; transform: translateX(-2px); }
  100% { opacity: 1; transform: translateX(3px); }
}

@keyframes dropdown-front-enter {
  0% { opacity: 0; transform: translateX(-16px); }
  100% { opacity: 1; transform: translateX(0); }
}

@keyframes peek-breathe {
  0% { opacity: 0.34; }
  100% { opacity: 0.54; }
}

@keyframes dropdown-open {
  0% { opacity: 0; transform: translateY(-10px); }
  100% { opacity: 1; transform: translateY(0); }
}

@keyframes menu-enter {
  0% { opacity: 0; transform: translateX(30px); }
  70% { opacity: 1; transform: translateX(-5px); }
  100% { opacity: 1; transform: translateX(0); }
}

@keyframes menu-signal {
  0% { opacity: 0.3; transform: scaleY(0.5); }
  100% { opacity: 1; transform: scaleY(1); }
}

* { box-sizing: border-box; }
html, body, #app { width: 100%; height: 100%; margin: 0; }
html, body, #app { pointer-events: none; }
html.item-detail-drag-active,
html.item-detail-drag-active body,
html.item-detail-drag-active #app,
html.item-detail-drag-active .base-hud,
html.item-detail-interaction-active,
html.item-detail-interaction-active body,
html.item-detail-interaction-active #app,
html.item-detail-interaction-active .base-hud {
  pointer-events: auto;
}
body {
  overflow: hidden;
  color: #dce9f5;
  background-color: transparent;
  font-family: Roboto, Arial, sans-serif;
}
button { font-family: Roboto, Arial, sans-serif; }

.base-hud {
  position: relative;
  width: 100%;
  height: 100%;
  min-width: 960px;
  min-height: 540px;
  overflow: hidden;
  pointer-events: none;
  background-color: transparent;
}
.base-hud.hud-entering { animation: hud-enter 0.4s ease-out 1 both; }

.hud-interactive { pointer-events: auto; }
.hud-panel {
  background-color: rgba(5, 13, 20, 0.78);
  border: 1px solid #214d6d;
  box-shadow: -3px 4px 10px rgba(2, 7, 11, 0.24);
}

.top-frame {
  position: absolute;
  top: 22px;
  left: 28px;
  right: 28px;
  height: 1px;
  background-color: #1b4b6b;
  opacity: 0.7;
}

.top-signal {
  position: absolute;
  top: 19px;
  left: 58px;
  right: 58px;
  height: 7px;
  overflow: hidden;
  opacity: 0.8;
}
.top-signal span, .top-signal i { position: absolute; display: block; top: 2px; width: 92px; height: 2px; background-color: #65c9f2; }
.top-signal span { left: 0; animation: signal-travel 5.8s linear infinite; }
.top-signal i { right: 0; animation: signal-travel-reverse 7.2s linear 1.4s infinite; }

.corner { position: absolute; width: 36px; height: 36px; top: 22px; border-top: 2px solid #3b91c1; }
.corner-left-top { left: 28px; border-left: 2px solid #3b91c1; }
.corner-right-top { right: 28px; border-right: 2px solid #3b91c1; }
.corner { animation: corner-breathe 1.6s ease-in-out infinite alternate; }

.edge-telemetry { position: absolute; top: 112px; width: 22px; height: 116px; }
.edge-telemetry-left { left: 20px; }
.edge-telemetry-right { right: 20px; }
.edge-telemetry i { display: block; width: 13px; height: 2px; margin-bottom: 13px; background-color: #3f93bd; transform-origin: left center; animation: telemetry-step 0.75s ease-in-out infinite alternate; }
.edge-telemetry-right i { margin-left: 9px; transform-origin: right center; }
.edge-telemetry i:nth-child(2) { animation: telemetry-step 0.75s ease-in-out 0.16s infinite alternate; }
.edge-telemetry i:nth-child(3) { animation: telemetry-step 0.75s ease-in-out 0.32s infinite alternate; }
.edge-telemetry i:nth-child(4) { animation: telemetry-step 0.75s ease-in-out 0.48s infinite alternate; }

.edge-stream {
  position: absolute;
  top: 268px;
  width: 2px;
  height: 250px;
  overflow: hidden;
  pointer-events: none;
  opacity: 0.55;
}
.edge-stream-left { left: 27px; }
.edge-stream-right { right: 27px; }
.edge-stream i {
  position: absolute;
  top: 0;
  display: block;
  width: 2px;
  height: 62px;
  background-color: #5bb8de;
  animation: edge-stream-drift 4.8s linear infinite;
}
.edge-stream i:nth-child(2) { top: 92px; height: 34px; animation: edge-stream-drift 6.2s linear 1.8s infinite; }

.scene-switcher {
  position: absolute;
  left: 48px;
  top: 42px;
  z-index: 30;
  width: 286px;
  height: 68px;
}
.hud-entering .scene-switcher { animation: panel-enter-left 0.52s ease-out 0.08s 1 both; }
.scene-direct-button {
  position: relative;
  width: 100%;
  height: 68px;
  padding: 0 8px 0 0;
  display: flex;
  align-items: center;
  overflow: hidden;
  text-align: left;
  color: #e1f2fb;
  background-color: rgba(5, 17, 26, 0.86);
  border: 1px solid #347da4;
  box-shadow: -2px 3px 7px rgba(28, 105, 143, 0.12);
}
.scene-direct-button:hover, .scene-direct-button:focus {
  border-color: #6dc3e9;
  background-color: rgba(9, 31, 45, 0.92);
  transform: translateX(3px);
}
.scene-direct-button:active { transform: translateX(1px); }
.scene-direct-button.switching {
  pointer-events: none;
  animation: scene-switch-confirm 0.36s ease-out;
}
.scene-direct-button.switching .scene-switch-icon { visibility: hidden; }
.scene-switch-scan {
  position: absolute;
  z-index: 2;
  top: 0;
  left: 0;
  width: 28%;
  height: 100%;
  pointer-events: none;
  background-color: rgba(128, 214, 248, 0.28);
  opacity: 0;
}
.scene-direct-button.switching .scene-switch-scan { animation: scene-switch-scan 0.36s ease-out; }
.scene-direct-button.switching .scene-code,
.scene-direct-button.switching .scene-copy,
.scene-direct-button.switching .scene-switch-target {
  position: absolute;
  z-index: 3;
}
.scene-direct-button.switching .scene-code { top: 29px; left: 0; width: 43px; }
.scene-direct-button.switching .scene-copy { top: 11px; left: 43px; right: 100px; }
.scene-direct-button.switching .scene-switch-target { top: 17px; right: 31px; }
.scene-direct-button.switching .scene-code-out,
.scene-direct-button.switching .scene-copy-out { animation: scene-switch-text-out 0.24s ease-in both; }
.scene-direct-button.switching .scene-code-in,
.scene-direct-button.switching .scene-copy-in { animation: scene-switch-text-in 0.28s ease-out 0.08s both; }
.scene-direct-button.switching .scene-switch-target-out { animation: scene-switch-target-out 0.22s ease-in both; }
.panel-tonal-shift {
  position: absolute;
  top: 0;
  right: 0;
  width: 54%;
  height: 100%;
  display: flex;
  pointer-events: none;
}
.panel-tonal-shift i { flex: 1; height: 100%; }
.panel-tonal-shift i:first-child { background-color: rgba(24, 71, 92, 0.08); }
.panel-tonal-shift i:nth-child(2) { background-color: rgba(24, 71, 92, 0.16); }
.panel-tonal-shift i:nth-child(3) { background-color: rgba(24, 71, 92, 0.25); }
.scene-direct-button:hover .panel-tonal-shift i:first-child,
.location-current:hover .panel-tonal-shift i:first-child { background-color: rgba(34, 100, 128, 0.12); }
.scene-direct-button:hover .panel-tonal-shift i:nth-child(2),
.location-current:hover .panel-tonal-shift i:nth-child(2) { background-color: rgba(34, 100, 128, 0.22); }
.scene-direct-button:hover .panel-tonal-shift i:nth-child(3),
.location-current:hover .panel-tonal-shift i:nth-child(3) { background-color: rgba(34, 100, 128, 0.32); }
.panel-heading {
  height: 44px;
  padding: 0 5px 10px;
  display: flex;
  align-items: flex-end;
  justify-content: space-between;
  border-bottom: 1px solid #1b4663;
}
.panel-heading strong { color: #b4cedc; font-size: 14px; letter-spacing: 1.8px; font-weight: 500; }
.heading-index { color: #438db7; font-size: 9px; letter-spacing: 1.3px; }
.scene-dropdown-stage { position: relative; height: 88px; margin-top: 9px; }
.scene-peek-card {
  position: absolute;
  z-index: 1;
  top: 10px;
  left: 60px;
  width: 248px;
  height: 68px;
  padding: 0;
  display: flex;
  align-items: center;
  text-align: left;
  color: #5f798a;
  background-color: #030a10;
  border: 1px solid #15364b;
  animation: peek-breathe 1.7s ease-in-out infinite alternate;
}
.scene-peek-card > span { position: absolute; top: 10px; right: 13px; color: #397b9f; font-size: 9px; }
.scene-peek-card > strong { position: absolute; top: 28px; right: 12px; color: #7894a4; font-size: 11px; letter-spacing: 0.5px; font-weight: 500; }
.scene-peek-card > i { position: absolute; top: 13px; right: 4px; width: 3px; height: 42px; background-color: #245b78; }
.scene-peek-card:hover, .scene-peek-card:focus { color: #9fc4d8; border-color: #377b9e; background-color: #07151e; }
.scene-peek-card:hover > strong, .scene-peek-card:focus > strong { color: #9fc4d8; }
.scene-dropdown-toggle {
  position: absolute;
  z-index: 2;
  top: 0;
  left: 0;
  width: 252px;
  height: 76px;
  padding: 0;
  display: flex;
  align-items: center;
  text-align: left;
  color: #e1f2fb;
  background-color: #0d2a3d;
  border: 1px solid #4aa3d4;
  box-shadow: -2px 3px 7px rgba(28, 105, 143, 0.16);
}
.hud-entering .scene-dropdown-toggle { animation: dropdown-front-enter 0.28s ease-out 1 both; }
.scene-dropdown-toggle:hover, .scene-dropdown-toggle:focus { border-color: #76c9ed; background-color: #11364d; transform: translateX(4px); }
.scene-dropdown-toggle:active { transform: translateX(1px); }
.scene-code { width: 43px; text-align: center; color: #55a5cc; font-size: 10px; letter-spacing: 0.8px; }
.scene-copy { flex: 1; display: block; }
.scene-copy strong { display: block; font-size: 19px; letter-spacing: 2.5px; font-weight: 500; }
.scene-copy small { display: block; margin-top: 5px; color: #658ca1; font-size: 9px; letter-spacing: 1px; }
.scene-switch-target {
  width: 76px;
  padding-right: 7px;
  text-align: right;
  border-left: 1px solid #245774;
}
.scene-switch-target small { display: block; color: #4387aa; font-size: 8px; letter-spacing: 0.8px; }
.scene-switch-target strong { display: block; margin-top: 4px; color: #8eb1c2; font-size: 11px; letter-spacing: 0.7px; font-weight: 500; }
.scene-switch-icon { width: 24px; color: #68bfe5; font-size: 20px; text-align: center; animation: switch-drift 1.35s ease-in-out infinite alternate; }
.scene-dropdown-arrow { width: 26px; color: #75c8ed; font-size: 22px; text-align: center; transition: transform 0.16s; }
.scene-dropdown-arrow.open { transform: rotate(180deg); }
.scene-indicator { width: 3px; height: 46px; margin-right: 7px; background-color: #59b7e6; animation: active-beacon 0.7s ease-in-out infinite alternate; }
.scene-dropdown-menu { position: absolute; z-index: 30; top: 145px; left: 13px; right: 13px; padding: 7px; background-color: #040c12; border: 1px solid #2b6688; box-shadow: -3px 4px 9px rgba(1, 7, 11, 0.34); animation: dropdown-open 0.2s ease-out; }
.scene-dropdown-option { width: 100%; height: 59px; margin-bottom: 5px; padding: 0 10px; display: flex; align-items: center; text-align: left; color: #718d9d; background-color: #06131c; border: 1px solid #173d54; }
.scene-dropdown-option:last-child { margin-bottom: 0; }
.scene-dropdown-option > span { width: 38px; color: #4384a4; font-size: 9px; }
.scene-dropdown-option > div { flex: 1; }
.scene-dropdown-option > div strong { display: block; color: #86a1b0; font-size: 14px; letter-spacing: 1.2px; font-weight: 500; }
.scene-dropdown-option > div small { display: block; margin-top: 4px; color: #4d7387; font-size: 9px; letter-spacing: 0.6px; }
.scene-dropdown-option > i { width: 6px; height: 6px; border: 1px solid #3a7797; transform: rotate(45deg); }
.scene-dropdown-option:hover, .scene-dropdown-option:focus { border-color: #4c9dc3; background-color: #0a2230; }
.scene-dropdown-option:hover > div strong, .scene-dropdown-option:focus > div strong { color: #cce7f3; }
.scene-dropdown-option.active { border-color: #5cb5dc; background-color: #0b293a; }
.scene-dropdown-option.active > div strong { color: #e0f4fc; }
.scene-dropdown-option.active > i { background-color: #65c6ea; border-color: #8bdcf7; }

.location-selector {
  position: absolute;
  left: 48px;
  top: 118px;
  z-index: 25;
  width: 250px;
}
.hud-entering .location-selector { animation: rail-rebuild 0.4s ease-out 0.06s 1 both; }
.location-current {
  position: relative;
  width: 100%;
  height: 92px;
  padding: 0 10px 0 14px;
  display: flex;
  align-items: center;
  overflow: hidden;
  text-align: left;
  color: #d9eef8;
  background-color: rgba(4, 15, 23, 0.88);
  border: 1px solid #2f6f93;
}
.location-current:hover, .location-current:focus {
  border-color: #61b5dc;
  background-color: rgba(8, 27, 39, 0.92);
}
.location-current-copy { flex: 1; display: block; }
.location-current-copy small { display: block; color: #5ba1c5; font-size: 10px; letter-spacing: 1.2px; }
.location-current-copy strong { display: block; margin-top: 10px; color: #d8ecf6; font-size: 18px; letter-spacing: 1.8px; font-weight: 500; }
.location-current-meta { width: 48px; padding-right: 6px; text-align: right; border-right: 1px solid #275b78; }
.location-current-meta small { display: block; color: #477e9c; font-size: 8px; letter-spacing: 1px; }
.location-current-meta strong { display: block; margin-top: 6px; color: #75a9c2; font-size: 10px; letter-spacing: 0.8px; font-weight: 500; animation: room-code-pulse 1.6s ease-in-out infinite alternate; }
.location-current-arrow { width: 24px; color: #65b9df; font-size: 20px; text-align: center; transition: transform 0.16s; }
.location-current-arrow.open { transform: rotate(180deg); }
.location-menu {
  position: absolute;
  z-index: 40;
  top: 92px;
  left: 0;
  width: 250px;
  padding: 7px;
  display: none;
  background-color: rgba(3, 11, 17, 0.96);
  border: 1px solid #2b6688;
  box-shadow: -3px 4px 10px rgba(1, 7, 11, 0.38);
  animation: dropdown-open 0.2s ease-out;
}
.location-selector:hover .location-menu,
.location-selector.open .location-menu,
.location-menu.visible { display: block; }

.location-rail {
  position: absolute;
  z-index: 26;
  top: 42px;
  left: 348px;
  right: 388px;
  height: 62px;
  padding: 0;
  display: flex;
  align-items: center;
  border-top: 1px solid #163e57;
  border-bottom: 1px solid #163e57;
  background-color: rgba(3, 13, 20, 0.62);
  overflow: hidden;
}
.hud-entering .location-rail { animation: rail-rebuild 0.4s ease-out 1 both; }
.location-button {
  position: relative;
  flex: 1 1 0;
  min-width: 0;
  height: 44px;
  margin-left: 0;
  padding: 0 8px;
  display: flex;
  align-items: center;
  justify-content: center;
  text-align: center;
  color: #638095;
  background-color: rgba(3, 15, 23, 0.72);
  border: 1px solid #173f57;
  overflow: hidden;
}
.location-button > span {
  position: absolute;
  left: 10px;
  color: #3d7b9f;
  font-size: 9px;
}
.location-button strong {
  max-width: 100%;
  min-width: 0;
  font-size: 12px;
  letter-spacing: 1px;
  font-weight: 500;
  white-space: nowrap;
  overflow: hidden;
}
.location-button i {
  position: absolute;
  right: 10px;
  width: 5px;
  height: 5px;
  border: 1px solid #306d91;
  transform: rotate(45deg);
}
.location-button:hover, .location-button:focus { color: #bdd9e8; background-color: #0a2230; border-color: #4c9dc3; }
.location-button.active { color: #d9f0fb; background-color: #0b293a; border-color: #5cb5dc; }
.location-button.active i { background-color: #58b6e5; border-color: #8ad9fa; animation: target-lock 0.38s ease-out; }
.location-button:active strong { transform: translateX(2px); }
.location-button strong { transition: transform 0.12s; }

.resource-panel {
  position: absolute;
  top: 42px;
  right: 48px;
  width: 322px;
  height: 64px;
  display: flex;
  align-items: center;
}
.hud-entering .resource-panel { animation: panel-enter-right 0.52s ease-out 0.12s 1 both; }
.currency-panel { flex: 1; height: 62px; padding: 0 10px 0 12px; display: flex; align-items: center; }
.currency-icon-frame { width: 40px; height: 40px; margin-right: 12px; display: flex; align-items: center; justify-content: center; border: 1px solid #2e6a8d; background-color: rgba(7, 24, 35, 0.56); }
.currency-icon { display: block; width: 29px; height: 29px; }
.currency-icon-fallback { color: #82d1f1; font-size: 26px; }
.currency-copy { flex: 1; text-align: right; }
.currency-copy small { display: block; color: #6f99b0; font-size: 11px; letter-spacing: 0.7px; }
.currency-copy strong { display: block; margin-top: 4px; color: #e1f1f8; font-size: 24px; letter-spacing: 1.5px; font-weight: 400; animation: amount-signal 0.42s ease-out; }

.time-control {
  position: absolute;
  top: 114px;
  right: 48px;
  width: 322px;
  height: 64px;
  padding: 0 8px 0 10px;
  display: flex;
  align-items: center;
}
.hud-entering .time-control { animation: panel-enter-right 0.56s ease-out 0.08s 1 both; }
.time-control.paused { border-color: #486a7f; background-color: rgba(7, 16, 25, 0.84); }
.time-summary { flex: 1; padding-right: 8px; }
.time-state-line { height: 16px; display: flex; align-items: center; }
.time-pulse { width: 6px; height: 6px; margin-right: 6px; border-radius: 50%; background-color: #48a9d8; box-shadow: 0 0 3px rgba(72, 169, 216, 0.55); animation: signal-pulse 0.8s ease-in-out infinite alternate; }
.paused .time-pulse { background-color: #6c7780; box-shadow: none; animation: none; }
.time-state-line small { color: #579abe; font-size: 9px; letter-spacing: 0.5px; }
.time-state-line strong { margin-left: 7px; color: #a8c3d1; font-size: 10px; letter-spacing: 0.5px; font-weight: 500; }
.compact-calendar { height: 32px; display: flex; align-items: center; }
.compact-calendar > span { width: 42px; display: flex; align-items: baseline; }
.compact-calendar > span strong { color: #dceef7; font-size: 18px; font-weight: 400; }
.compact-calendar > span small { margin-left: 3px; color: #678da3; font-size: 9px; }
.compact-calendar > i { width: 1px; height: 18px; margin: 0 5px 0 1px; background-color: #1b435e; }
.clock-value { flex: 1; text-align: right; color: #e2f2fa; font-size: 21px; letter-spacing: 2px; font-weight: 400; animation: clock-refresh 0.32s ease-out; }
.time-actions { height: 44px; padding-left: 7px; display: flex; align-items: center; border-left: 1px solid #1c4560; }
.time-actions button {
  height: 38px;
  color: #8db5ca;
  background-color: rgba(8, 23, 34, 0.78);
  border: 1px solid #285876;
  transition: transform 0.14s, border-color 0.14s, background-color 0.14s;
}
.time-actions button:hover, .time-actions button:focus { transform: translateY(-3px); border-color: #5bb2dd; background-color: #0d2637; }
.time-actions button:active { transform: translateY(1px); }
.time-actions > button:first-child { width: 34px; display: flex; align-items: center; justify-content: center; }
.play-icon { width: 0; height: 0; margin-left: 2px; border-top: 5px solid transparent; border-bottom: 5px solid transparent; border-left: 8px solid #8fd4f4; }
.pause-icon { display: flex; }
.pause-icon i { display: block; width: 3px; height: 12px; margin: 0 2px; background-color: #8fd4f4; }
.fast-button { width: 48px; margin-left: 5px; display: flex; align-items: center; justify-content: center; }
.fast-arrows { margin-right: 3px; color: #5da3c7; font-size: 16px; }
.accelerated .fast-arrows { color: #76caf1; animation: fast-echo 0.42s ease-in-out infinite alternate; }
.fast-button strong { color: #c8e3ef; font-size: 10px; letter-spacing: 0.4px; }

.menu-button {
  position: relative;
  width: 64px;
  height: 46px;
  padding: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  color: #79c6e7;
  background-color: transparent;
  border: 0 solid transparent;
  border-left: 1px solid #285876;
}
.menu-button:hover, .menu-button:focus { background-color: rgba(19, 60, 82, 0.52); }
.menu-button:active { background-color: rgba(28, 82, 108, 0.65); }
.menu-button-icon { width: 34px; height: 34px; padding: 7px 6px; display: flex; flex-direction: column; align-items: center; justify-content: center; }
.menu-button-icon i { display: block; width: 23px; height: 2px; margin: 3px 0; background-color: #79c9e9; }

.overlay-host {
  position: absolute;
  z-index: 100;
  top: 0;
  right: 0;
  bottom: 0;
  left: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  pointer-events: none;
}

.commander-office-frame {
  display: block;
  width: 72%;
  height: 68%;
  background-color: #02070b;
  border: 1px solid #2f6f93;
  pointer-events: auto;
}

@media (max-height: 760px) {
  .scene-switcher { top: 28px; }
  .location-selector { top: 102px; }
  .resource-panel { top: 28px; }
  .time-control { top: 98px; }
}
</style>
