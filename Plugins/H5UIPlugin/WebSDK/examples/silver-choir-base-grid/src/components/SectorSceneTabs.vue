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
  <section class="scene-deck hud-panel hud-interactive">
    <header>
      <small>{{ heading }}</small>
      <strong>{{ hint }}</strong>
    </header>
    <div class="scene-deck-options">
      <button
        v-for="scene in scenes"
        :id="'scene-' + scene.id"
        :key="scene.id"
        class="scene-deck-button"
        :class="{ active: activeScene === scene.id }"
        type="button"
        @click="$emit('select', scene.id)"
      >
        <span>{{ scene.code }}</span>
        <div>
          <strong>{{ scene.label }}</strong>
          <small>{{ scene.sublabel }}</small>
        </div>
        <i></i><b></b>
      </button>
    </div>
  </section>
</template>
