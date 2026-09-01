import { defineConfig } from 'vite'
import preact from '@preact/preset-vite'
import { viteSingleFile } from 'vite-plugin-singlefile'

export default defineConfig({
  plugins: [preact(), viteSingleFile()],
  build: {
    target: ['es2015', 'safari12'],
    cssCodeSplit: false,
    assetsInlineLimit: 100000,
    minify: 'esbuild',
  },
  server: {
    port: 5173,
    proxy: {
      '/api': {
        target: 'http://127.0.0.1:8081',
        changeOrigin: true,
      },
    },
  },
})
