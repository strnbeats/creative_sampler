# Basic Mode Visual Specification

**Status:** approved visual direction, handed off from UI-2.4B exploration

**Reference implementation:** [`web-ui/exploration/basic-mode/`](../web-ui/exploration/basic-mode/)

**Related product plan:** [`BASIC_MODE_DESIGN.md`](BASIC_MODE_DESIGN.md)

This document freezes the Basic Mode visual direction established by the UI-2.4B exploration and refined in UI-2.4B.1 and UI-2.4B.2. It records what exists in the prototype and defines what production work must preserve. The prototype is a visual and interaction study: it is not an FX engine, slicer, or audio player.

## Approved visual concept

Basic Mode is a performance instrument for turning an imported sound into a playable kit and shaping it through six preset-specific macros. It should feel immediate and expressive, without exposing manual slice editing or technical engine controls. Advanced Mode remains the place for detailed waveform and slice editing.

The common visual foundation is Lithic: a warm mineral-light surface, precise technical typography and markings, copper as the persistent signal and interaction accent, and restrained instrument-like construction. Presets tint and alter the material character while remaining inside that family.

The six macros are the main visual event. Each one combines three inseparable layers: a recognizable rotary interaction, an individual effect-inspired material, and visible material response to its value. Their silhouettes are irregular and organic, with relief and selected fragments extending beyond the main body. They must not be reduced during production migration to ordinary round or square knobs, generic CSS progress rings, or standard JUCE controls. Decorative geometry must stay outside the stable interaction region and must never obscure the value, ticks, or copper position marker.

## Composition and hierarchy

1. **Top rail:** Creative Sampler identity, Basic/Advanced workspace switch, and sample import.
2. **Source bar:** sample identity and concise source metadata. The filename remains subordinate to the controls and instrument composition.
3. **FX stage:** preset identity, category, short sound description, preset navigation/browser, demo chain, and six macros on one shared Lithic surface. The preset establishes the material palette and each macro's role.
4. **Playable kit strip:** a compact source/slice waveform preview above eight named performance pads. The strip communicates a prepared kit rather than a manual waveform editor.
5. **Footer:** prototype status and current demonstration limitations.

The preset browser is a focused drawer rather than a dashboard. It groups the collection by category, supports text search and category filtering, shows material thumbnails, and marks the loaded preset. Its miniatures reuse the actual material silhouettes and signal marks.

## Macro interaction and visual invariants

The SVG renderer uses a shared 168 × 168 viewBox for all material variants. A fixed square `macro-hit` region carries the accessible slider interaction while the graphic itself has `pointer-events: none`. The illustrated body can be asymmetrical and its relief can project outside the body without changing hit geometry. This separation is a production requirement: material edges must not shift or enlarge the hit target as values change.

The persistent reading system is shared across archetypes:

- 17 graduated ticks around a 256-degree sweep, with the active portion copper-lit;
- a small copper position marker with a light edge;
- a numeric percentage readout and macro name;
- a short pair of example target labels below the control.

The body has a layered lower skirt, raised face, clipped texture, edge highlight, soft contact shadow and material-specific outer relief. Preset tint unifies a bank without erasing the identity of individual materials. Material geometry, opacity, scale, fragments, waves or light respond to the normalized value. The response is event-driven SVG rendering, not a continuously animated background.

### Twelve implemented material archetypes

