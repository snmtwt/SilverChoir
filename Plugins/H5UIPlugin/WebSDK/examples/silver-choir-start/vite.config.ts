import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import { resolve } from 'node:path'
import { readFileSync } from 'node:fs'

export default defineConfig({
  plugins: [
    vue({ template: { compilerOptions: { hoistStatic: false } } }),
    {
      name: 'emit-localization-config',
      generateBundle() {
        this.emitFile({
          type: 'asset',
          fileName: 'localization.json',
          source: readFileSync(resolve(__dirname, 'src/localization.json'), 'utf8')
        })
      }
    }
  ],
  define: {
    'process.env.NODE_ENV': JSON.stringify('production')
  },
  build: {
    target: 'es2020',
    minify: true,
    outDir: resolve(__dirname, '../../../../../Content/GameCore/H5UI/MainMenu'),
    emptyOutDir: false,
    cssCodeSplit: false,
    lib: {
      entry: resolve(__dirname, 'src/main.ts'),
      name: 'SilverChoirStartMenu',
      formats: ['iife'],
      fileName: () => 'start-menu.js'
    },
    rollupOptions: {
      output: {
        inlineDynamicImports: true,
        assetFileNames: asset => asset.name?.endsWith('.css')
          ? 'start-menu.css'
          : '[name][extname]'
      }
    }
  }
})
