<script setup lang="ts">
import { computed } from '@h5ui-plugin/vue'
import type { ShopProduct } from '../types'

const props = withDefaults(defineProps<{
  product: ShopProduct
  size?: 'card' | 'hero'
}>(), {
  size: 'card'
})

const imageSrc = computed(() => {
  const d = props.product.detail?.image
  const list = props.product.image
  const src = (d && d.length) ? d : (list && list.length ? list : '')
  return src || ''
})

const catClass = computed(() => 'is-cat-' + (props.product.cat || 'gear'))
</script>

<template>
  <div
    class="product-media"
    :class="[catClass, size === 'hero' ? 'is-hero' : 'is-card']"
  >
    <!-- 16:9 frame via padding-bottom (no calc) -->
    <div class="product-media-ratio" aria-hidden="true"></div>
    <div class="product-media-inner">
      <img
        v-if="imageSrc"
        class="product-media-img"
        :src="imageSrc"
        :alt="product.name"
      />
      <div v-else class="product-glyph" :class="'glyph-' + product.cat" aria-hidden="true">
        <span class="glyph-a"></span>
        <span class="glyph-b"></span>
        <span class="glyph-c"></span>
      </div>
    </div>
  </div>
</template>

<style scoped>
.product-media {
  position: relative;
  width: 100%;
  overflow: hidden;
  background-color: #e8e4dc;
}

/* Card: always 16:9 (9/16 = 56.25%) */
.product-media.is-card .product-media-ratio {
  display: block;
  width: 100%;
  height: 0;
  padding-bottom: 56.25%;
}

.product-media.is-card .product-media-inner {
  position: absolute;
  top: 0;
  right: 0;
  bottom: 0;
  left: 0;
  display: flex;
  align-items: center;
  justify-content: center;
}

/* Hero: fill parent column; keep 16:9 when tall enough */
.product-media.is-hero {
  height: 100%;
  min-height: 280px;
  background-color: #1a1e28;
}

.product-media.is-hero .product-media-ratio {
  display: none;
}

.product-media.is-hero .product-media-inner {
  position: absolute;
  top: 0;
  right: 0;
  bottom: 0;
  left: 0;
  display: flex;
  align-items: center;
  justify-content: center;
}

.product-media.is-cat-rifles { background-color: #e6e2da; }
.product-media.is-cat-smgs { background-color: #e2e6ea; }
.product-media.is-cat-sidearms { background-color: #eae4e0; }
.product-media.is-cat-ammo { background-color: #e4e8e0; }
.product-media.is-cat-optics { background-color: #e0e6ee; }
.product-media.is-cat-gear { background-color: #e8e4e0; }

.product-media.is-hero.is-cat-rifles,
.product-media.is-hero.is-cat-smgs,
.product-media.is-hero.is-cat-sidearms,
.product-media.is-hero.is-cat-ammo,
.product-media.is-hero.is-cat-optics,
.product-media.is-hero.is-cat-gear {
  background-color: #1a1e28;
}

.product-media-img {
  width: 100%;
  height: 100%;
  object-fit: cover;
  object-position: center center;
}

/* Soft vignette feel for cover images */
.product-media.is-card .product-media-img {
  transform: scale(1.01);
}

.product-glyph {
  position: relative;
  width: 88px;
  height: 64px;
  opacity: 0.92;
}

.product-media.is-hero .product-glyph {
  width: 110px;
  height: 80px;
}

.glyph-a,
.glyph-b,
.glyph-c {
  position: absolute;
  background-color: #3a3e48;
  border-radius: 2px;
}

.glyph-a { top: 26px; left: 6px; width: 72px; height: 8px; }
.glyph-b { top: 34px; left: 20px; width: 14px; height: 22px; }
.glyph-c { top: 16px; left: 56px; width: 12px; height: 16px; background-color: #5a5e68; }

.glyph-ammo .glyph-a {
  top: 12px;
  left: 24px;
  width: 16px;
  height: 40px;
  border-radius: 2px 2px 5px 5px;
}
.glyph-ammo .glyph-b {
  top: 8px;
  left: 46px;
  width: 16px;
  height: 44px;
  border-radius: 2px 2px 5px 5px;
  background-color: #5a5e68;
}
.glyph-ammo .glyph-c { display: none; }

.glyph-optics .glyph-a {
  top: 22px;
  left: 10px;
  width: 68px;
  height: 14px;
  border-radius: 8px;
}
.glyph-optics .glyph-b {
  top: 18px;
  left: 34px;
  width: 22px;
  height: 22px;
  border-radius: 50%;
  background-color: #6a7080;
}
.glyph-optics .glyph-c { display: none; }

.glyph-gear .glyph-a {
  top: 14px;
  left: 18px;
  width: 52px;
  height: 38px;
  border-radius: 5px;
}
.glyph-gear .glyph-b {
  top: 24px;
  left: 30px;
  width: 28px;
  height: 14px;
  background-color: #6a7080;
}
.glyph-gear .glyph-c { display: none; }

.is-hero .glyph-a,
.is-hero .glyph-b,
.is-hero .glyph-c {
  background-color: #8a909c;
}
</style>