| ID | Visual concept and geometry | Texture and value response | Light, relief and useful roles |
|---|---|---|---|
| `air` | Translucent folded membranes with long, irregular wisps. | Several semi-transparent films separate and spread as value rises; their pale edge lines become clearer. | Diffuse pearly light and layered projections beyond the body. Air, diffusion, transparency, width, bloom. |
| `heat` | Molten copper mass crossed by bent, flowing bands. | Ribbon amplitude and thickness, ember visibility and glowing particles increase with value. | Dark contact skirt, bright hot edges and a few raised molten droplets. Saturation, drive, warmth, distortion. |
| `crush` | Stepped, block-like body with a broken digital profile. | A deterministic field of rectangular fragments shifts, separates and changes prominence with value. | Hard pixel fragments project beyond the body; horizontal scan breaks gain contrast. Bit reduction, sample-rate reduction, stutter, digital damage. |
| `grain` | Soft-edged particulate mass. | A fixed seeded dust field grows denser and larger with value, remaining stable between renders. | Dust escapes the silhouette; warm flecks and fine diagonal scoring imply film or granular matter. Grain, noise, texture, density. |
| `motion` | Flowing irregular body built from layered wave ribbons. | Ribbon amplitude and highlight thickness expand with value; the outer flowing trace also swells. | Cool fluid highlights and a wave crest that extends outside the body. Modulation, rate, movement, stereo spread. |
| `space` | Slanted, layered depth field rather than a radial ring. | Elliptical depth layers expand and separate; distant points become more visible. | Soft violet-pearl light, nested contour planes and sparse stars. Reverb, delay depth, diffusion, atmosphere. |
| `chaos` | Angular fractured plate with independent facets. | Facets separate and their edges become stronger as the value rises. | Offset shards extend outside the body; restrained rose/copper fracture highlights. Randomness, scatter, probability, unstable repeats. |
| `prism` | Faceted translucent crystal. | Spectral planes brighten and color bands thicken with value. | Pale refracted highlights and facet edges extend beyond the central geometry. Excitation, spectral shaping, shimmer, resonant color. |
| `echo` | Repeated offset forms inside a soft irregular body. | Echo outlines become more visible and spread farther apart as value rises. | Thin warm contour strokes and a small lower echo arc. Delay time, feedback, repeats, pre-delay. |
| `chrome` | Reflective fluid metal, with a non-circular droplet-like outline. | A liquid chrome band changes angle and highlight width; its bright reflection grows with value. | High contrast metallic glints, dark reflected troughs, and raised droplets. Resonance, comb reflection, metallic tone, pitch bend. |
| `energy` | Faceted electromagnetic core crossed by sharp traces. | Six pulse traces grow in amplitude and thickness as the value rises. | Indigo field, cool luminous cores and branching traces beyond the body. Gate, pulse, transient emphasis, clocked modulation. |
| `film` | Angled analog-film body with repeated frame windows. | Warm frame content and waveform marks become more prominent and thicker with value. | Perforation-like details and worn highlights protrude from the body. Tape, wow/flutter, saturation, dropout, lo-fi wear. |

These are the 12 implemented archetypes. New archetypes require a concrete product need and review; preset variety should first be composed from the existing system.

## Demonstration preset library

The exploration defines ten presets. Each macro entry below gives **name / material / initial value / example target pair**. The chain and target names are illustrative only; they do not represent instantiated DSP nodes or live parameter bindings.

