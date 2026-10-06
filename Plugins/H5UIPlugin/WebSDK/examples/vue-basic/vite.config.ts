import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import { resolve } from 'node:path'

export default defineConfig({
  plugins: [vue({ template: { compilerOptions: { hoistStatic: false } } })],
  define: {
    'process.env.NODE_ENV': JSON.stringify('production')
  },
  build: {
    target: 'es2020',
    minify: true,
    outDir: resolve(__dirname, '../../../Resources/UI/vue-basic'),
    emptyOutDir: true,
    cssCodeSplit: false,
    lib: {
      entry: resolve(__dirname, 'src/main.ts'),
      name: 'H5UIVueBasicExample',
      formats: ['iife'],
      fileName: () => 'vue-basic.js'
    },
    rollupOptions: {
      output: {
        inlineDynamicImports: true,
        assetFileNames: asset => asset.name?.endsWith('.css') ? 'vue-basic.css' : '[name][extname]'
      }
    }
  }
})
