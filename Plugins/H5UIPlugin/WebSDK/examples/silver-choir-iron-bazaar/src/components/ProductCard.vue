<script setup lang="ts">
import type { ShopProduct } from '../types'
import ProductMedia from './ProductMedia.vue'

const props = defineProps<{
  product: ShopProduct
  money: (n: number) => string
}>()

const emit = defineEmits<{
  open: [id: string]
  add: [id: string]
}>()

function onAdd(e: Event) {
  if (e && (e as MouseEvent).stopPropagation) (e as MouseEvent).stopPropagation()
  emit('add', props.product.id)
}

function stockLabel() {
  const s = props.product.stock
  if (s <= 0) return { text: 'Out of stock', cls: 'is-out' }
  if (s <= 4) return { text: 'Only ' + s + ' left', cls: 'is-low' }
  return { text: 'In stock', cls: '' }
}

const stock = stockLabel()
const badgeClass = props.product.badge === 'SALE'
  ? 'is-sale'
  : props.product.badge === 'NEW'
    ? 'is-new'
    : ''
</script>

<template>
  <article class="shop-product" @click="emit('open', product.id)">
    <div class="shop-product-media-wrap">
      <ProductMedia :product="product" size="card" />
      <span v-if="product.badge" class="shop-product-badge" :class="badgeClass">
        {{ product.badge }}
      </span>
    </div>

    <div class="shop-product-body">
      <div class="shop-product-head">
        <div class="shop-product-cat">{{ product.cat }}</div>
        <h3 class="shop-product-name">{{ product.name }}</h3>
        <div class="shop-product-meta">
          <span class="shop-product-rating">★ {{ product.rating.toFixed(1) }}</span>
          <span class="shop-product-dot" aria-hidden="true"></span>
          <span class="shop-product-sold">{{ product.sold }} sold</span>
        </div>
      </div>

      <p class="shop-product-desc">{{ product.desc }}</p>

      <div class="shop-product-foot">
        <div class="shop-product-price-block">
          <span class="shop-product-price">{{ money(product.price) }}</span>
          <span v-if="product.was && product.was > product.price" class="shop-product-was">
            {{ money(product.was) }}
          </span>
        </div>
        <div class="shop-product-stock" :class="stock.cls">{{ stock.text }}</div>
      </div>

      <button
        class="shop-btn shop-btn-primary shop-product-cta"
        type="button"
        :disabled="product.stock <= 0"
        @click="onAdd"
      >
        {{ product.stock <= 0 ? 'Sold out' : 'Add to cart' }}
      </button>
    </div>
  </article>
</template>
