export type SceneId = 'haven' | 'underground'

export type LocationId =
  | 'hall'
  | 'kitchen'
  | 'office'
  | 'command-room'
  | 'readiness-room'
  | 'squad-room'
  | 'commander-office'

export type SceneOption = {
  id: SceneId
  code: string
  label: string
  sublabel: string
}

export type LocationOption = {
  id: LocationId
  code: string
  label: string
}

export type CalendarTime = {
  month: number
  day: number
  hour: number
  minute: number
}
