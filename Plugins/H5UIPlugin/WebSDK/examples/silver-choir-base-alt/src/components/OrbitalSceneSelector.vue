<script setup lang="ts">
import type { SceneId, SceneOption } from '../types'

defineProps<{
  scenes: SceneOption[]
  activeScene: SceneId
  heading: string
  hint: string
}>()

defineEmits<{
  select: [scene: SceneId]
}>()
</script>

<template>
  <section class="orbital-scene hud-interactive">
    <div class="orbit-ring"><i></i><i></i><i></i><i></i></div>
    <div class="orbit-ring orbit-ring-inner"><i></i><i></i></div>
    <header class="orbit-heading">
      <small>{{ heading }}</small>
      <strong>{{ hint }}</strong>
    </header>
    <div class="orbit-core">
      <span>SC</span>
      <small>02 / ZONE</small>
    </div>
    <button
      v-for="scene in scenes"
      :id="'scene-' + scene.id"
      :key="scene.id"
      class="orbit-scene-button"
      :class="['orbit-' + scene.id, { active: activeScene === scene.id }]"
      type="button"
      @click="$emit('select', scene.id)"
    >
      <span>{{ scene.code }}</span>
      <strong>{{ scene.label }}</strong>
      <small>{{ scene.sublabel }}</small>
      <i></i>
    </button>
  </section>
</template>