| Preset | Category and sonic concept | Demo chain | Six macro definitions | Tint / visual character |
|---|---|---|---|---|
| **Pink Meltdown** | Hyperpop. Soft edges fracture into saturated color and repeats. | Saturate → Fracture → Diffuse | Heat / `heat` / 58 / Drive · Tone; Crush / `crush` / 27 / Bit depth · Sample rate; Grain / `grain` / 42 / Density · Size; Motion / `motion` / 64 / Rate · Depth; Space / `space` / 36 / Decay · Mix; Chaos / `chaos` / 18 / Scatter · Feedback. | `#b35142`; ember, pixel, dust, liquid, depth and fractured facets. |
| **Glass Vocal** | Vocal Processing. Bright doubles and glassy echoes leave room for a lead. | Excite → Double → Echo | Air / `air` / 64 / Shelf · Drive; Shine / `prism` / 38 / Excite · Tone; Width / `motion` / 52 / Spread · Detune; Delay / `echo` / 24 / Time · Feedback; Texture / `grain` / 17 / Grain · Mix; Bloom / `space` / 45 / Decay · Diffusion. | `#668f8d`; translucent films, refraction, fluid width, echo, grain and bloom. |
| **Dreamsick** | Ambient / Dreamy. Slow pitch drift dissolves into a soft shimmering tail. | Detune → Blur → Shimmer | Drift / `motion` / 40 / Rate · Detune; Blur / `air` / 62 / Diffusion · Time; Shimmer / `prism` / 29 / Pitch send · Feedback; Depth / `echo` / 54 / Mod mix · Spread; Motion / `energy` / 23 / Phase · Rate; Atmosphere / `space` / 72 / Tail · Mix. | `#77698f`; slow fluid forms, haze, shimmer and deep lavender light. |
| **Broken CD** | Glitch / Digital. Skips, smears and scatters fragments into a new rhythm. | Repeat → Downsample → Scatter | Skip / `energy` / 31 / Retrigger · Chance; Chip / `crush` / 57 / Bit depth · Tone; Stutter / `echo` / 43 / Subdivision · Feedback; Swing / `chrome` / 22 / Offset · Rate; Smear / `space` / 33 / Diffusion · Mix; Scatter / `chaos` / 68 / Spread · Jitter. | `#596984`; cool digital blocks against metallic offsets and separated shards. |
| **Tape Memories** | Vintage / Lo-Fi. Warm saturation, gentle wow and darkening echoes. | Tape → Wobble → Echo | Bias / `film` / 51 / Drive · Tone; Dust / `grain` / 20 / Noise · High cut; Wow / `motion` / 38 / Rate · Drift; Flutter / `heat` / 14 / Speed · Depth; Echo / `echo` / 44 / Time · Feedback; Wear / `crush` / 30 / Dropout · Mix. | `#a4673c`; warm film frames, dust, gentle movement, copper and worn blocks. |
| **Liquid Motion** | Rhythmic / Movement. Pulsing filters and stereo movement pull phrases into orbit. | Filter → Phase → Pan | Shape / `prism` / 36 / Cutoff · Resonance; Pulse / `energy` / 58 / Rate · Depth; Phase / `echo` / 46 / Stages · Feedback; Flow / `motion` / 63 / Rate · Offset; Width / `chrome` / 33 / Spread · Mix; Focus / `air` / 47 / Balance · Tone. | `#42877e`; teal liquid, pulse traces, metallic flow and translucent focus. |
| **Chrome Tears** | Hyperpop. Silver pitch bends and comb reflections turn a vocal into liquid metal. | Bend → Comb → Plate | Chrome / `chrome` / 74 / Comb mix · Resonance; Bend / `motion` / 32 / Pitch range · Glide; Gleam / `prism` / 56 / High shelf · Excite; Tears / `air` / 46 / Spread · Delay; Rattle / `grain` / 23 / Flutter · Damping; Mirror / `echo` / 61 / Plate mix · Pre-delay. | `#7596a2`; liquid silver, pale spectral refraction and translucent highlights. |
| **Infinite Space** | Ambient / Dreamy. Long dark reverb opens into distant frozen reflections. | Diffuse → Freeze → Shimmer | Horizon / `space` / 78 / Size · Decay; Mist / `air` / 54 / Diffusion · High cut; Freeze / `prism` / 37 / Hold · Feedback; Distance / `echo` / 62 / Pre-delay · Early mix; Gravity / `chrome` / 29 / Pitch shift · Low shelf; Stars / `grain` / 48 / Density · Shimmer. | `#6a6a98`; layered violet depth, haze, sparse highlights and suspended grain. |
| **Digital Heartbeat** | Rhythmic / Movement. A gated signal snaps into a pulse with off-beat repeats. | Gate → Clock → Repeat | Heart / `energy` / 71 / Gate depth · Attack; Clock / `crush` / 56 / Subdivision · Swing; Snap / `heat` / 44 / Transient · Drive; Steps / `echo` / 27 / Repeat rate · Feedback; Glitch / `chaos` / 19 / Probability · Jitter; Push / `motion` / 63 / Mod depth · Pan. | `#a04a56`; strong pulse light, stepped clock, ember snap and rhythmic fractures. |
| **Angel.exe** | Experimental. A pristine octave double breaks into unstable halos and artifacts. | Octave → Excite → Corrupt | Halo / `air` / 72 / Octave mix · Spread; Grace / `prism` / 52 / Shelf · Excite; Error / `crush` / 37 / Bit depth · Dropout; Flicker / `energy` / 18 / Gate rate · Chance; Stain / `film` / 28 / Tape drive · Damping; Ascend / `space` / 63 / Shimmer · Tail. | `#9a6d92`; pale membranes and crystal light disrupted by pixels, pulse and film. |

The browser currently groups the ten entries into Hyperpop, Vocal Processing, Ambient / Dreamy, Glitch / Digital, Vintage / Lo-Fi, Rhythmic / Movement and Experimental. A preset owns its tint, macro labels, material assignments, demo starting values and two illustrative targets per macro.

