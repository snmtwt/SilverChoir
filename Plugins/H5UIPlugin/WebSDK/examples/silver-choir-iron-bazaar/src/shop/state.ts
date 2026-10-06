import { computed, nextTick, reactive, ref, type Ref } from '@h5ui-plugin/vue'
import type {
  MountOptions,
  ShopCatalog,
  ShopProduct,
  ShopSort,
  ShopView
} from '../types'
import defaultCatalog from '../data/catalog.json'

/** Catalog list scroll region (see App.vue `.shop-main[data-shop-scroll]`). */
const BROWSE_SCROLL_SELECTOR = '[data-shop-scroll]'

function findBrowseScrollEl(): HTMLElement | null {
  try {
    const el = document.querySelector(BROWSE_SCROLL_SELECTOR)
    return el instanceof HTMLElement ? el : null
  } catch {
    return null
  }
}

function normalizeCatalog(input?: ShopCatalog | ShopProduct[] | null): ShopCatalog {
  if (!input) {
    return JSON.parse(JSON.stringify(defaultCatalog)) as ShopCatalog
  }
  if (Array.isArray(input)) {
    return {
      version: 1,
      title: 'Iron Bazaar',
      tagline: 'Marketplace · shadow://iron.bazaar',
      currency: 'USD',
      freeShippingOver: 2500,
      shippingFee: 120,
      categories: (defaultCatalog as ShopCatalog).categories,
      products: input
    }
  }
  const base = defaultCatalog as ShopCatalog
  return {
    version: input.version ?? 1,
    title: input.title || base.title,
    tagline: input.tagline || base.tagline,
    currency: input.currency || 'USD',
    freeShippingOver: input.freeShippingOver ?? 2500,
    shippingFee: input.shippingFee ?? 120,
    categories: input.categories && input.categories.length ? input.categories : base.categories,
    products: Array.isArray(input.products) ? input.products : []
  }
}

function formatMoney(n: number): string {
  const v = Math.round(Number(n) || 0)
  const s = String(v)
  let out = ''
  let count = 0
  for (let i = s.length - 1; i >= 0; i -= 1) {
    out = s.charAt(i) + out
    count += 1
    if (count === 3 && i > 0) {
      out = ',' + out
      count = 0
    }
  }
  return '$' + out
}

export type ShopStore = ReturnType<typeof createShopStore>

