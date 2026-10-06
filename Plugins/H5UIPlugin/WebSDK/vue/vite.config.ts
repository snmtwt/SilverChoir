import { defineConfig } from 'vite'
import { resolve } from 'node:path'

export default defineConfig({
  define: {
    'process.env.NODE_ENV': JSON.stringify('production')
  },
  build: {
    target: 'es2020',
    minify: true,
    outDir: resolve(__dirname, '../../Resources/UI/sdk'),
    emptyOutDir: false,
    lib: {
      entry: resolve(__dirname, 'src/index.ts'),
      name: 'H5UIVue',
      formats: ['iife'],
      fileName: () => 'h5ui-vue.global.js'
    },
    rollupOptions: {
      output: {
        inlineDynamicImports: true
      }
    }
  }
})
