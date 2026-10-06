import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import { resolve } from 'node:path'
import { readFileSync } from 'node:fs'

export default defineConfig({
  plugins: [
    vue({ template: { compilerOptions: { hoistStatic: false } } }),
    {
      name: 'emit-base-control-localization',
      generateBundle() {
        this.emitFile({
          type: 'asset',
          fileName: 'base-control.localization.json',
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
    outDir: resolve(__dirname, '../../../../../Content/GameCore/H5UI/BaseControl'),
    emptyOutDir: false,
    cssCodeSplit: false,
    lib: {
      entry: resolve(__dirname, 'src/main.ts'),
      name: 'SilverChoirBaseControl',
      formats: ['iife'],
      fileName: () => 'base-control.js'
    },
    rollupOptions: {
      output: {
        inlineDynamicImports: true,
        assetFileNames: asset => asset.name?.endsWith('.css')
          ? 'base-control.css'
          : '[name][extname]'
      }
    }
  }
})
