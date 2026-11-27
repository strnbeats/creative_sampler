# Creative Sampler · Basic Mode visual exploration

The approved Basic Mode visual direction and production handoff criteria are recorded in [`docs/BASIC_MODE_VISUAL_SPEC.md`](../../../docs/BASIC_MODE_VISUAL_SPEC.md). This specification freezes the Organic Depth art direction; this exploration remains demonstration-only.

## Run locally

From the repository root:

```sh
cd web-ui
./node_modules/.bin/vite --config exploration/basic-mode/vite.config.ts
```

Open [Basic Mode](http://127.0.0.1:5184/). Open each reference state in a new tab or reload after changing the fragment. The first table preserves the original UI-2.4B captures. The preset gallery farther down shows the current UI-2.4B.2 exploration.

| State | Reference URL | Screenshot |
| --- | --- | --- |
| Sample loaded | [#loaded](http://127.0.0.1:5184/#loaded) | [1440×900](screenshots/loaded-1440x900.png), [900×686](screenshots/loaded-900x686.png), [620×663](screenshots/loaded-620x663.png) |
| Preset browsing | [#browser](http://127.0.0.1:5184/#browser) | [1440×900](screenshots/browser-1440x900.png) |
| Performance | [#performance](http://127.0.0.1:5184/#performance) | [1440×900](screenshots/performance-1440x900.png) |
| Empty | [#empty](http://127.0.0.1:5184/#empty) | [1440×900](screenshots/empty-1440x900.png) |
| Advanced preview | [#advanced](http://127.0.0.1:5184/#advanced) | — |

## Interactions

- Drag a macro up or down. Hold Shift for finer movement. Double-click or press Enter on a focused macro to restore its preset value. Arrow keys adjust by 1%, Shift with an arrow adjusts by 5%, Page Up and Page Down adjust by 10%, and Home and End set the endpoints.
- Browse, search or filter presets in the drawer. The arrow buttons cycle through the collection directly. Macro labels and target pairs change with the preset. Edits are kept per preset for the lifetime of the page.
- Click a pad or press 1–8 to highlight a segment and its corresponding pad for one second. The performance reference starts with a held demonstration highlight.
- Switch workspaces to inspect the same kit in the Advanced preview. The Basic macro values and preset are retained.
- Import uses the filename to demonstrate the preparation flow. The waveform, analysis for Garden Glass and eight slice definitions are fixtures. Selecting a different file keeps the fixed waveform and kit and displays a demo label in place of the Garden Glass analysis. The Advanced preview shows a larger waveform and the same kit.

## Design

The macro panel places all six controls on one light Lithic surface. A copper marker, graduated ticks and a fixed input area give the materials a shared interface. Each material has its own body, raised edges, lighting and outer relief. Air forms membranes, Chrome forms droplets and Crush sheds pixel blocks. The preset definition assigns the material and a palette tint. Geometry, lighting and opacity respond to the value. The renderer runs on input changes without a background animation loop.

Pink Meltdown combines ember bands (Heat), shifted pixel blocks (Crush), lit dust (Grain), liquid ribbons (Motion), receding rings (Space) and separated facets (Chaos). Glass Vocal uses expanding translucent films (Air), split light (Shine) and spreading repeats (Delay). It reuses the ribbons, dust and rings for Width, Texture and Bloom.

The smaller, dark copper kit strip groups a waveform thumbprint and playable pads. At 620 px the macro bank becomes 3×2 and the pad notes remain available in accessible names.

### UI-2.4B.2 preset gallery

Each preset has six starting values, a demo signal chain and two example parameter assignments per macro. The browser groups presets by category and shows thumbnails of their first three macro materials.

| Preset | Open in Basic Mode | 1440×900 |
| --- | --- | --- |
| Pink Meltdown | [Open](http://127.0.0.1:5184/#preset=meltdown) | [PNG](screenshots/organic-depth/pink-meltdown.png) |
| Glass Vocal | [Open](http://127.0.0.1:5184/#preset=glass) | [PNG](screenshots/organic-depth/glass-vocal.png) |
| Broken CD | [Open](http://127.0.0.1:5184/#preset=broken) | [PNG](screenshots/organic-depth/broken-cd.png) |
| Dreamsick | [Open](http://127.0.0.1:5184/#preset=dreamsick) | [PNG](screenshots/organic-depth/dreamsick.png) |
| Tape Memories | [Open](http://127.0.0.1:5184/#preset=tape) | [PNG](screenshots/organic-depth/tape-memories.png) |
| Chrome Tears | [Open](http://127.0.0.1:5184/#preset=chrome-tears) | [PNG](screenshots/organic-depth/chrome-tears.png) |
| Infinite Space | [Open](http://127.0.0.1:5184/#preset=infinite-space) | [PNG](screenshots/organic-depth/infinite-space.png) |
| Digital Heartbeat | [Open](http://127.0.0.1:5184/#preset=digital-heartbeat) | [PNG](screenshots/organic-depth/digital-heartbeat.png) |
| Liquid Motion | [Open](http://127.0.0.1:5184/#preset=orbit) | [PNG](screenshots/organic-depth/liquid-motion.png) |
| Angel.exe | [Open](http://127.0.0.1:5184/#preset=angel-exe) | [PNG](screenshots/organic-depth/angel-exe.png) |

[Preset browser](screenshots/organic-depth/preset-browser.png) · [620×663](screenshots/organic-depth/digital-heartbeat-compact.png) · [All six macros at 10%](screenshots/organic-depth/pink-meltdown-low.png) · [All six at 90%](screenshots/organic-depth/pink-meltdown-high.png) · [Hover](screenshots/organic-depth/heat-hover.png) · [Drag](screenshots/organic-depth/heat-drag.png)

Close-ups at 2× pixel density: [Air](screenshots/organic-depth/air-detail.png) · [Chrome](screenshots/organic-depth/chrome-detail.png) · [Heart](screenshots/organic-depth/heart-detail.png)

### Archived UI-2.4B.1 reference captures

| Preset | Default | All macros at 10% | All macros at 90% | Compact |
| --- | --- | --- | --- | --- |
| Pink Meltdown | [1440×900](screenshots/effect-materials/pink-meltdown-desktop.png) | [Low](screenshots/effect-materials/pink-meltdown-low.png) | [High](screenshots/effect-materials/pink-meltdown-high.png) | [620×663](screenshots/effect-materials/pink-meltdown-compact.png) |
| Glass Vocal | [1440×900](screenshots/effect-materials/glass-vocal-desktop.png) | [Low](screenshots/effect-materials/glass-vocal-low.png) | [High](screenshots/effect-materials/glass-vocal-high.png) | [620×663](screenshots/effect-materials/glass-vocal-compact.png) |

[Heat hovered](screenshots/effect-materials/heat-hover.png) · [Heat with the pointer held during a drag](screenshots/effect-materials/heat-drag.png)

## Check and build

From `web-ui`:

```sh
./node_modules/.bin/tsc -p exploration/basic-mode/tsconfig.json
./node_modules/.bin/vite build --config exploration/basic-mode/vite.config.ts
```

The build writes the self-contained HTML to `exploration/basic-mode/dist/index.html`. The screenshots were captured from the built page in Firefox at 1×. WebView layout, native import and audio behavior are outside this exploration.