export function createShopStore(options: MountOptions = {}) {
  const catalog = ref(normalizeCatalog(options.catalog)) as Ref<ShopCatalog>
  const view = ref<ShopView>(options.productId ? 'detail' : 'browse')
  const activeProductId = ref<string | null>(options.productId || null)
  const category = ref('all')
  const sort = ref<ShopSort>('featured')
  const query = ref('')
  const cartOpen = ref(false)
  const cart = reactive<Record<string, number>>({})
  const toast = ref('')
  let toastTimer: ReturnType<typeof setTimeout> | undefined

  /**
   * Browse list scroll — like real browser history.scrollRestoration:
   * saved when leaving catalog for product detail, restored on back.
   * Soft-reload remounts a fresh store, so scroll starts at 0.
   */
  let browseScrollTop = 0
  let browseScrollLeft = 0
  let restoreScrollScheduled = false

  const onBackToIndex = options.onBackToIndex
  const emitHost = options.emitHost || defaultEmitHost

  const products = computed(() => catalog.value.products || [])

  const filtered = computed(() => {
    const q = query.value.trim().toLowerCase()
    let list = products.value.slice()
    if (category.value !== 'all') {
      list = list.filter(p => p.cat === category.value)
    }
    if (q) {
      list = list.filter(p => {
        const hay = `${p.name} ${p.desc} ${p.cat} ${(p.detail?.tags || []).join(' ')}`.toLowerCase()
        return hay.indexOf(q) !== -1
      })
    }
    list.sort((a, b) => {
      if (sort.value === 'price-asc') return a.price - b.price
      if (sort.value === 'price-desc') return b.price - a.price
      if (sort.value === 'rating') return b.rating - a.rating
      return (b.featured || 0) - (a.featured || 0)
    })
    return list
  })

  const activeProduct = computed(() => {
    if (!activeProductId.value) return null
    return products.value.find(p => p.id === activeProductId.value) || null
  })

  const cartCount = computed(() => {
    let n = 0
    for (const id of Object.keys(cart)) n += cart[id] || 0
    return n
  })

  const subtotal = computed(() => {
    let sum = 0
    for (const id of Object.keys(cart)) {
      const p = products.value.find(x => x.id === id)
      if (p) sum += p.price * (cart[id] || 0)
    }
    return sum
  })

  const shipping = computed(() => {
    if (subtotal.value <= 0) return 0
    const freeOver = catalog.value.freeShippingOver ?? 2500
    if (subtotal.value >= freeOver) return 0
    return catalog.value.shippingFee ?? 120
  })

  const total = computed(() => subtotal.value + shipping.value)

  function showToast(message: string, ms = 2200) {
    toast.value = message
    if (toastTimer !== undefined) clearTimeout(toastTimer)
    toastTimer = setTimeout(() => {
      toast.value = ''
      toastTimer = undefined
    }, ms)
  }

  function setCatalog(next?: ShopCatalog | ShopProduct[] | null) {
    catalog.value = normalizeCatalog(next)
    if (activeProductId.value && !products.value.find(p => p.id === activeProductId.value)) {
      activeProductId.value = null
      view.value = 'browse'
    }
    // Drop cart lines for removed products
    for (const id of Object.keys(cart)) {
      if (!products.value.find(p => p.id === id)) delete cart[id]
    }
  }

  function upsertProduct(product: ShopProduct) {
    if (!product || !product.id) return
    const list = catalog.value.products.slice()
    const idx = list.findIndex(p => p.id === product.id)
    if (idx >= 0) list[idx] = { ...list[idx], ...product }
    else list.push(product)
    catalog.value = { ...catalog.value, products: list }
  }

  function captureBrowseScroll() {
    const el = findBrowseScrollEl()
    if (!el) return
    browseScrollTop = el.scrollTop || 0
    browseScrollLeft = el.scrollLeft || 0
  }

  /** Drop saved position (e.g. explicit reset). Soft-reload already remounts. */
  function clearBrowseScroll() {
    browseScrollTop = 0
    browseScrollLeft = 0
  }

  function applyBrowseScroll(el: HTMLElement) {
    el.scrollTop = browseScrollTop
    el.scrollLeft = browseScrollLeft
    if (typeof el.scrollTo === 'function') {
      try {
        el.scrollTo(browseScrollLeft, browseScrollTop)
      } catch {
        /* ignore */
      }
    }
  }

  function scheduleBrowseScrollRestore() {
    if (restoreScrollScheduled) return
    restoreScrollScheduled = true
    nextTick(() => {
      restoreScrollScheduled = false
      const el = findBrowseScrollEl()
      if (!el) return
      applyBrowseScroll(el)
      /* Second pass after grid layout / images settle (browser-like). */
      const top = browseScrollTop
      const left = browseScrollLeft
      const paint = () => {
        const again = findBrowseScrollEl()
        if (!again) return
        browseScrollTop = top
        browseScrollLeft = left
        applyBrowseScroll(again)
      }
      if (typeof requestAnimationFrame === 'function') {
        requestAnimationFrame(() => requestAnimationFrame(paint))
      } else {
        setTimeout(paint, 0)
      }
    })
  }

  function openProduct(id: string) {
    const p = products.value.find(x => x.id === id)
    if (!p) {
      showToast('Product not found')
      return
    }
    /* Capture list scroll before browse DOM is torn down (v-if). */
    if (view.value === 'browse') {
      captureBrowseScroll()
    }
    activeProductId.value = id
    view.value = 'detail'
    emitHost('ShopProductOpen', { id, name: p.name })
  }

  function backToBrowse() {
    view.value = 'browse'
    activeProductId.value = null
    scheduleBrowseScrollRestore()
  }

  function addToCart(id: string, delta = 1) {
    const p = products.value.find(x => x.id === id)
    if (!p || p.stock <= 0) {
      showToast('Out of stock')
      return
    }
    let cur = cart[id] || 0
    cur += delta
    if (cur <= 0) {
      delete cart[id]
    } else {
      if (cur > p.stock) {
        cur = p.stock
        showToast('Max stock reached')
      }
      cart[id] = cur
    }
    if (delta > 0) cartOpen.value = true
  }

  function removeFromCart(id: string) {
    delete cart[id]
  }

  function clearCart() {
    for (const id of Object.keys(cart)) delete cart[id]
    showToast('Cart cleared')
  }

  function checkout() {
    if (cartCount.value === 0) {
      showToast('Cart is empty')
      return
    }
    const items: Record<string, number> = {}
    for (const id of Object.keys(cart)) items[id] = cart[id]
    emitHost('ShopCheckout', {
      items,
      total: total.value,
      subtotal: subtotal.value,
      shipping: shipping.value,
      count: cartCount.value
    })
    showToast('Checkout queued · escrow pending', 2800)
  }

  function money(n: number) {
    return formatMoney(n)
  }

  return {
    catalog,
    view,
    activeProductId,
    activeProduct,
    category,
    sort,
    query,
    cartOpen,
    cart,
    toast,
    products,
    filtered,
    cartCount,
    subtotal,
    shipping,
    total,
    onBackToIndex,
    emitHost,
    showToast,
    setCatalog,
    upsertProduct,
    openProduct,
    backToBrowse,
    captureBrowseScroll,
    clearBrowseScroll,
    addToCart,
    removeFromCart,
    clearCart,
    checkout,
    money
  }
}

function defaultEmitHost(eventName: string, payload?: unknown) {
  try {
    const w = window as Window & {
      H5UI?: { emit: (type: string, name: string, payload?: unknown) => void }
      dispatchEvent: (e: Event) => boolean
    }
    if (typeof CustomEvent === 'function') {
      w.dispatchEvent(new CustomEvent('CommanderOS.' + eventName, { detail: payload }))
    }
    if (w.H5UI && typeof w.H5UI.emit === 'function') {
      w.H5UI.emit('CommanderOS', eventName, payload ?? '')
    }
  } catch {
    /* ignore bridge failures */
  }
}
