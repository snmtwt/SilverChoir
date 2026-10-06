<script setup lang="ts">
import { ref, watch } from '@h5ui-plugin/vue'

const props = defineProps<{
  heading: string
  currencyName: string
  amount: number
  iconSource: string
}>()

const iconFailed = ref(false)

watch(() => props.iconSource, () => {
  iconFailed.value = false
})

function formatAmount(value: number): string {
  const rounded = Math.round(value)
  const sign = rounded < 0 ? '-' : ''
  const digits = String(Math.abs(rounded))
  let formatted = ''

  for (let index = 0; index < digits.length; index += 1) {
    if (index > 0 && (digits.length - index) % 3 === 0) formatted += ','
    formatted += digits[index]
  }

  return sign + formatted
}
</script>

<template>
  <section class="currency-panel">
    <span class="currency-icon-frame" aria-hidden="true">
      <span v-if="iconFailed" class="currency-icon-fallback">◇</span>
      <img
        v-else
        id="currency-icon"
        class="currency-icon"
        :src="iconSource"
        alt=""
        @error="iconFailed = true"
      />
    </span>
    <div class="currency-copy">
      <small>{{ heading }} / {{ currencyName }}</small>
      <strong id="currency-amount" :key="amount">{{ formatAmount(amount) }}</strong>
    </div>
  </section>
</template>
