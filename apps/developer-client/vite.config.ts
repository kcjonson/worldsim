import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import { viteSingleFile } from 'vite-plugin-singlefile';

export default defineConfig({
  plugins: [
    react(),
    viteSingleFile()  // Inline all JS/CSS into a single HTML file (works with file://)
  ],
  build: {
    outDir: 'dist',
    minify: 'esbuild',
    sourcemap: 'inline',  // Inline source maps for debugging (works with single-file build)
    cssCodeSplit: false,  // Required for vite-plugin-singlefile
    assetsInlineLimit: 100000000  // Inline everything
  }
});
