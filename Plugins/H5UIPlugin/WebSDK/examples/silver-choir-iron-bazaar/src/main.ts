/**
 * Iron Bazaar — Vue marketplace module for CommanderOS / H5UI.
 *
 * Build: pnpm --filter silver-choir-iron-bazaar build
 * Output: Content/GameCore/H5UI/CommanderOS/V2/iron-bazaar.{js,css,catalog.json}
 *
 * Runtime API (window.IronBazaar):
 *   mount(el, options?)
 *   unmount()
 *   setCatalog(catalog | products[])
 *   upsertProduct(product)
 *   openProduct(id)
 *   isShowingDetail()
 *   backToBrowse()
 *   getCatalog()
 */
import { createH5UIApp, type App } from '@h5ui-plugin/vue'
import AppRoot from './App.vue'
import { createShopStore, type ShopStore } from './shop/state'
import type { MountOptions, ShopCatalog, ShopProduct } from './types'
import defaultCatalog from './data/catalog.json'

type IronBazaarApi = {
  mount: (el: string | Element, options?: MountOptions) => void
  unmount: () => void
  setCatalog: (catalog?: ShopCatalog | ShopProduct[] | null) => void
  upsertProduct: (product: ShopProduct) => void
  openProduct: (id: string) => void
  isShowingDetail: () => boolean
  backToBrowse: () => void
  getCatalog: () => ShopCatalog | null
  version: string
}

let app: App<Element> | null = null
let store: ShopStore | null = null
let mountedEl: Element | null = null

function resolveEl(el: string | Element): Element {
  if (typeof el === 'string') {
    const found = document.querySelector(el)
    if (!found) throw new Error(`IronBazaar.mount: element not found: ${el}`)
    return found
  }
  return el
}

function mount(el: string | Element, options: MountOptions = {}): void {
  unmount()
  const target = resolveEl(el)
  mountedEl = target
  target.innerHTML = ''

  // Default: back button returns to CommanderOS home if available
  const opts: MountOptions = {
    onBackToIndex: () => {
      try {
        const cos = (window as Window & { CommanderOS?: { navigate?: (id: string) => void } }).CommanderOS
        if (cos && typeof cos.navigate === 'function') {
          cos.navigate('home')
          return
        }
      } catch {
        /* ignore */
      }
    },
    ...options
  }

  store = createShopStore(opts)
  app = createH5UIApp(AppRoot)
  app.provide('shopStore', store)
  app.mount(target)

  if (opts.productId) {
    store.openProduct(opts.productId)
  }
}

function unmount(): void {
  if (app) {
    try {
      app.unmount()
    } catch {
      /* ignore */
    }
    app = null
  }
  store = null
  if (mountedEl) {
    try {
      mountedEl.innerHTML = ''
    } catch {
      /* ignore */
    }
    mountedEl = null
  }
}

function setCatalog(catalog?: ShopCatalog | ShopProduct[] | null): void {
  if (store) {
    store.setCatalog(catalog)
    return
  }
  // Cache for next mount via defaultCatalog mutation is intentionally avoided;
  // callers should mount with { catalog } or call setCatalog after mount.
  ;(window as Window & { __IronBazaarPendingCatalog?: ShopCatalog | ShopProduct[] | null }).__IronBazaarPendingCatalog = catalog ?? null
}

function upsertProduct(product: ShopProduct): void {
  if (store) store.upsertProduct(product)
}

function openProduct(id: string): void {
  if (store) store.openProduct(id)
}

function isShowingDetail(): boolean {
  return store?.view.value === 'detail'
}

function backToBrowse(): void {
  if (store?.view.value === 'detail') store.backToBrowse()
}

function getCatalog(): ShopCatalog | null {
  if (store) return store.catalog.value
  return defaultCatalog as ShopCatalog
}

const api: IronBazaarApi = {
  mount,
  unmount,
  setCatalog,
  upsertProduct,
  openProduct,
  isShowingDetail,
  backToBrowse,
  getCatalog,
  version: '0.1.0'
}

const w = window as Window & { IronBazaar?: IronBazaarApi }
w.IronBazaar = api

// Standalone page: auto-mount #iron-bazaar-app
const standalone = document.getElementById('iron-bazaar-app')
if (standalone) {
  const pending = (window as Window & { __IronBazaarPendingCatalog?: ShopCatalog | ShopProduct[] | null }).__IronBazaarPendingCatalog
  mount(standalone, pending ? { catalog: pending } : {})
}

export default api
