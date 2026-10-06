import type { InventoryItemData, InventoryPanelData } from './types'
import { sisH5UIPointerSessions } from './SISH5UIPointerSessionCoordinator'

export const SISH5UI_INVENTORY_DRAG_EVENTS = {
  begin: 'SISH5UI.Drag.Begin',
  end: 'SISH5UI.Drag.End',
  cancel: 'SISH5UI.Drag.Cancel',
  state: 'SISH5UI.Drag.State',
  enter: 'SISH5UI.Drag.Enter',
  over: 'SISH5UI.Drag.Over',
  leave: 'SISH5UI.Drag.Leave',
  probeRequest: 'SISH5UI.Drag.ProbeRequest',
  probeResult: 'SISH5UI.Drag.ProbeResult',
  result: 'SISH5UI.Drag.Result',
  drop: 'SISH5UI.Drag.Drop',
  dropRequest: 'SISH5UI.Drag.DropRequest'
} as const

const SISH5UI_PROTOCOL_VERSION = 1
const SIS_TOP_LEVEL_PARENT_ITEM_ID = '7a2e9d2b-4f0a-4e3a-9b4b-2e7f2e7a9e01'

type UnrealEmitter = (event: string, payload: unknown) => void
type InventoryResolver = (inventoryId: string, itemId?: string) => InventoryPanelData | null
type EquipmentItemResolver = (equipmentSlotId: string) => {
  inventoryId: string
  inventoryComponentId?: string
  presentationSlot?: string
  item: InventoryItemData
} | null
type BaseSlotSizeResolver = () => { width: number, height: number }

export type SISH5UIInventoryDragControllerOptions = {
  emitToUnreal: UnrealEmitter
  resolveInventory: InventoryResolver
  resolveEquipmentItem?: EquipmentItemResolver
  resolveBaseSlotSize?: BaseSlotSizeResolver
  dragThreshold?: number
  selectionSuppressionMilliseconds?: number
}

type DragCandidate = {
  item: InventoryItemData
  inventoryId: string
  inventoryComponentId: string
  presentationSlot: string
  sourceElement: Element
  startX: number
  startY: number
  visualWidth: number
  visualHeight: number
  grabOffsetX: number
  grabOffsetY: number
  active: boolean
}

type ExternalDragDetail = {
  protocolVersion?: number
  sessionId?: string
  itemId?: string
  sourceInventoryId?: string
  clientX?: number
  clientY?: number
  visualWidth?: number
  visualHeight?: number
  grabOffsetX?: number
  grabOffsetY?: number
  rowSpan?: number
  columnSpan?: number
}

type CellQuadrant = 'top-left' | 'top-right' | 'bottom-left' | 'bottom-right'

type InventoryDropTarget = {
  kind: 'inventory'
  gridElement: Element
  inventoryId: string
  inventoryComponentId: string
  presentationSlot: string
  parentItemId: string
  targetItemId: string
  containerIndex: number
  hoverRow: number
  hoverColumn: number
  quadrant: CellQuadrant
}

type EquipmentDropTarget = {
  kind: 'equipment'
  slotElement: Element
  equipmentViewId: string
  equipmentSlotId: string
  inventoryComponentId: string
  presentationSlot: string
  hitMode: 'pointer' | 'overlap'
}

type AttachmentDropTarget = {
  kind: 'attachment'
  slotElement: Element
  inventoryId: string
  inventoryComponentId: string
  presentationSlot: string
  parentItemId: string
  attachmentSlotIndex: number
}

type DropTarget = InventoryDropTarget | EquipmentDropTarget | AttachmentDropTarget

type InventoryDropCandidate = {
  key: string
  sessionId: string
  itemId: string
  target: DropTarget
  previewRow: number
  previewColumn: number
  rowSpan: number
  columnSpan: number
  handleMode?: string
  willReplace?: boolean
  occupiedEquipmentSlotIds?: string[]
  moves?: InventoryMovePreview[]
}

type InventoryMovePreview = {
  itemId?: string
  primaryItem?: boolean
  fromInventoryId?: string
  targetInventoryId?: string
  parentItemId?: string
  containerIndex?: number
  row?: number
  column?: number
  rowSpan?: number
  columnSpan?: number
  rotated?: boolean
  fromEquipmentSlotId?: string
}

type InventoryProbeResult = {
  protocolVersion?: number
  sessionId?: string
  itemId?: string
  targetInventoryId?: string
  targetParentItemId?: string
  targetItemId?: string
  targetContainerIndex?: number
  hoverRow?: number
  hoverColumn?: number
  quadrant?: CellQuadrant
  targetKind?: string
  equipmentViewId?: string
  equipmentSlotId?: string
  targetAttachmentSlotIndex?: number
  targetRow?: number
  targetColumn?: number
  rowSpan?: number
  columnSpan?: number
  accepted?: boolean
  reason?: string
  handleMode?: string
  willReplace?: boolean
  occupiedEquipmentSlotIds?: string[]
  moves?: InventoryMovePreview[]
}

type PreviewState = 'pending' | 'valid' | 'replace' | 'invalid'

