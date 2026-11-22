import { fileURLToPath } from 'node:url';
import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import { viteSingleFile } from 'vite-plugin-singlefile';

const root = fileURLToPath(new URL('./app', import.meta.url));

export default defineConfig({
  root,
  plugins: [react(), viteSingleFile()],
  base: './',
  server: { host: '127.0.0.1', port: 5180, strictPort: true },
  build: {
    outDir: fileURLToPath(new URL('../dist-v2', import.meta.url)),
    emptyOutDir: true,
    assetsInlineLimit: 10_000_000,
  },
});
