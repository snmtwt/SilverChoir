import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import { resolve } from 'node:path'
import { readFileSync } from 'node:fs'

const outDir = resolve(__dirname, '../../../../../Content/GameCore/H5UI/CommanderOS/V2')

export default defineConfig({
  plugins: [
    vue({ template: { compilerOptions: { hoistStatic: false } } }),
    {
      name: 'emit-iron-bazaar-catalog',
      generateBundle() {
        this.emitFile({
          type: 'asset',
          fileName: 'iron-bazaar.catalog.json',
          source: readFileSync(resolve(__dirname, 'src/data/catalog.json'), 'utf8')
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
    outDir,
    emptyOutDir: false,
    cssCodeSplit: false,
    lib: {
      entry: resolve(__dirname, 'src/main.ts'),
      name: 'IronBazaar',
      formats: ['iife'],
      fileName: () => 'iron-bazaar.js'
    },
    rollupOptions: {
      output: {
        inlineDynamicImports: true,
        assetFileNames: asset => asset.name?.endsWith('.css')
          ? 'iron-bazaar.css'
          : '[name][extname]'
      }
    }
  }
})
