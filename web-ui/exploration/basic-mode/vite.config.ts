import { fileURLToPath } from 'node:url';
import react from '@vitejs/plugin-react';
import tailwindcss from '@tailwindcss/vite';
import { defineConfig } from 'vite';
import { viteSingleFile } from 'vite-plugin-singlefile';

export default defineConfig({
  root: fileURLToPath(new URL('./', import.meta.url)),
  base: './',
  plugins: [react(), tailwindcss(), viteSingleFile()],
  server: { host: '127.0.0.1', port: 5184, strictPort: true },
});