const ITEM_SELECTOR = '.inventory-item, .item-detail-attachment-slot.occupied'
const GRID_SELECTOR = '.inventory-grid'
const GRID_CELL_SELECTOR = '.inventory-grid-cell'
const INVENTORY_SELECTOR = '[data-inventory-id]'
const EQUIPMENT_SLOT_SELECTOR = '.equipment-slot'
const ATTACHMENT_SLOT_SELECTOR = '.item-detail-attachment-slot'
const SOURCE_CLASS = 'inventory-item-drag-source'
const PREVIEW_CLASS = 'inventory-grid-cell-drop-preview'
const REPLACEMENT_PREVIEW_CLASS = 'inventory-grid-cell-drop-replacement'
const PREVIEW_STATES: PreviewState[] = ['pending', 'valid', 'replace', 'invalid']

function readEventDetail(event: Event): unknown {
  const raw = (event as CustomEvent).detail
  if (typeof raw !== 'string') return raw
  try {
    return JSON.parse(raw)
  } catch {
    return raw
  }
}

function positiveInteger(value: unknown, fallback = 1): number {
  const parsed = Math.floor(Number(value))
  return Number.isFinite(parsed) && parsed > 0 ? parsed : fallback
}

function integerOr(value: unknown, fallback: number): number {
  const parsed = Math.floor(Number(value))
  return Number.isFinite(parsed) ? parsed : fallback
}

function normalizeParentItemId(value: unknown): string {
  const parentItemId = typeof value === 'string' ? value.toLowerCase() : ''
  return parentItemId === SIS_TOP_LEVEL_PARENT_ITEM_ID ? '' : parentItemId
}

/**
 * Page-independent adapter between an HTML inventory grid and SIS's external
 * drag protocol. The controller owns pointer gesture state and drop previews;
 * inventory authority remains entirely in Unreal.
 */
export class SISH5UIInventoryDragController {
  private readonly emitToUnreal: UnrealEmitter
  private readonly resolveInventory: InventoryResolver
  private readonly resolveEquipmentItem?: EquipmentItemResolver
  private readonly resolveBaseSlotSize?: BaseSlotSizeResolver
  private readonly dragThresholdSquared: number
  private readonly selectionSuppressionMilliseconds: number
  private host: Window | null = null
  private dragCandidate: DragCandidate | null = null
  private previewCandidate: InventoryDropCandidate | null = null
  private previewCells: Element[] = []
  private previewGrids: Element[] = []
  private previewEquipmentSlots: Element[] = []
  private previewAttachmentSlots: Element[] = []
  private suppressSelectionUntil = 0
  private activeSessionId = ''
  private pendingBeginItemId = ''
  private dragOverCount = 0
  private readonly pointerOwner =
    `inventory-drag-${Math.random().toString(36).slice(2)}`

  constructor(options: SISH5UIInventoryDragControllerOptions) {
    this.emitToUnreal = options.emitToUnreal
    this.resolveInventory = options.resolveInventory
    this.resolveEquipmentItem = options.resolveEquipmentItem
    this.resolveBaseSlotSize = options.resolveBaseSlotSize
    const dragThreshold = Math.max(0, options.dragThreshold ?? 6)
    this.dragThresholdSquared = dragThreshold * dragThreshold
    this.selectionSuppressionMilliseconds = Math.max(
      0,
      options.selectionSuppressionMilliseconds ?? 80
    )
  }

  mount(host: Window = window): void {
    if (this.host === host) return
    this.destroy()
    this.host = host
    sisH5UIPointerSessions.retain(host)
    host.addEventListener('mousedown', this.handleMouseDown, true)
    host.addEventListener(SISH5UI_INVENTORY_DRAG_EVENTS.state, this.handleDragState)
    host.addEventListener(SISH5UI_INVENTORY_DRAG_EVENTS.over, this.handleDragOver)
    host.addEventListener(SISH5UI_INVENTORY_DRAG_EVENTS.leave, this.handleDragLeave)
    host.addEventListener(SISH5UI_INVENTORY_DRAG_EVENTS.probeResult, this.handleProbeResult)
    host.addEventListener(SISH5UI_INVENTORY_DRAG_EVENTS.result, this.handleDragResult)
    host.addEventListener(SISH5UI_INVENTORY_DRAG_EVENTS.drop, this.handleDrop)
  }

  destroy(): void {
    if (this.host) {
      this.host.removeEventListener('mousedown', this.handleMouseDown, true)
      this.host.removeEventListener(SISH5UI_INVENTORY_DRAG_EVENTS.state, this.handleDragState)
      this.host.removeEventListener(SISH5UI_INVENTORY_DRAG_EVENTS.over, this.handleDragOver)
      this.host.removeEventListener(SISH5UI_INVENTORY_DRAG_EVENTS.leave, this.handleDragLeave)
      this.host.removeEventListener(SISH5UI_INVENTORY_DRAG_EVENTS.probeResult, this.handleProbeResult)
      this.host.removeEventListener(SISH5UI_INVENTORY_DRAG_EVENTS.result, this.handleDragResult)
      this.host.removeEventListener(SISH5UI_INVENTORY_DRAG_EVENTS.drop, this.handleDrop)
      sisH5UIPointerSessions.releaseHost(this.pointerOwner)
    }
    this.host = null
    this.activeSessionId = ''
    this.pendingBeginItemId = ''
    this.clearDragCandidate()
    this.clearPreview()
  }

