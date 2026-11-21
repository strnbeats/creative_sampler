import { readFile, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { defineConfig, type Plugin } from 'vite';
import react from '@vitejs/plugin-react';
import tailwindcss from '@tailwindcss/vite';
import { viteSingleFile } from 'vite-plugin-singlefile';

function classicInlineScript(): Plugin {
  return {
    name: 'creative-sampler-classic-inline-script',
    async closeBundle() {
      const output = resolve('dist/index.html');
      const html = await readFile(output, 'utf8');
      await writeFile(output, html.replace('<script type="module" crossorigin>', '<script defer>'));
    },
  };
}

export default defineConfig({
  plugins: [react(), tailwindcss(), viteSingleFile(), classicInlineScript()],
  base: './',
  build: { outDir: 'dist', emptyOutDir: true, assetsInlineLimit: 10000000 },
});
