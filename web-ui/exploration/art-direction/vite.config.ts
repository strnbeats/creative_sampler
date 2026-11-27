import { fileURLToPath } from 'node:url';
import react from '@vitejs/plugin-react';
import { defineConfig } from 'vite';
import { viteSingleFile } from 'vite-plugin-singlefile';

export default defineConfig({
  root: fileURLToPath(new URL('./', import.meta.url)),
  base: './',
  plugins: [react(), viteSingleFile()],
  server: {
    host: '127.0.0.1',
    port: 5182,
    strictPort: true,
    fs: { allow: [fileURLToPath(new URL('../../', import.meta.url))] },
  },
});