  isSelectionSuppressed(now = performance.now()): boolean {
    return now < this.suppressSelectionUntil
  }

  private readonly handleMouseDown = (event: Event): void => {
    const mouseEvent = event as MouseEvent
    if (mouseEvent.button !== 0 ||
        this.activeSessionId ||
        this.pendingBeginItemId) return

    const target = mouseEvent.target as Element | null
    const itemElement = target?.closest?.(`${ITEM_SELECTOR}, ${EQUIPMENT_SLOT_SELECTOR}`)
    const equipmentSlotId = itemElement?.matches(EQUIPMENT_SLOT_SELECTOR)
      ? (itemElement.getAttribute('data-equipment-slot-id') || '')
      : ''
    const equipmentItem = equipmentSlotId
      ? this.resolveEquipmentItem?.(equipmentSlotId)
      : null
    const inventoryElement = equipmentItem ? null : itemElement?.closest?.(INVENTORY_SELECTOR)
    const itemId = itemElement?.getAttribute('data-item-id') || ''
    const inventoryId = equipmentItem?.inventoryId || inventoryElement?.getAttribute('data-inventory-id') || ''
    const inventoryComponentId = equipmentItem?.inventoryComponentId ||
      inventoryElement?.getAttribute('data-inventory-component-id') ||
      inventoryId
    const presentationSlot = equipmentItem?.presentationSlot ||
      inventoryElement?.getAttribute('data-presentation-slot') ||
      ''
    const item = equipmentItem?.item ||
      this.resolveInventory(inventoryId, itemId)?.items?.find(candidate => candidate.itemId === itemId)
    if (!itemElement || !item || !inventoryId || !inventoryComponentId) return

    this.clearPreview()
    const rect = itemElement.getBoundingClientRect()
    const baseSlotSize = this.resolveBaseSlotSize?.()
    const visualWidth = baseSlotSize
      ? Math.max(1, baseSlotSize.width * positiveInteger(item.columnSpan))
      : Math.max(1, rect.width)
    const visualHeight = baseSlotSize
      ? Math.max(1, baseSlotSize.height * positiveInteger(item.rowSpan))
      : Math.max(1, rect.height)
    this.dragCandidate = {
      item,
      inventoryId,
      inventoryComponentId,
      presentationSlot,
      sourceElement: itemElement,
      startX: mouseEvent.clientX,
      startY: mouseEvent.clientY,
      visualWidth,
      visualHeight,
      grabOffsetX: visualWidth * 0.5,
      grabOffsetY: visualHeight * 0.5,
      active: false
    }
    if (!sisH5UIPointerSessions.begin(this.pointerOwner, mouseEvent, {
      move: this.handlePointerMove,
      end: this.handlePointerEnd
    })) {
      this.clearDragCandidate()
    }
  }

  private readonly handlePointerMove = (mouseEvent: MouseEvent): void => {
    if (!this.dragCandidate) return

    // Once Unreal has accepted the drag, drive hit testing directly from the
    // document-level pointer session. An entire quick drag can happen between
    // two game ticks, so relying only on Unreal's tick-dispatched Drag.Over
    // event leaves the preview at its initial cell until mouseup.
    if (this.dragCandidate.active) {
      this.processDragPosition({
        protocolVersion: SISH5UI_PROTOCOL_VERSION,
        sessionId: this.activeSessionId,
        itemId: this.dragCandidate.item.itemId,
        sourceInventoryId: this.dragCandidate.inventoryId,
        clientX: mouseEvent.clientX,
        clientY: mouseEvent.clientY,
        visualWidth: this.dragCandidate.visualWidth,
        visualHeight: this.dragCandidate.visualHeight,
        grabOffsetX: this.dragCandidate.grabOffsetX,
        grabOffsetY: this.dragCandidate.grabOffsetY,
        rowSpan: positiveInteger(this.dragCandidate.item.rowSpan),
        columnSpan: positiveInteger(this.dragCandidate.item.columnSpan)
      })
      return
    }

    const deltaX = mouseEvent.clientX - this.dragCandidate.startX
    const deltaY = mouseEvent.clientY - this.dragCandidate.startY
    if (deltaX * deltaX + deltaY * deltaY < this.dragThresholdSquared) return

    const candidate = this.dragCandidate
    candidate.active = true
    candidate.sourceElement.classList.add(SOURCE_CLASS)
    this.pendingBeginItemId = candidate.item.itemId
    console.warn('[SISH5UI PointerTrace][Drag] threshold', {
      itemId: candidate.item.itemId,
      startX: candidate.startX,
      startY: candidate.startY,
      clientX: mouseEvent.clientX,
      clientY: mouseEvent.clientY
    })
    this.emitToUnreal(SISH5UI_INVENTORY_DRAG_EVENTS.begin, {
      protocolVersion: SISH5UI_PROTOCOL_VERSION,
      itemId: candidate.item.itemId,
      inventoryId: candidate.inventoryId,
      sourceInventoryComponentId: candidate.inventoryComponentId,
      sourcePresentationSlot: candidate.presentationSlot,
      displayName: candidate.item.displayName,
      icon: candidate.item.icon || '',
      stackSize: candidate.item.stackSize || 1,
      iconWidth: candidate.item.iconWidth || 0,
      iconHeight: candidate.item.iconHeight || 0,
      iconAspectRatio: candidate.item.iconAspectRatio || 0,
      rotated: candidate.item.bRotated ?? candidate.item.rotated ?? false,
      useSceneCapture:
        candidate.item.bUseSceneCapture ?? candidate.item.useSceneCapture ?? true,
      visualWidth: candidate.visualWidth,
      visualHeight: candidate.visualHeight,
      grabOffsetX: candidate.grabOffsetX,
      grabOffsetY: candidate.grabOffsetY
    })
  }

