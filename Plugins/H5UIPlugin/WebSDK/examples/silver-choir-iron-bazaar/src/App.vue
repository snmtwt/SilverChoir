<script setup lang="ts">
import { inject, onBeforeUnmount, onMounted, ref } from '@h5ui-plugin/vue'
import type { ShopStore } from './shop/state'
import ProductCard from './components/ProductCard.vue'
import CartPanel from './components/CartPanel.vue'
import ProductDetailHost from './components/ProductDetailHost.vue'
import './styles/shop.css'

const store = inject<ShopStore>('shopStore')
if (!store) {
  throw new Error('Iron Bazaar: shopStore not provided')
}

const rootEl = ref<HTMLElement | null>(null)
const layoutClass = ref('is-wide')
let resizeObserver: ResizeObserver | null = null

function updateLayout(): void {
  const width = rootEl.value?.getBoundingClientRect().width ?? window.innerWidth
  layoutClass.value = width < 520
    ? 'is-compact'
    : width < 820
      ? 'is-narrow'
      : width < 1180
        ? 'is-regular'
        : 'is-wide'
}

onMounted(() => {
  updateLayout()
  if (typeof ResizeObserver !== 'undefined' && rootEl.value) {
    resizeObserver = new ResizeObserver(() => updateLayout())
    resizeObserver.observe(rootEl.value)
    return
  }
  window.addEventListener('resize', updateLayout)
})

onBeforeUnmount(() => {
  if (resizeObserver) resizeObserver.disconnect()
  window.removeEventListener('resize', updateLayout)
})

</script>

<template>
  <div ref="rootEl" class="iron-bazaar-root" :class="layoutClass" data-module="iron-bazaar">
    <header v-show="store.view.value === 'browse'" class="shop-topbar">
      <div class="shop-brand">
        <div class="shop-brand-mark" aria-hidden="true">
          <span class="shop-brand-mark-bar"></span>
          <span class="shop-brand-mark-grip"></span>
        </div>
        <div class="shop-brand-text">
          <div class="shop-brand-name">{{ store.catalog.value.title || 'Iron Bazaar' }}</div>
          <div class="shop-brand-tag">{{ store.catalog.value.tagline || 'Marketplace' }}</div>
        </div>
      </div>

      <div class="shop-search">
        <input
          class="shop-search-input"
          type="text"
          placeholder="Search rifles, ammo, optics…"
          autocomplete="off"
          :value="store.query.value"
          @input="store.query.value = ($event.target as HTMLInputElement).value"
        />
      </div>

      <button
        class="shop-cart-btn"
        type="button"
        title="Cart"
        @click="store.cartOpen.value = !store.cartOpen.value"
      >
        <span class="shop-cart-label">Cart</span>
        <span class="shop-cart-count">{{ store.cartCount.value }}</span>
      </button>
    </header>

    <!-- Browse -->
    <template v-if="store.view.value === 'browse'">
      <section class="shop-promo">
        <div>
          <div class="shop-promo-title">Contractor Resupply Sale</div>
          <div class="shop-promo-desc">
            Free FOB pickup on orders over {{ store.money(store.catalog.value.freeShippingOver || 2500) }}
            · Escrow available · No refunds on opened crates
          </div>
        </div>
        <div class="shop-promo-pills">
          <span class="shop-promo-pill">Ships 24–72h</span>
          <span class="shop-promo-pill">Verified sellers</span>
          <span class="shop-promo-pill">Bulk discount</span>
        </div>
      </section>

      <section class="shop-controls" aria-label="Catalog controls">
      <nav class="shop-cats" aria-label="Categories">
        <button
          v-for="cat in store.catalog.value.categories || []"
          :key="cat.id"
          class="shop-cat"
          :class="{ 'is-active': store.category.value === cat.id }"
          type="button"
          @click="store.category.value = cat.id"
        >
          {{ cat.label }}
        </button>
      </nav>

      <div class="shop-toolbar">
        <div class="shop-sort">
          <span class="shop-sort-label">Sort</span>
          <button
            class="shop-sort-btn"
            :class="{ 'is-active': store.sort.value === 'featured' }"
            type="button"
            @click="store.sort.value = 'featured'"
          >Featured</button>
          <button
            class="shop-sort-btn"
            :class="{ 'is-active': store.sort.value === 'price-asc' }"
            type="button"
            @click="store.sort.value = 'price-asc'"
          >Price ↑</button>
          <button
            class="shop-sort-btn"
            :class="{ 'is-active': store.sort.value === 'price-desc' }"
            type="button"
            @click="store.sort.value = 'price-desc'"
          >Price ↓</button>
          <button
            class="shop-sort-btn"
            :class="{ 'is-active': store.sort.value === 'rating' }"
            type="button"
            @click="store.sort.value = 'rating'"
          >Rating</button>
        </div>
      </div>
      </section>

      <div class="shop-body">
        <div class="shop-main" data-shop-scroll data-scroll-region>
          <div v-if="store.filtered.value.length" class="shop-grid">
            <ProductCard
              v-for="p in store.filtered.value"
              :key="p.id"
              :product="p"
              :money="store.money"
              @open="store.openProduct"
              @add="(id) => store.addToCart(id, 1)"
            />
          </div>
          <div v-else class="shop-empty">
            <div class="shop-empty-title">No matches</div>
            <div class="shop-empty-desc">Try another category or clear your search.</div>
            <button
              class="shop-btn shop-btn-secondary"
              type="button"
              @click="store.category.value = 'all'; store.query.value = ''; store.sort.value = 'featured'"
            >Reset filters</button>
          </div>
        </div>

        <CartPanel
          :open="store.cartOpen.value"
          :cart="store.cart"
          :products="store.products.value"
          :subtotal="store.subtotal.value"
          :shipping="store.shipping.value"
          :total="store.total.value"
          :money="store.money"
          @close="store.cartOpen.value = false"
          @inc="(id) => store.addToCart(id, 1)"
          @dec="(id) => store.addToCart(id, -1)"
          @remove="store.removeFromCart"
          @clear="store.clearCart"
          @checkout="store.checkout"
        />
      </div>
    </template>

    <!-- Detail (generic or special) -->
    <div v-else-if="store.activeProduct.value" class="shop-detail-wrap">
      <div class="shop-detail-main">
        <ProductDetailHost
          :product="store.activeProduct.value"
          :money="store.money"
          @back="store.backToBrowse"
          @add="store.addToCart(store.activeProduct.value!.id, 1)"
        />
      </div>
      <CartPanel
        :open="store.cartOpen.value"
        :cart="store.cart"
        :products="store.products.value"
        :subtotal="store.subtotal.value"
        :shipping="store.shipping.value"
        :total="store.total.value"
        :money="store.money"
        @close="store.cartOpen.value = false"
        @inc="(id) => store.addToCart(id, 1)"
        @dec="(id) => store.addToCart(id, -1)"
        @remove="store.removeFromCart"
        @clear="store.clearCart"
        @checkout="store.checkout"
      />
    </div>

    <footer v-if="store.view.value === 'browse'" class="shop-footer">
      <span>Secure escrow · optional</span>
      <span class="shop-footer-dot"></span>
      <span>FOB pickup default</span>
      <span class="shop-footer-dot"></span>
      <span>Seller ratings community-sourced</span>
    </footer>

    <div v-if="store.toast.value" class="shop-toast">{{ store.toast.value }}</div>
  </div>
</template>
