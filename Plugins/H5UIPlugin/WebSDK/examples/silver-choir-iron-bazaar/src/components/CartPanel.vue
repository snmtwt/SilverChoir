<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from '@h5ui-plugin/vue'
import type { ShopProduct } from '../types'

const props = defineProps<{
  open: boolean
  cart: Record<string, number>
  products: ShopProduct[]
  subtotal: number
  shipping: number
  total: number
  money: (n: number) => string
}>()

const emit = defineEmits<{
  close: []
  inc: [id: string]
  dec: [id: string]
  remove: [id: string]
  clear: []
  checkout: []
}>()

const lines = computed(() => {
  const out: { id: string; product: ShopProduct; qty: number }[] = []
  for (const id of Object.keys(props.cart)) {
    const product = props.products.find(p => p.id === id)
    if (product) out.push({ id, product, qty: props.cart[id] })
  }
  return out
})

const empty = computed(() => lines.value.length === 0)

const panelEl = ref<HTMLElement | null>(null)
const headEl = ref<HTMLElement | null>(null)
const contentEl = ref<HTMLElement | null>(null)
const scrollEl = ref<HTMLElement | null>(null)
const summaryEl = ref<HTMLElement | null>(null)
const panelHeight = ref<number | null>(null)
const contentHeight = ref<number | null>(null)
let parentObserver: ResizeObserver | null = null
let observedParent: Element | null = null

const panelStyle = computed(() => panelHeight.value === null
  ? {}
  : { height: `${panelHeight.value}px` })

const contentStyle = computed(() => contentHeight.value === null
  ? {}
  : { height: `${contentHeight.value}px` })

function observeParent(parent: Element): void {
  if (typeof ResizeObserver === 'undefined' || observedParent === parent) return
  if (!parentObserver) parentObserver = new ResizeObserver(() => measureCart())
  parentObserver.disconnect()
  parentObserver.observe(parent)
  observedParent = parent
}

function measureCart(): void {
  const panel = panelEl.value
  const content = contentEl.value
  const scroll = scrollEl.value
  const parent = panel?.parentElement
  if (!panel || !content || !scroll || !parent) {
    panelHeight.value = null
    contentHeight.value = null
    return
  }

  observeParent(parent)

  const availableHeight = parent.clientHeight
  const headHeight = headEl.value?.offsetHeight ?? 0
  const summaryHeight = summaryEl.value?.offsetHeight ?? 0
  const borderHeight = 2
  const chromeHeight = headHeight + summaryHeight + borderHeight
  const naturalContentHeight = scroll.scrollHeight
  const naturalPanelHeight = chromeHeight + naturalContentHeight
  const targetPanelHeight = availableHeight > 0
    ? Math.min(naturalPanelHeight, availableHeight)
    : naturalPanelHeight

  panelHeight.value = Math.max(chromeHeight, Math.round(targetPanelHeight))
  contentHeight.value = Math.max(0, Math.round(targetPanelHeight - chromeHeight))
}

function scheduleMeasure(): void {
  void nextTick(measureCart)
}

watch(
  () => [props.open, lines.value.length, props.total],
  scheduleMeasure,
  { flush: 'post' }
)

onMounted(scheduleMeasure)

onBeforeUnmount(() => {
  parentObserver?.disconnect()
  parentObserver = null
  observedParent = null
})
</script>

<template>
  <aside
    v-if="open"
    ref="panelEl"
    class="shop-cart-panel"
    :style="panelStyle"
    aria-label="Shopping cart"
  >
    <div ref="headEl" class="shop-cart-head">
      <div class="shop-cart-head-title">Your cart</div>
      <button class="shop-cart-close" type="button" title="Close" @click="emit('close')">×</button>
    </div>

    <div ref="contentEl" class="shop-cart-content" :style="contentStyle">
      <div v-if="empty" ref="scrollEl" class="shop-cart-empty">
        Cart is empty. Add gear from the catalog.
      </div>

      <div v-else ref="scrollEl" class="shop-cart-list">
        <div v-for="line in lines" :key="line.id" class="shop-cart-line">
          <div class="shop-cart-line-info">
            <div class="shop-cart-line-name">{{ line.product.name }}</div>
            <div class="shop-cart-line-price">{{ money(line.product.price) }} each</div>
          </div>
          <div class="shop-cart-line-qty">
            <button class="shop-qty-btn" type="button" @click="emit('dec', line.id)">−</button>
            <span class="shop-qty-val">{{ line.qty }}</span>
            <button class="shop-qty-btn" type="button" @click="emit('inc', line.id)">+</button>
          </div>
          <div class="shop-cart-line-sum">{{ money(line.product.price * line.qty) }}</div>
          <button class="shop-cart-remove" type="button" title="Remove" @click="emit('remove', line.id)">×</button>
        </div>
      </div>
    </div>

    <div v-if="!empty" ref="summaryEl" class="shop-cart-summary">
      <div class="shop-cart-row">
        <span>Subtotal</span>
        <span>{{ money(subtotal) }}</span>
      </div>
      <div class="shop-cart-row shop-cart-row-muted">
        <span>FOB handling</span>
        <span>{{ shipping === 0 ? 'FREE' : money(shipping) }}</span>
      </div>
      <div class="shop-cart-row shop-cart-total">
        <span>Total</span>
        <span>{{ money(total) }}</span>
      </div>
      <button class="shop-btn shop-btn-primary" type="button" @click="emit('checkout')">
        Proceed to checkout
      </button>
      <button class="shop-btn shop-btn-ghost" type="button" @click="emit('clear')">Clear cart</button>
    </div>
  </aside>
</template>