  private readonly handlePointerEnd = (): void => {
    if (!this.dragCandidate) return
    if (this.dragCandidate.active) {
      this.suppressSelectionUntil = performance.now() + this.selectionSuppressionMilliseconds
    }
    // Unreal queues this end request and performs placement on its next tick,
    // outside RmlUi's mouseup dispatch.
    if (this.activeSessionId) {
      this.emitToUnreal(SISH5UI_INVENTORY_DRAG_EVENTS.end, {
        protocolVersion: SISH5UI_PROTOCOL_VERSION,
        sessionId: this.activeSessionId,
        itemId: this.dragCandidate.item.itemId
      })
    }
    this.clearDragCandidate()
  }

  private readonly handleDragState = (event: Event): void => {
    const detail = readEventDetail(event) as {
      accepted?: boolean
      sessionId?: string
      itemId?: string
      reason?: string
    } | null
    if (detail?.accepted === false) {
      if (!this.pendingBeginItemId ||
          detail.itemId !== this.pendingBeginItemId) return
      this.activeSessionId = ''
      this.pendingBeginItemId = ''
      this.clearDragCandidate()
      this.clearPreview()
      console.warn('[SISH5UI PointerTrace][Drag] rejected', detail)
      return
    }
    if (detail?.accepted === true && detail.sessionId) {
      if (!this.pendingBeginItemId ||
          detail.itemId !== this.pendingBeginItemId) return
      this.activeSessionId = detail.sessionId
      this.dragOverCount = 0
      console.warn('[SISH5UI PointerTrace][Drag] accepted', {
        sessionId: detail.sessionId,
        itemId: detail.itemId
      })
    }
  }

  private readonly handleDragResult = (event: Event): void => {
    const detail = readEventDetail(event) as {
      sessionId?: string
    } | null
    if (!this.activeSessionId ||
        !detail?.sessionId ||
        detail.sessionId !== this.activeSessionId) return
    sisH5UIPointerSessions.abort(this.pointerOwner)
    console.warn('[SISH5UI PointerTrace][Drag] result', {
      sessionId: detail.sessionId,
      dragOverCount: this.dragOverCount
    })
    this.activeSessionId = ''
    this.pendingBeginItemId = ''
    this.clearDragCandidate()
    this.clearPreview()
  }

  private readonly handleDragOver = (event: Event): void => {
    const detail = readEventDetail(event) as ExternalDragDetail | null
    this.processDragPosition(detail)
  }

  private processDragPosition(detail: ExternalDragDetail | null): void {
    if (!this.activeSessionId || detail?.sessionId !== this.activeSessionId) return
    this.dragOverCount += 1
    const candidate = this.resolveDropCandidate(detail)
    if (!candidate) {
      this.clearPreview()
      return
    }
    if (candidate.key === this.previewCandidate?.key) return

    console.warn('[SISH5UI PointerTrace][Drag] candidate', {
      sessionId: detail.sessionId,
      dragOverCount: this.dragOverCount,
      clientX: detail.clientX,
      clientY: detail.clientY,
      targetKind: candidate.target.kind,
      candidateKey: candidate.key
    })
    this.renderPreview(candidate, 'pending')
    this.emitToUnreal(
      SISH5UI_INVENTORY_DRAG_EVENTS.probeRequest,
      this.buildRequestPayload(candidate)
    )
  }

  private readonly handleProbeResult = (event: Event): void => {
    const result = readEventDetail(event) as InventoryProbeResult | null
    const candidate = this.previewCandidate
    if (!result || !candidate ||
        !this.activeSessionId ||
        result.sessionId !== this.activeSessionId ||
        !this.matchesCandidate(result, candidate)) return

    const authoritativeCandidate: InventoryDropCandidate = {
      ...candidate,
      previewRow: integerOr(result.targetRow, candidate.previewRow),
      previewColumn: integerOr(result.targetColumn, candidate.previewColumn),
      rowSpan: positiveInteger(result.rowSpan, candidate.rowSpan),
      columnSpan: positiveInteger(result.columnSpan, candidate.columnSpan),
      handleMode: result.handleMode,
      willReplace: result.willReplace,
      occupiedEquipmentSlotIds: Array.isArray(result.occupiedEquipmentSlotIds)
        ? result.occupiedEquipmentSlotIds
        : [],
      moves: Array.isArray(result.moves) ? result.moves : []
    }
    const state: PreviewState = result.accepted === true
      ? (result.willReplace === true ? 'replace' : 'valid')
      : 'invalid'
    this.renderPreview(authoritativeCandidate, state, result.reason)
  }