## Interaction captured in the exploration

- **Macro drag:** pointer capture; vertical motion changes the value. Shift modifies the drag sensitivity for fine movement. Drag updates only the local demo value.
- **Keyboard:** slider role and accessible name; arrows adjust by 1%, Shift+arrows by 5%, Page Up/Down by 10%, Home/End set endpoints, Enter restores that macro's starting value.
- **Reset:** double-click restores the selected preset's initial value.
- **Preset change:** previous/next buttons or browser selection change all six names, materials, examples and initial values. Edited values are retained per preset in page memory while switching between presets. This is not saved to disk or a plugin state.
- **Preset browser:** search matches name and category; category filters narrow the grouped list; material swatches preview the first three macro archetypes; the current preset is marked Loaded.
- **Pads:** mouse click or keys 1–8 highlight one of eight pads and its waveform segment for one second. This is a visual trigger simulation, with no sound or MIDI output.
- **Basic / Advanced:** changes between two exploration views while retaining the in-memory preset and macro state. Advanced is a preview, not the production editor.
- **Empty and import:** Empty offers a demo kit and an audio file chooser. Import demonstrates a brief preparation state and displays the chosen filename; waveform, analysis and slice layout remain fixtures. No audio is decoded or automatically sliced.

## Responsive behavior

The desktop composition presents all six controls in one row. At widths up to 1020 px, top spacing and the macro bank tighten. At 900 × 686, the interface remains in its medium layout. At widths up to 730 px, the macros reflow to three columns by two rows, with smaller but still fixed square hit regions; the source header and preset navigation condense. Below 500 px, preset information and navigation stack and the pads become a four-column grid. The document body has a 320 px minimum width.

Organic relief may extend within the SVG's visible overflow, but the document must not clip or overlap neighboring controls. Production validation must check actual JUCE WebView sizing and device scale; the exploration's browser behavior is not a guarantee for WebView.

## Production handoff requirements

### Required visual acceptance

- Retain the Lithic warm-light material system, copper scale and markers, typographic hierarchy and purposeful open space.
- Keep the six macros visually central and arranged as a coherent bank.
- Preserve each macro's irregular silhouette, multi-layer depth, projection/relief, texture and value-responsive material. Do not substitute standard circular/square knobs, stock JUCE controls, or a generic arc ring.
- Keep a stable and obvious interactive hit region independent of relief; preserve the scale, copper marker, numeric value and accessible label.
- Map materials by macro role in the selected preset, rather than by permanent bank index.
- Preserve meaningful visual contrast and preset-to-preset material diversity. The ten exploration presets are the approved starting library and visual test set.
- Keep the focused, categorized browser, search, miniatures and clear selected state.
- Keep the compact kit/pad area subordinate to the macros and distinct from a manual slice editor.
- Match the desktop and medium composition and deliberately reflow at compact sizes without unreadable controls or clipped ornaments.

### Required functional acceptance

- Bind macro values to real FX Engine state with a documented mapping, units, bounds, curves, and authoritative value source.
- Support reliable two-way synchronization: UI gestures update C++ state and accepted/externally changed engine values update the UI without echo loops or stale acknowledgements.
- Load and apply real preset definitions, including effect chain, order, parameter values and macro mapping, with a defined strategy for replacing or transitioning the active configuration.
- Persist preset and parameter configuration using the product's state/preset mechanism, not exploration-local React state.
- Keep Basic Mode macro mappings and Advanced Mode direct controls coherent when switching modes. Define conflict resolution when Advanced edits a parameter controlled by a Basic macro.
- Give pads a real supported playback/trigger path before representing them as live performance controls. Use actual imported sample, slicing and analysis results; remove all demo fixtures from production state.
- Keep failures, unavailable effects and unsupported preset versions explicit and recoverable.

### Technical and performance constraints

