export type InventoryGridData = {
  parentItemId?: string
  ownerDisplayName?: string
  ownerWeight?: number
  containerIndex: number
  columns: number
  rows: number
  layoutColumn?: number
  layoutRow?: number
}

export type InventoryItemData = {
  parentItemId?: string
  itemId: string
  displayName: string
  itemType?: string
  containerIndex: number
  row: number
  column: number
  rowSpan: number
  columnSpan: number
  bRotated?: boolean
  rotated?: boolean
  bUseSceneCapture?: boolean
  useSceneCapture?: boolean
  stackSize?: number
  maxStackSize?: number
  iconWidth?: number
  iconHeight?: number
  iconAspectRatio?: number
  icon?: string
}

export type InventoryPanelData = {
  inventoryId?: string
  inventoryComponentId?: string
  presentationSlot?: string
  inventoryViewId?: string
  regionRole?: string
  inventoryName?: string
  revision?: number
  totalPrice?: number
  totalWeight?: number
  containers?: InventoryGridData[]
  items?: InventoryItemData[]
}

export type InventoryRegion = 'player' | 'character'

export type InventorySelection = {
  inventoryId: string
  region: InventoryRegion
  item: InventoryItemData
}

export type ItemMenuEntryData = {
  menuText: string
  functionName: string
  enabled?: boolean
}

export type ItemMenuShowData = {
  protocolVersion: number
  requestId: string
  itemId: string
  inventoryComponentId: string
  presentationSlot?: string
  clientX: number
  clientY: number
  entries: ItemMenuEntryData[]
}

export type ItemDetailPropertyData = {
  name: string
  value: string
  unit?: string
  modified?: boolean
}

export type ItemDetailAttachmentSlotData = {
  slotIndex: number
  name?: string
  column: number
  row: number
  width: number
  height: number
  magazineSlot?: boolean
  slotTexture?: string
  itemId?: string
  itemName?: string
  itemIcon?: string
  stackSize?: number
}

export type ItemDetailData = {
  protocolVersion: number
  sessionId: string
  itemId: string
  inventoryId?: string
  inventoryComponentId?: string
  presentationSlot?: string
  displayName: string
  description?: string
  itemType?: string
  previewTexture?: string
  previewWidth?: number
  previewHeight?: number
  interactivePreview?: boolean
  supportsAttachments?: boolean
  stackSize?: number
  maxStackSize?: number
  price?: number
  weight?: number
  properties: ItemDetailPropertyData[]
  attachmentSlots: ItemDetailAttachmentSlotData[]
}

export type EquipmentSlotData = {
  slotId: string
  label: string
  side: 'left' | 'right'
  shape?: 'square' | 'wide'
  icon?: string
  itemId?: string
  itemName?: string
  itemType?: string
  stackSize?: number
  rowSpan?: number
  columnSpan?: number
  iconWidth?: number
  iconHeight?: number
  iconAspectRatio?: number
  equipmentViewId?: string
  inventoryId?: string
  inventoryComponentId?: string
  presentationSlot?: string
  regionRole?: string
  equipmentSlotType?: string
  subSlotIndex?: number
  blocked?: boolean
  requiresAllSlotsInGroup?: boolean
}

export type EquipmentSlotElementData = {
  slotId: string
  elementId: string
  side: 'left' | 'right'
}

export type EquipmentSlotSnapshotData = {
  equipmentSlotId: string
  equipmentSlotType: string
  subSlotIndex: number
  blocked?: boolean
  bBlocked?: boolean
  requiresAllSlotsInGroup?: boolean
  bRequiresAllSlotsInGroup?: boolean
  itemId?: string
  displayName?: string
  itemType?: string
  stackSize?: number
  rowSpan?: number
  columnSpan?: number
  iconWidth?: number
  iconHeight?: number
  iconAspectRatio?: number
  icon?: string
  isInventoryContainer?: boolean
  bIsInventoryContainer?: boolean
  inventoryContainers?: InventoryGridData[]
  inventoryItems?: InventoryItemData[]
}

export type EquipmentSnapshotData = {
  inventoryId: string
  inventoryName?: string
  revision: number
  totalPrice?: number
  totalWeight?: number
  slots: EquipmentSlotSnapshotData[]
}