  private readonly handleDragLeave = (event: Event): void => {
    const detail = readEventDetail(event) as ExternalDragDetail | null
    if (!this.activeSessionId || detail?.sessionId !== this.activeSessionId) return
    this.clearPreview()
  }

  private readonly handleDrop = (event: Event): void => {
    const detail = readEventDetail(event) as ExternalDragDetail | null
    if (!this.activeSessionId || detail?.sessionId !== this.activeSessionId) return
    const candidate = this.resolveDropCandidate(detail)
    if (!candidate) return

    this.emitToUnreal(
      SISH5UI_INVENTORY_DRAG_EVENTS.dropRequest,
      this.buildRequestPayload(candidate)
    )
  }

  private buildRequestPayload(candidate: InventoryDropCandidate): Record<string, unknown> {
    const common = {
      protocolVersion: SISH5UI_PROTOCOL_VERSION,
      sessionId: candidate.sessionId,
      itemId: candidate.itemId,
      ...(this.dragCandidate
        ? {
            sourceInventoryComponentId: this.dragCandidate.inventoryComponentId,
            sourcePresentationSlot: this.dragCandidate.presentationSlot
          }
        : {})
    }
    if (candidate.target.kind === 'equipment') {
      return {
        ...common,
        targetKind: 'equipment',
        equipmentViewId: candidate.target.equipmentViewId,
        equipmentSlotId: candidate.target.equipmentSlotId,
        targetInventoryComponentId: candidate.target.inventoryComponentId,
        targetPresentationSlot: candidate.target.presentationSlot
      }
    }
    if (candidate.target.kind === 'attachment') {
      return {
        ...common,
        targetKind: 'attachment',
        targetInventoryId: candidate.target.inventoryId,
        targetInventoryComponentId: candidate.target.inventoryComponentId,
        targetPresentationSlot: candidate.target.presentationSlot,
        targetParentItemId: candidate.target.parentItemId,
        targetAttachmentSlotIndex: candidate.target.attachmentSlotIndex
      }
    }
    return {
      ...common,
      targetInventoryId: candidate.target.inventoryId,
      targetInventoryComponentId: candidate.target.inventoryComponentId,
      targetPresentationSlot: candidate.target.presentationSlot,
      ...(candidate.target.parentItemId
        ? { targetParentItemId: candidate.target.parentItemId }
        : {}),
      ...(candidate.target.targetItemId
        ? { targetItemId: candidate.target.targetItemId }
        : {}),
      targetContainerIndex: candidate.target.containerIndex,
      hoverRow: candidate.target.hoverRow,
      hoverColumn: candidate.target.hoverColumn,
      quadrant: candidate.target.quadrant
    }
  }

  private clearDragCandidate(): void {
    this.dragCandidate?.sourceElement.classList.remove(SOURCE_CLASS)
    this.dragCandidate = null
  }

  private clearPreview(): void {
    for (const cell of this.previewCells) {
      cell.classList.remove(PREVIEW_CLASS)
      cell.classList.remove(REPLACEMENT_PREVIEW_CLASS)
      for (const state of PREVIEW_STATES) {
        cell.classList.remove(`inventory-grid-cell-drop-${state}`)
      }
    }
    this.previewCells = []

    for (const grid of this.previewGrids) {
      for (const state of PREVIEW_STATES) {
        grid.classList.remove(`inventory-grid-drop-${state}`)
      }
      grid.removeAttribute('data-drop-reason')
      grid.removeAttribute('data-drop-handle-mode')
      grid.removeAttribute('data-drop-will-replace')
    }
    this.previewGrids = []
    for (const slot of this.previewEquipmentSlots) {
      for (const state of PREVIEW_STATES) {
        slot.classList.remove(`equipment-slot-drop-${state}`)
      }
      slot.classList.remove('equipment-slot-replacement-preview')
      slot.removeAttribute('data-drop-reason')
      slot.removeAttribute('data-drop-handle-mode')
      slot.removeAttribute('data-drop-will-replace')
    }
    this.previewEquipmentSlots = []
    for (const slot of this.previewAttachmentSlots) {
      for (const state of PREVIEW_STATES) {
        slot.classList.remove(`item-detail-attachment-slot-drop-${state}`)
      }
      slot.removeAttribute('data-drop-reason')
      slot.removeAttribute('data-drop-handle-mode')
      slot.removeAttribute('data-drop-will-replace')
    }
    this.previewAttachmentSlots = []
    this.previewCandidate = null
  }

