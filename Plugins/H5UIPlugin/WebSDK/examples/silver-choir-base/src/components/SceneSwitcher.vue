<script setup lang="ts">
import { computed, onUnmounted, ref } from '@h5ui-plugin/vue'
import type { SceneId, SceneOption } from '../types'

const props = defineProps<{
  scenes: SceneOption[]
  activeScene: SceneId
}>()

const emit = defineEmits<{
  select: [scene: SceneId]
}>()

const activeOption = computed(() => props.scenes.find(scene => scene.id === props.activeScene) || props.scenes[0])
const inactiveOption = computed(() => props.scenes.find(scene => scene.id !== props.activeScene) || props.scenes[0])
const isSwitching = ref(false)
const switchFromOption = ref<SceneOption | null>(null)
const switchToOption = ref<SceneOption | null>(null)

let switchTimer: ReturnType<typeof setTimeout> | undefined

function selectScene(scene: SceneId): void {
  if (scene === props.activeScene || isSwitching.value) return

  switchFromOption.value = activeOption.value
  switchToOption.value = props.scenes.find(option => option.id === scene) || inactiveOption.value
  isSwitching.value = true
  if (switchTimer !== undefined) clearTimeout(switchTimer)
  emit('select', scene)
  switchTimer = setTimeout(() => {
    isSwitching.value = false
    switchFromOption.value = null
    switchToOption.value = null
    switchTimer = undefined
  }, 360)
}

onUnmounted(() => {
  if (switchTimer !== undefined) clearTimeout(switchTimer)
})
</script>

<template>
  <section class="scene-switcher hud-interactive">
    <button
      :id="'scene-' + activeOption.id"
      class="scene-direct-button hud-panel"
      :class="{ switching: isSwitching }"
      type="button"
      :title="inactiveOption.label"
      @click="selectScene(inactiveOption.id)"
    >
      <span class="panel-tonal-shift" aria-hidden="true"><i></i><i></i><i></i></span>
      <span class="scene-switch-scan" aria-hidden="true"></span>
      <template v-if="isSwitching && switchFromOption && switchToOption">
        <span class="scene-code scene-code-out">{{ switchFromOption.code }}</span>
        <span class="scene-copy scene-copy-out">
          <strong>{{ switchFromOption.label }}</strong>
          <small>{{ switchFromOption.sublabel }}</small>
        </span>
        <span class="scene-code scene-code-in">{{ switchToOption.code }}</span>
        <span class="scene-copy scene-copy-in">
          <strong>{{ switchToOption.label }}</strong>
          <small>{{ switchToOption.sublabel }}</small>
        </span>
        <span class="scene-switch-target scene-switch-target-out">
          <small>{{ switchToOption.code }}</small>
          <strong>{{ switchToOption.label }}</strong>
        </span>
      </template>
      <template v-else>
        <span class="scene-code">{{ activeOption.code }}</span>
        <span class="scene-copy">
          <strong>{{ activeOption.label }}</strong>
          <small>{{ activeOption.sublabel }}</small>
        </span>
        <span class="scene-switch-target">
          <small>{{ inactiveOption.code }}</small>
          <strong>{{ inactiveOption.label }}</strong>
        </span>
      </template>
      <span class="scene-switch-icon">⇄</span>
    </button>
  </section>
</template>
