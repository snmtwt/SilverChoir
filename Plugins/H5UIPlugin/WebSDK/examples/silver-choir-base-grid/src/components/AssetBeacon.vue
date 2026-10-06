<script setup lang="ts">
defineProps<{
  channel: string
  network: string
  heading: string
  currencyName: string
  amount: number
  delta: number
  deltaLabel: string
}>()

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
  <section class="asset-beacon hud-panel">
    <div class="beacon-brand">
      <div class="grid-sigil"><span></span><i></i><b></b></div>
      <div><strong>{{ channel }}</strong><small>{{ network }}</small></div>
    </div>
    <div class="beacon-value">
      <small>{{ heading }} / {{ currencyName }}</small>
      <strong id="currency-amount" :key="amount">{{ formatAmount(amount) }}</strong>
      <span id="currency-delta" :class="{ negative: delta < 0 }">{{ delta >= 0 ? '+' : '' }}{{ formatAmount(delta) }} / {{ deltaLabel }}</span>
    </div>
  </section>
</template>