  private renderPreview(
    candidate: InventoryDropCandidate,
    state: PreviewState,
    reason = ''
  ): void {
    this.clearPreview()
    this.previewCandidate = candidate

    if (candidate.target.kind === 'equipment') {
      const occupiedSlots = (candidate.occupiedEquipmentSlotIds ?? [])
        .map(slotId => document.getElementById(slotId))
        .filter((slot): slot is HTMLElement => slot !== null)
      if (!occupiedSlots.includes(candidate.target.slotElement as HTMLElement)) {
        occupiedSlots.unshift(candidate.target.slotElement as HTMLElement)
      }
      for (const slot of occupiedSlots) {
        if (this.previewEquipmentSlots.includes(slot)) continue
        this.previewEquipmentSlots.push(slot)
        slot.classList.add(`equipment-slot-drop-${state}`)
        if (reason) slot.setAttribute('data-drop-reason', reason)
        if (candidate.handleMode) slot.setAttribute('data-drop-handle-mode', candidate.handleMode)
        if (candidate.willReplace) slot.setAttribute('data-drop-will-replace', 'true')
      }
      for (const move of candidate.moves ?? []) {
        if (move.primaryItem === true || !move.fromEquipmentSlotId) continue
        const replacedSlot = document.getElementById(move.fromEquipmentSlotId)
        if (!replacedSlot || this.previewEquipmentSlots.includes(replacedSlot)) continue
        replacedSlot.classList.add('equipment-slot-drop-replace')
        replacedSlot.classList.add('equipment-slot-replacement-preview')
        this.previewEquipmentSlots.push(replacedSlot)
      }
      return
    }

    if (candidate.target.kind === 'attachment') {
      const slot = candidate.target.slotElement
      this.previewAttachmentSlots.push(slot)
      slot.classList.add(`item-detail-attachment-slot-drop-${state}`)
      if (reason) slot.setAttribute('data-drop-reason', reason)
      if (candidate.handleMode) {
        slot.setAttribute('data-drop-handle-mode', candidate.handleMode)
      }
      if (candidate.willReplace) {
        slot.setAttribute('data-drop-will-replace', 'true')
      }
      return
    }

    const grid = candidate.target.gridElement
    this.previewGrids.push(grid)
    grid.classList.add(`inventory-grid-drop-${state}`)
    if (reason) grid.setAttribute('data-drop-reason', reason)
    if (candidate.handleMode) grid.setAttribute('data-drop-handle-mode', candidate.handleMode)
    if (candidate.willReplace) grid.setAttribute('data-drop-will-replace', 'true')

    const authoritativeMoves = candidate.moves ?? []
    if (state !== 'pending' && authoritativeMoves.length > 0) {
      let primaryRendered = false
      for (const move of authoritativeMoves) {
        const moveGrid = this.findGrid(
          move.targetInventoryId || candidate.target.inventoryId,
          integerOr(move.containerIndex, candidate.target.containerIndex),
          normalizeParentItemId(move.parentItemId) || candidate.target.parentItemId
        )
        if (!moveGrid) continue
        const moveState: PreviewState = move.primaryItem === true ? state : 'replace'
        this.markPreviewCells(
          moveGrid,
          integerOr(move.row, candidate.previewRow),
          integerOr(move.column, candidate.previewColumn),
          positiveInteger(move.rowSpan, candidate.rowSpan),
          positiveInteger(move.columnSpan, candidate.columnSpan),
          moveState,
          move.primaryItem !== true
        )
        primaryRendered ||= move.primaryItem === true
      }
      if (primaryRendered) return
    }

    this.markPreviewCells(
      grid,
      candidate.previewRow,
      candidate.previewColumn,
      candidate.rowSpan,
      candidate.columnSpan,
      state,
      false
    )
  }

  private findGrid(inventoryId: string, containerIndex: number, parentItemId = ''): Element | null {
    const normalizedParentItemId = normalizeParentItemId(parentItemId)
    for (const grid of document.querySelectorAll(GRID_SELECTOR)) {
      const owner = grid.closest(INVENTORY_SELECTOR)
      if (owner?.getAttribute('data-inventory-id') === inventoryId &&
          Number(grid.getAttribute('data-container-index')) === containerIndex &&
          normalizeParentItemId(grid.getAttribute('data-parent-item-id')) === normalizedParentItemId) {
        return grid
      }
    }
    return null
  }

  private markPreviewCells(
    grid: Element,
    rowStart: number,
    columnStart: number,
    rowSpan: number,
    columnSpan: number,
    state: PreviewState,
    replacement: boolean
  ): void {
    if (!this.previewGrids.includes(grid)) this.previewGrids.push(grid)
    for (const cell of grid.querySelectorAll(GRID_CELL_SELECTOR)) {
      const row = Number(cell.getAttribute('data-row'))
      const column = Number(cell.getAttribute('data-column'))
      if (row >= rowStart && row < rowStart + rowSpan &&
          column >= columnStart && column < columnStart + columnSpan) {
        cell.classList.add(PREVIEW_CLASS)
        cell.classList.add(`inventory-grid-cell-drop-${state}`)
        if (replacement) cell.classList.add(REPLACEMENT_PREVIEW_CLASS)
        this.previewCells.push(cell)
      }
    }
  }

