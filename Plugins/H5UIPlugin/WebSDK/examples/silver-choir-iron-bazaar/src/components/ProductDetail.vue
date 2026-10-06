<script setup lang="ts">
/**
 * Generic product / weapon introduction page.
 * All text and images come from catalog JSON (or UE-injected product.detail).
 */
import { computed } from '@h5ui-plugin/vue'
import type { ShopProduct } from '../types'
import ProductMedia from './ProductMedia.vue'

const props = defineProps<{
  product: ShopProduct
  money: (n: number) => string
}>()

const emit = defineEmits<{
  back: []
  add: []
}>()

const detail = computed(() => props.product.detail || {})
const paragraphs = computed(() => {
  if (detail.value.longDesc && detail.value.longDesc.length) return detail.value.longDesc
  return [props.product.desc]
})
const specs = computed(() => detail.value.specs || [])
const highlights = computed(() => detail.value.highlights || [])
const seller = computed(() => detail.value.seller)
const gallery = computed(() => detail.value.gallery || [])
const tags = computed(() => detail.value.tags || [])

const stock = computed(() => {
  const s = props.product.stock
  if (s <= 0) return { text: 'Out of stock', cls: 'is-out' }
  if (s <= 4) return { text: 'Only ' + s + ' left', cls: 'is-low' }
  return { text: 'In stock · ' + s + ' units', cls: '' }
})
</script>

<template>
  <div class="pdp">
    <nav class="pdp-breadcrumb">
      <button class="shop-link-back" type="button" @click="emit('back')">← Back to catalog</button>
      <span class="pdp-crumb-sep">/</span>
      <span class="pdp-crumb-cat">{{ product.cat }}</span>
      <span class="pdp-crumb-sep">/</span>
      <span class="pdp-crumb-name">{{ product.name }}</span>
    </nav>

    <div class="pdp-hero">
      <div class="pdp-media-col">
        <ProductMedia :product="product" size="hero" />
        <div v-if="gallery.length" class="pdp-gallery">
          <div v-for="(src, i) in gallery" :key="i" class="pdp-gallery-item">
            <img :src="src" :alt="product.name + ' ' + (i + 1)" />
          </div>
        </div>
      </div>

      <div class="pdp-info-col">
        <div class="pdp-cat">{{ product.cat }}</div>
        <h1 class="pdp-title">{{ product.name }}</h1>
        <p v-if="detail.subtitle" class="pdp-subtitle">{{ detail.subtitle }}</p>

        <div class="pdp-meta">
          <span class="pdp-rating">★ {{ product.rating.toFixed(1) }}</span>
          <span class="pdp-sold">{{ product.sold }} sold</span>
          <span v-if="detail.condition" class="pdp-condition">{{ detail.condition }}</span>
        </div>

        <div class="pdp-price-row">
          <span class="pdp-price">{{ money(product.price) }}</span>
          <span v-if="product.was && product.was > product.price" class="pdp-was">
            {{ money(product.was) }}
          </span>
          <span v-if="product.badge" class="pdp-badge">{{ product.badge }}</span>
        </div>

        <div class="pdp-stock" :class="stock.cls">{{ stock.text }}</div>

        <div v-if="tags.length" class="pdp-tags">
          <span v-for="t in tags" :key="t" class="pdp-tag">{{ t }}</span>
        </div>

        <button
          class="shop-btn shop-btn-primary pdp-add"
          type="button"
          :disabled="product.stock <= 0"
          @click="emit('add')"
        >
          {{ product.stock <= 0 ? 'Sold out' : 'Add to cart' }}
        </button>

        <div v-if="seller" class="pdp-seller">
          <div class="pdp-seller-label">Seller</div>
          <div class="pdp-seller-name">{{ seller.name }}</div>
          <div class="pdp-seller-meta">
            <span v-if="seller.rating != null">★ {{ seller.rating }}</span>
            <span v-if="seller.deals != null"> · {{ seller.deals }} deals</span>
          </div>
          <p v-if="seller.note" class="pdp-seller-note">{{ seller.note }}</p>
        </div>
      </div>
    </div>

    <section class="pdp-section">
      <h2 class="pdp-section-title">Overview</h2>
      <p v-for="(para, i) in paragraphs" :key="i" class="pdp-para">{{ para }}</p>
      <ul v-if="highlights.length" class="pdp-highlights">
        <li v-for="h in highlights" :key="h">{{ h }}</li>
      </ul>
    </section>

    <section v-if="specs.length" class="pdp-section">
      <h2 class="pdp-section-title">Specifications</h2>
      <div class="pdp-specs">
        <div v-for="row in specs" :key="row.label" class="pdp-spec-row">
          <span class="pdp-spec-label">{{ row.label }}</span>
          <span class="pdp-spec-value">{{ row.value }}</span>
        </div>
      </div>
    </section>
  </div>
</template>

