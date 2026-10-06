export type SISH5UIPointerSessionEndReason =
  | 'mouseup'
  | 'window-blur'
  | 'owner-release'
  | 'owner-unmounted'
  | 'superseded'
  | 'ue-result'

export type SISH5UIPointerSessionCallbacks = {
  move?: (event: MouseEvent) => void
  end?: (
    event: MouseEvent | null,
    reason: SISH5UIPointerSessionEndReason
  ) => void
}

type ActivePointerSession = {
  owner: string
  button: number
  sequence: number
  moveCount: number
  callbacks: SISH5UIPointerSessionCallbacks
}

/**
 * Document-level mouse session coordinator shared by inventory dragging,
 * item-preview rotation and detail-panel movement.
 *
 * Slate owns native capture. This class owns the H5 gesture owner, ensuring
 * that features cannot retain independent mousemove/mouseup state.
 */
export class SISH5UIPointerSessionCoordinator {
  private host: Window | null = null
  private retainCount = 0
  private active: ActivePointerSession | null = null
  private nextSequence = 0

  retain(host: Window = window): void {
    if (this.host && this.host !== host) this.detach('owner-unmounted')
    if (!this.host) {
      this.host = host
      host.addEventListener('mousemove', this.handleMouseMove, true)
      host.addEventListener('mouseup', this.handleMouseUp, true)
      host.addEventListener('blur', this.handleWindowBlur)
    }
    this.retainCount += 1
  }

  releaseHost(owner?: string): void {
    if (owner && this.active?.owner === owner) {
      this.finish(null, 'owner-unmounted')
    }
    this.retainCount = Math.max(0, this.retainCount - 1)
    if (this.retainCount === 0) this.detach('owner-unmounted')
  }

  begin(
    owner: string,
    event: MouseEvent,
    callbacks: SISH5UIPointerSessionCallbacks
  ): boolean {
    if (!owner || !this.host || event.button < 0) return false
    if (this.active) {
      if (this.active.owner !== owner) return false
      this.finish(null, 'superseded')
    }
    this.active = {
      owner,
      button: event.button,
      sequence: ++this.nextSequence,
      moveCount: 0,
      callbacks
    }
    console.warn('[SISH5UI PointerTrace][H5Session] begin', {
      sequence: this.active.sequence,
      owner,
      button: event.button,
      clientX: event.clientX,
      clientY: event.clientY
    })
    return true
  }

  isOwnedBy(owner: string): boolean {
    return this.active?.owner === owner
  }

  end(owner: string, reason: SISH5UIPointerSessionEndReason = 'owner-release'): void {
    if (this.active?.owner === owner) this.finish(null, reason)
  }

  /** Releases ownership without invoking a business action already completed by Unreal. */
  abort(owner: string): void {
    if (this.active?.owner === owner) {
      console.warn('[SISH5UI PointerTrace][H5Session] abort', {
        sequence: this.active.sequence,
        owner,
        moveCount: this.active.moveCount
      })
      this.active = null
    }
  }

  private readonly handleMouseMove = (event: Event): void => {
    const session = this.active
    if (!session) return
    const mouseEvent = event as MouseEvent
    session.moveCount += 1
    if (session.moveCount <= 3 || session.moveCount % 15 === 0) {
      console.warn('[SISH5UI PointerTrace][H5Session] move', {
        sequence: session.sequence,
        owner: session.owner,
        moveCount: session.moveCount,
        clientX: mouseEvent.clientX,
        clientY: mouseEvent.clientY
      })
    }
    session.callbacks.move?.(mouseEvent)
  }

  private readonly handleMouseUp = (event: Event): void => {
    const mouseEvent = event as MouseEvent
    if (!this.active || mouseEvent.button !== this.active.button) return
    this.finish(mouseEvent, 'mouseup')
  }

  private readonly handleWindowBlur = (): void => {
    this.finish(null, 'window-blur')
  }

  private finish(
    event: MouseEvent | null,
    reason: SISH5UIPointerSessionEndReason
  ): void {
    const session = this.active
    if (!session) return
    // Clear first because callbacks may synchronously cross into Unreal and
    // dispatch another H5 event.
    this.active = null
    console.warn('[SISH5UI PointerTrace][H5Session] end', {
      sequence: session.sequence,
      owner: session.owner,
      moveCount: session.moveCount,
      reason,
      clientX: event?.clientX,
      clientY: event?.clientY
    })
    session.callbacks.end?.(event, reason)
  }

  private detach(reason: SISH5UIPointerSessionEndReason): void {
    this.finish(null, reason)
    if (this.host) {
      this.host.removeEventListener('mousemove', this.handleMouseMove, true)
      this.host.removeEventListener('mouseup', this.handleMouseUp, true)
      this.host.removeEventListener('blur', this.handleWindowBlur)
    }
    this.host = null
    this.retainCount = 0
  }
}

export const sisH5UIPointerSessions = new SISH5UIPointerSessionCoordinator()