  private resolveDropCandidate(detail: ExternalDragDetail | null): InventoryDropCandidate | null {
    if (detail?.protocolVersion !== SISH5UI_PROTOCOL_VERSION || !detail.sessionId || !detail.itemId) return null
    const target = this.resolveDropTarget(detail)
    if (!target) return null

    const targetKey = target.kind === 'equipment'
      ? [
          target.kind,
          target.equipmentViewId,
          target.equipmentSlotId,
          target.inventoryComponentId,
          target.presentationSlot
        ]
      : target.kind === 'attachment'
        ? [
            target.kind,
            target.inventoryId,
            target.inventoryComponentId,
            target.presentationSlot,
            target.parentItemId,
            target.attachmentSlotIndex
          ]
        : [
          target.kind,
          target.inventoryId,
          target.inventoryComponentId,
          target.presentationSlot,
          target.parentItemId,
          target.targetItemId,
          target.containerIndex,
          target.hoverRow,
          target.hoverColumn,
          target.quadrant
        ]
    return {
      key: [detail.sessionId, detail.itemId, ...targetKey].join('|'),
      sessionId: detail.sessionId,
      itemId: detail.itemId,
      target,
      previewRow: target.kind === 'inventory' ? target.hoverRow : 0,
      previewColumn: target.kind === 'inventory' ? target.hoverColumn : 0,
      rowSpan: 1,
      columnSpan: 1
    }
  }

  private resolveDropTarget(detail: ExternalDragDetail): DropTarget | null {
    if (typeof detail.clientX !== 'number' || typeof detail.clientY !== 'number') return null

    const hitElement = document.elementFromPoint(detail.clientX, detail.clientY)
    const attachmentTarget = this.createAttachmentDropTarget(
      hitElement?.closest?.(ATTACHMENT_SLOT_SELECTOR) ?? null
    )
    if (attachmentTarget) return attachmentTarget

    const pointerEquipmentTarget = this.createEquipmentDropTarget(
      hitElement?.closest?.(EQUIPMENT_SLOT_SELECTOR) ?? null,
      'pointer'
    )
    if (pointerEquipmentTarget) return pointerEquipmentTarget

    const overlapEquipmentTarget = this.createEquipmentDropTarget(
      this.findEquipmentSlotOverlappingDragVisual(detail),
      'overlap'
    )
    if (overlapEquipmentTarget) return overlapEquipmentTarget

    const gridElement = hitElement?.closest?.(GRID_SELECTOR)
    const inventoryElement = gridElement?.closest?.(INVENTORY_SELECTOR)
    if (!gridElement || !inventoryElement) return null

    const inventoryId = inventoryElement.getAttribute('data-inventory-id') || ''
    const inventoryComponentId =
      inventoryElement.getAttribute('data-inventory-component-id') || inventoryId
    const presentationSlot = inventoryElement.getAttribute('data-presentation-slot') || ''
    const parentItemId = normalizeParentItemId(gridElement.getAttribute('data-parent-item-id'))
    const hoveredItemElement = hitElement?.closest?.(ITEM_SELECTOR) ?? null
    const targetItemId =
      hoveredItemElement?.closest?.(GRID_SELECTOR) === gridElement
        ? (hoveredItemElement.getAttribute('data-item-id') || '')
        : ''
    const containerIndex = Number(gridElement.getAttribute('data-container-index'))
    const columns = Number(gridElement.getAttribute('data-columns'))
    const rows = Number(gridElement.getAttribute('data-rows'))
    if (!inventoryId || !inventoryComponentId ||
        !Number.isInteger(containerIndex) || columns <= 0 || rows <= 0) return null

    const gridRect = gridElement.getBoundingClientRect()
    const firstCellRect = gridElement.querySelector(GRID_CELL_SELECTOR)?.getBoundingClientRect()
    const cellWidth = firstCellRect?.width || gridRect.width / columns
    const cellHeight = firstCellRect?.height || gridRect.height / rows
    if (cellWidth <= 0 || cellHeight <= 0) return null

    const gridOriginX = firstCellRect?.left ?? gridRect.left
    const gridOriginY = firstCellRect?.top ?? gridRect.top
    const gridX = detail.clientX - gridOriginX
    const gridY = detail.clientY - gridOriginY
    const hoverColumn = Math.floor(gridX / cellWidth)
    const hoverRow = Math.floor(gridY / cellHeight)
    if (hoverColumn < 0 || hoverColumn >= columns || hoverRow < 0 || hoverRow >= rows) return null

    const inCellX = gridX - hoverColumn * cellWidth
    const inCellY = gridY - hoverRow * cellHeight
    const horizontal = inCellX < cellWidth * 0.5 ? 'left' : 'right'
    const vertical = inCellY < cellHeight * 0.5 ? 'top' : 'bottom'

    return {
      kind: 'inventory',
      gridElement,
      inventoryId,
      inventoryComponentId,
      presentationSlot,
      parentItemId,
      targetItemId,
      containerIndex,
      hoverRow,
      hoverColumn,
      quadrant: `${vertical}-${horizontal}` as CellQuadrant
    }
  }

