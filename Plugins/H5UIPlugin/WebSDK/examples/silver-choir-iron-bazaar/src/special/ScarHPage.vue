<script setup lang="ts">
/**
 * Example special product page — only used when detail.specialPage === 'scar-h'.
 * Replace or extend this pattern for unique weapons later.
 */
import type { ShopProduct } from '../types'
import ProductMedia from '../components/ProductMedia.vue'

const props = defineProps<{
  product: ShopProduct
  money: (n: number) => string
}>()

const emit = defineEmits<{
  add: []
  back: []
}>()
</script>

<template>
  <div class="special-scar">
    <button class="shop-link-back" type="button" @click="emit('back')">← Back to catalog</button>

    <div class="special-scar-hero">
      <div class="special-scar-media">
        <ProductMedia :product="product" size="hero" />
        <div class="special-scar-ribbon">LIMITED DROP</div>
      </div>
      <div class="special-scar-copy">
        <div class="special-scar-kicker">SPECIAL LISTING · SCAR PLATFORM</div>
        <h1 class="special-scar-title">{{ product.name }}</h1>
        <p class="special-scar-sub">{{ product.detail?.subtitle || product.desc }}</p>
        <div class="special-scar-price">{{ money(product.price) }}</div>
        <p
          v-for="(para, i) in (product.detail?.longDesc || [product.desc])"
          :key="i"
          class="special-scar-para"
        >
          {{ para }}
        </p>
        <ul class="special-scar-list" v-if="product.detail?.highlights?.length">
          <li v-for="h in product.detail.highlights" :key="h">{{ h }}</li>
        </ul>
        <button class="shop-btn shop-btn-primary" type="button" @click="emit('add')">
          Add to cart
        </button>
      </div>
    </div>
  </div>
</template>

<style scoped>
.special-scar {
  padding: 8px 4px 20px;
}
.shop-link-back {
  margin-bottom: 14px;
  padding: 6px 0;
  color: #8a6230;
  background: transparent;
  border: none;
  font-size: 12px;
  font-weight: 600;
  cursor: pointer;
}
.special-scar-hero {
  display: flex;
  flex-direction: row;
  flex-wrap: wrap;
  background: #141820;
  border: 1px solid #3a342c;
  border-radius: 14px;
  overflow: hidden;
}
.special-scar-media {
  position: relative;
  width: 42%;
  min-width: 220px;
  min-height: 260px;
  background: #1a1e28;
}
.special-scar-ribbon {
  position: absolute;
  top: 14px;
  left: 14px;
  padding: 4px 10px;
  color: #1e1c18;
  background: #d2b478;
  border-radius: 4px;
  font-size: 10px;
  letter-spacing: 1px;
  font-weight: 700;
}
.special-scar-copy {
  flex: 1;
  min-width: 220px;
  padding: 22px 22px 20px;
  color: #e8e2d4;
}
.special-scar-kicker {
  margin-bottom: 8px;
  color: #d2b478;
  font-size: 10px;
  letter-spacing: 1.6px;
  font-weight: 700;
}
.special-scar-title {
  margin: 0 0 8px;
  font-size: 26px;
  font-weight: 600;
}
.special-scar-sub {
  margin: 0 0 12px;
  color: #a8a298;
  font-size: 13px;
}
.special-scar-price {
  margin-bottom: 14px;
  color: #f0e8d8;
  font-size: 24px;
  font-weight: 700;
}
.special-scar-para {
  margin: 0 0 10px;
  color: #b0aaa0;
  font-size: 13px;
  line-height: 1.55;
}
.special-scar-list {
  margin: 0 0 16px;
  padding-left: 18px;
  color: #c8c0b0;
  font-size: 12px;
  line-height: 1.5;
}
</style>
