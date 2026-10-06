/**
 * Special product pages — register by key matching product.detail.specialPage.
 * Most products use the generic ProductDetail.vue; only outliers go here.
 *
 * Later: add a new Vue SFC and map it below, then rebuild the module.
 */
import type { Component } from '@h5ui-plugin/vue'
import ScarHPage from './ScarHPage.vue'

const registry: Record<string, Component> = {
  'scar-h': ScarHPage
}

export function resolveSpecialPage(key?: string | null): Component | null {
  if (!key) return null
  return registry[key] || null
}

export function listSpecialPages(): string[] {
  return Object.keys(registry)
}