  private createEquipmentDropTarget(
    equipmentSlot: Element | null,
    hitMode: EquipmentDropTarget['hitMode']
  ): EquipmentDropTarget | null {
    if (!equipmentSlot) return null
    const equipmentViewId = equipmentSlot.getAttribute('data-equipment-view-id') || ''
    const equipmentSlotId = equipmentSlot.getAttribute('data-equipment-slot-id') || ''
    const inventoryComponentId =
      equipmentSlot.getAttribute('data-inventory-component-id') ||
      equipmentSlot.getAttribute('data-inventory-id') ||
      ''
    const presentationSlot =
      equipmentSlot.getAttribute('data-presentation-slot') || 'characterInventory'
    if (!equipmentViewId || !equipmentSlotId) return null
    return {
      kind: 'equipment',
      slotElement: equipmentSlot,
      equipmentViewId,
      equipmentSlotId,
      inventoryComponentId,
      presentationSlot,
      hitMode
    }
  }

  private createAttachmentDropTarget(
    attachmentSlot: Element | null
  ): AttachmentDropTarget | null {
    if (!attachmentSlot) return null
    const inventoryId = attachmentSlot.getAttribute('data-inventory-id') || ''
    const inventoryComponentId =
      attachmentSlot.getAttribute('data-inventory-component-id') || inventoryId
    const presentationSlot =
      attachmentSlot.getAttribute('data-presentation-slot') || ''
    const parentItemId = normalizeParentItemId(
      attachmentSlot.getAttribute('data-parent-item-id')
    )
    const attachmentSlotIndex = Number(
      attachmentSlot.getAttribute('data-attachment-slot-index')
    )
    if (!inventoryId || !inventoryComponentId || !parentItemId ||
        !Number.isInteger(attachmentSlotIndex) || attachmentSlotIndex < 0) return null
    return {
      kind: 'attachment',
      slotElement: attachmentSlot,
      inventoryId,
      inventoryComponentId,
      presentationSlot,
      parentItemId,
      attachmentSlotIndex
    }
  }

  private findEquipmentSlotOverlappingDragVisual(detail: ExternalDragDetail): Element | null {
    if (typeof detail.clientX !== 'number' || typeof detail.clientY !== 'number' ||
      typeof detail.visualWidth !== 'number' || typeof detail.visualHeight !== 'number' ||
      typeof detail.grabOffsetX !== 'number' || typeof detail.grabOffsetY !== 'number') return null

    const dragLeft = detail.clientX - detail.grabOffsetX
    const dragTop = detail.clientY - detail.grabOffsetY
    const dragRight = dragLeft + detail.visualWidth
    const dragBottom = dragTop + detail.visualHeight
    let bestSlot: Element | null = null
    let bestArea = 0

    for (const slot of document.querySelectorAll(EQUIPMENT_SLOT_SELECTOR)) {
      const rect = slot.getBoundingClientRect()
      const overlapWidth = Math.max(0, Math.min(dragRight, rect.right) - Math.max(dragLeft, rect.left))
      const overlapHeight = Math.max(0, Math.min(dragBottom, rect.bottom) - Math.max(dragTop, rect.top))
      const overlapArea = overlapWidth * overlapHeight
      const slotArea = Math.max(1, rect.width * rect.height)
      if (overlapArea >= slotArea * 0.12 && overlapArea > bestArea) {
        bestSlot = slot
        bestArea = overlapArea
      }
    }
    return bestSlot
  }

  private matchesCandidate(result: InventoryProbeResult, candidate: InventoryDropCandidate): boolean {
    if (result.protocolVersion !== SISH5UI_PROTOCOL_VERSION ||
      result.sessionId !== candidate.sessionId || result.itemId !== candidate.itemId) return false
    if (candidate.target.kind === 'equipment') {
      return result.targetKind === 'equipment' &&
        result.equipmentViewId === candidate.target.equipmentViewId &&
        result.equipmentSlotId === candidate.target.equipmentSlotId
    }
    if (candidate.target.kind === 'attachment') {
      return result.targetKind === 'attachment' &&
        result.targetInventoryId === candidate.target.inventoryId &&
        normalizeParentItemId(result.targetParentItemId) === candidate.target.parentItemId &&
        Number(result.targetAttachmentSlotIndex) === candidate.target.attachmentSlotIndex
    }
    return result.targetInventoryId === candidate.target.inventoryId &&
      Number(result.targetContainerIndex) === candidate.target.containerIndex &&
      normalizeParentItemId(result.targetParentItemId) === candidate.target.parentItemId &&
      (result.targetItemId || '').toLowerCase() === candidate.target.targetItemId.toLowerCase() &&
      Number(result.hoverRow) === candidate.target.hoverRow &&
      Number(result.hoverColumn) === candidate.target.hoverColumn &&
      result.quadrant === candidate.target.quadrant
  }
}
