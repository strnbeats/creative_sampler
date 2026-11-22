import { fileURLToPath } from 'node:url';
import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import { viteSingleFile } from 'vite-plugin-singlefile';

const root = fileURLToPath(new URL('./showcase', import.meta.url));

export default defineConfig({
  root,
  plugins: [react(), viteSingleFile()],
  base: './',
  server: { host: '127.0.0.1', port: 5179, strictPort: true },
  build: {
    outDir: '/private/tmp/creative-sampler-ui20b-showcase-dist',
    emptyOutDir: true,
    assetsInlineLimit: 10_000_000,
  },
});