<style scoped>
.pdp {
  padding: 4px 2px 20px;
}
.pdp-breadcrumb {
  display: flex;
  flex-direction: row;
  flex-wrap: wrap;
  align-items: center;
  margin-bottom: 14px;
  font-size: 12px;
  color: #8a8478;
}
.shop-link-back {
  padding: 4px 0;
  color: #8a6230;
  background: transparent;
  border: none;
  font-size: 12px;
  font-weight: 600;
  cursor: pointer;
}
.pdp-crumb-sep { margin: 0 8px; color: #c8c0b0; }
.pdp-crumb-cat { text-transform: capitalize; }
.pdp-crumb-name { color: #3a342c; font-weight: 600; }

.pdp-hero {
  display: flex;
  flex-direction: row;
  flex-wrap: wrap;
  margin-bottom: 20px;
  background: #ffffff;
  border: 1px solid #e2ddd2;
  border-radius: 14px;
  overflow: hidden;
}
.pdp-media-col {
  width: 44%;
  min-width: 220px;
  background: #ece8e0;
}
.pdp-gallery {
  display: flex;
  flex-direction: row;
  flex-wrap: wrap;
  padding: 8px;
  gap: 0;
}
.pdp-gallery-item {
  width: 56px;
  height: 56px;
  margin: 4px;
  border: 1px solid #ddd6c8;
  border-radius: 8px;
  overflow: hidden;
  background: #f7f4ee;
}
.pdp-gallery-item img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}
.pdp-info-col {
  flex: 1;
  min-width: 220px;
  padding: 20px 22px;
}
.pdp-cat {
  margin-bottom: 6px;
  color: #9a9284;
  font-size: 10px;
  letter-spacing: 1px;
  font-weight: 700;
  text-transform: uppercase;
}
.pdp-title {
  margin: 0 0 8px;
  color: #1e1c18;
  font-size: 24px;
  font-weight: 600;
  line-height: 1.2;
}
.pdp-subtitle {
  margin: 0 0 12px;
  color: #6a6558;
  font-size: 13px;
  line-height: 1.45;
}
.pdp-meta {
  display: flex;
  flex-direction: row;
  flex-wrap: wrap;
  align-items: center;
  margin-bottom: 14px;
  font-size: 12px;
}
.pdp-rating { color: #a08040; font-weight: 700; margin-right: 12px; }
.pdp-sold { color: #8a8478; margin-right: 12px; }
.pdp-condition {
  padding: 2px 8px;
  color: #4a3e28;
  background: #f0e8d4;
  border-radius: 4px;
  font-weight: 600;
}
.pdp-price-row {
  display: flex;
  flex-direction: row;
  flex-wrap: wrap;
  align-items: baseline;
  margin-bottom: 8px;
}
.pdp-price {
  margin-right: 10px;
  color: #1e1c18;
  font-size: 28px;
  font-weight: 700;
}
.pdp-was {
  margin-right: 10px;
  color: #9a9284;
  font-size: 13px;
  text-decoration: line-through;
}
.pdp-badge {
  padding: 3px 8px;
  color: #1e1c18;
  background: #d2b478;
  border-radius: 4px;
  font-size: 10px;
  font-weight: 700;
  letter-spacing: 0.6px;
}
.pdp-stock {
  margin-bottom: 12px;
  color: #2a8a6a;
  font-size: 12px;
  font-weight: 600;
}
.pdp-stock.is-low { color: #b06030; }
.pdp-stock.is-out { color: #a04040; }
.pdp-tags {
  display: flex;
  flex-direction: row;
  flex-wrap: wrap;
  margin-bottom: 14px;
}
.pdp-tag {
  margin: 0 6px 6px 0;
  padding: 4px 8px;
  color: #5a5348;
  background: #f3f0ea;
  border: 1px solid #e2ddd2;
  border-radius: 12px;
  font-size: 11px;
}
.pdp-add {
  max-width: 280px;
  margin-bottom: 16px;
}
.pdp-seller {
  padding: 12px 14px;
  background: #f7f4ee;
  border: 1px solid #e2ddd2;
  border-radius: 10px;
}
.pdp-seller-label {
  margin-bottom: 4px;
  color: #8a8478;
  font-size: 10px;
  letter-spacing: 1px;
  font-weight: 700;
  text-transform: uppercase;
}
.pdp-seller-name {
  color: #1e1c18;
  font-size: 14px;
  font-weight: 600;
}
.pdp-seller-meta {
  margin-top: 2px;
  color: #8a8478;
  font-size: 11px;
}
.pdp-seller-note {
  margin: 8px 0 0;
  color: #6a6558;
  font-size: 12px;
  line-height: 1.4;
}
.pdp-section {
  margin-bottom: 16px;
  padding: 16px 18px;
  background: #ffffff;
  border: 1px solid #e2ddd2;
  border-radius: 12px;
}
.pdp-section-title {
  margin: 0 0 12px;
  color: #1e1c18;
  font-size: 15px;
  font-weight: 700;
}
.pdp-para {
  margin: 0 0 10px;
  color: #5a5348;
  font-size: 13px;
  line-height: 1.55;
}
.pdp-highlights {
  margin: 8px 0 0;
  padding-left: 18px;
  color: #3a342c;
  font-size: 13px;
  line-height: 1.5;
}
.pdp-specs {
  display: flex;
  flex-direction: column;
}
.pdp-spec-row {
  display: flex;
  flex-direction: row;
  justify-content: space-between;
  padding: 10px 0;
  border-bottom: 1px solid #f0ebe2;
  font-size: 13px;
}
.pdp-spec-row:last-child { border-bottom: none; }
.pdp-spec-label { color: #8a8478; font-weight: 600; }
.pdp-spec-value { color: #1e1c18; font-weight: 600; text-align: right; }

@media (max-width: 960px) {
  .pdp-media-col { width: 100%; }
}
</style>
