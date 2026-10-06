<script setup lang="ts">
/**
 * Chooses generic ProductDetail vs a registered special page component.
 */
import { computed } from '@h5ui-plugin/vue'
import type { ShopProduct } from '../types'
import { resolveSpecialPage } from '../special'
import ProductDetail from './ProductDetail.vue'

const props = defineProps<{
  product: ShopProduct
  money: (n: number) => string
}>()

const emit = defineEmits<{
  back: []
  add: []
}>()

const special = computed(() => resolveSpecialPage(props.product.detail?.specialPage))
</script>

<template>
  <component
    v-if="special"
    :is="special"
    :product="product"
    :money="money"
    @back="emit('back')"
    @add="emit('add')"
  />
  <ProductDetail
    v-else
    :product="product"
    :money="money"
    @back="emit('back')"
    @add="emit('add')"
  />
</template>