- C++ remains authoritative for DSP and audio state. The UI bridge stays typed, validated, versioned and outside the audio callback.
- No file I/O, JSON parsing, allocation-heavy preset construction, blocking lock, WebView call or wait in the audio callback. Build or swap effect state in a suitable worker/message-thread handoff and publish safely to audio processing.
- Avoid per-frame canvas/SVG animation and repeated expensive geometry generation. Keep material rendering event-driven or otherwise bounded; preserve the fixed seeded detail behavior where deterministic output matters.
- Test frequent pointer updates, preset changes and program/state restoration for responsiveness and stale state. Bound update rates only with a policy that does not make knob movement feel detached.
- Validate bundled SVG/CSS/React behavior, fonts, color rendering, scaling, reduced motion, pointer capture and keyboard accessibility in JUCE WebView on macOS and Windows.
- Measure CPU, memory, frame time and drag smoothness in the actual Standalone/WebView at desktop, medium and minimum supported sizes. Headless/browser measurements are indicative only and do not satisfy native acceptance.

## Reference screenshots

The following captures are from the actual built exploration in a browser and are visual references, not native plugin screenshots.

| Reference | Screenshot |
|---|---|
| Pink Meltdown | [1440 × 900](../web-ui/exploration/basic-mode/screenshots/organic-depth/pink-meltdown.png) |
| Glass Vocal | [1440 × 900](../web-ui/exploration/basic-mode/screenshots/organic-depth/glass-vocal.png) |
| Broken CD | [1440 × 900](../web-ui/exploration/basic-mode/screenshots/organic-depth/broken-cd.png) |
| Dreamsick | [1440 × 900](../web-ui/exploration/basic-mode/screenshots/organic-depth/dreamsick.png) |
| Tape Memories | [1440 × 900](../web-ui/exploration/basic-mode/screenshots/organic-depth/tape-memories.png) |
| Chrome Tears | [1440 × 900](../web-ui/exploration/basic-mode/screenshots/organic-depth/chrome-tears.png) |
| Infinite Space | [1440 × 900](../web-ui/exploration/basic-mode/screenshots/organic-depth/infinite-space.png) |
| Digital Heartbeat | [1440 × 900](../web-ui/exploration/basic-mode/screenshots/organic-depth/digital-heartbeat.png) · [620 × 663](../web-ui/exploration/basic-mode/screenshots/organic-depth/digital-heartbeat-compact.png) |
| Liquid Motion | [1440 × 900](../web-ui/exploration/basic-mode/screenshots/organic-depth/liquid-motion.png) |
| Angel.exe | [1440 × 900](../web-ui/exploration/basic-mode/screenshots/organic-depth/angel-exe.png) |
| Preset browser | [1440 × 900](../web-ui/exploration/basic-mode/screenshots/organic-depth/preset-browser.png) |
| Low / high values | [10% bank](../web-ui/exploration/basic-mode/screenshots/organic-depth/pink-meltdown-low.png) · [90% bank](../web-ui/exploration/basic-mode/screenshots/organic-depth/pink-meltdown-high.png) |
| Hover / drag | [Hover](../web-ui/exploration/basic-mode/screenshots/organic-depth/heat-hover.png) · [During drag](../web-ui/exploration/basic-mode/screenshots/organic-depth/heat-drag.png) |
| Material detail | [Air](../web-ui/exploration/basic-mode/screenshots/organic-depth/air-detail.png) · [Chrome](../web-ui/exploration/basic-mode/screenshots/organic-depth/chrome-detail.png) · [Energy](../web-ui/exploration/basic-mode/screenshots/organic-depth/heart-detail.png) |

Earlier reference states, including Empty, original loaded layout, medium and compact screenshots, remain catalogued in the exploration README. That README also documents the run and check commands.

## Known prototype / specification boundary

- Preset chains and the two target labels per macro are descriptive fixtures, not actual effect instances or mappings.
- All macro values are local percentage values for visual feedback, not engine parameters.
- File selection uses the filename only; the displayed signal, BPM, key and eight slice definitions are demo data.
- Pad activity is a timed visual highlight only. It does not generate audio or MIDI.
- Advanced Mode is a limited visual preview, not the production Advanced UI.
- The approved exploration has browser screenshots for all ten presets at 1440 × 900. The 620 × 663 capture represents Digital Heartbeat; it is not a separate compact proof for every preset.
- No claim is made here about native WebView performance or production DSP readiness.

## Related material

- [`BASIC_MODE_DESIGN.md`](BASIC_MODE_DESIGN.md) — product and architecture plan from UI-2.4A.
- [`web-ui/exploration/basic-mode/README.md`](../web-ui/exploration/basic-mode/README.md) — local launch, states, screenshots, prototype interaction and build checks.
