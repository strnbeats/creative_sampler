export type MacroMaterialId = 'heat' | 'crush' | 'grain' | 'motion' | 'space' | 'chaos' | 'air' | 'prism' | 'echo' | 'chrome' | 'energy' | 'film';

type Macro = {
  name: string;
  material: MacroMaterialId;
  initial: number;
  mapping: readonly [string, string];
};

type Preset = {
  id: string;
  name: string;
  category: string;
  description: string;
  tint: string;
  chain: readonly string[];
  macros: readonly [Macro, Macro, Macro, Macro, Macro, Macro];
};

const macro = (name: string, material: MacroMaterialId, initial: number, first: string, second: string): Macro =>
  ({ name, material, initial, mapping: [first, second] });

export const presets: readonly Preset[] = [
  {
    id: 'meltdown', name: 'Pink Meltdown', category: 'Hyperpop',
    tint: '#b35142',
    description: 'Soft edges give way to blown-out colour and fractured repeats.',
    chain: ['Saturate', 'Fracture', 'Diffuse'],
    macros: [
      macro('Heat', 'heat', 58, 'Drive', 'Tone'), macro('Crush', 'crush', 27, 'Bit depth', 'Sample rate'),
      macro('Grain', 'grain', 42, 'Density', 'Size'), macro('Motion', 'motion', 64, 'Rate', 'Depth'),
      macro('Space', 'space', 36, 'Decay', 'Mix'), macro('Chaos', 'chaos', 18, 'Scatter', 'Feedback'),
    ],
  },
  {
    id: 'glass', name: 'Glass Vocal', category: 'Vocal Processing',
    tint: '#668f8d',
    description: 'Bright doubles and glassy echoes that leave room for the lead.',
    chain: ['Excite', 'Double', 'Echo'],
    macros: [
      macro('Air', 'air', 64, 'Shelf', 'Drive'), macro('Shine', 'prism', 38, 'Excite', 'Tone'),
      macro('Width', 'motion', 52, 'Spread', 'Detune'), macro('Delay', 'echo', 24, 'Time', 'Feedback'),
      macro('Texture', 'grain', 17, 'Grain', 'Mix'), macro('Bloom', 'space', 45, 'Decay', 'Diffusion'),
    ],
  },
  {
    id: 'dreamsick', name: 'Dreamsick', category: 'Ambient / Dreamy',
    tint: '#77698f',
    description: 'Slow pitch drift dissolves into a soft, shimmering tail.',
    chain: ['Detune', 'Blur', 'Shimmer'],
    macros: [
      macro('Drift', 'motion', 40, 'Rate', 'Detune'), macro('Blur', 'air', 62, 'Diffusion', 'Time'),
      macro('Shimmer', 'prism', 29, 'Pitch send', 'Feedback'), macro('Depth', 'echo', 54, 'Mod mix', 'Spread'),
      macro('Motion', 'energy', 23, 'Phase', 'Rate'), macro('Atmosphere', 'space', 72, 'Tail', 'Mix'),
    ],
  },
  {
    id: 'broken', name: 'Broken CD', category: 'Glitch / Digital',
    tint: '#596984',
    description: 'Skip, smear and scatter the smallest fragments into a new rhythm.',
    chain: ['Repeat', 'Downsample', 'Scatter'],
    macros: [
      macro('Skip', 'energy', 31, 'Retrigger', 'Chance'), macro('Chip', 'crush', 57, 'Bit depth', 'Tone'),
      macro('Stutter', 'echo', 43, 'Subdivision', 'Feedback'), macro('Swing', 'chrome', 22, 'Offset', 'Rate'),
      macro('Smear', 'space', 33, 'Diffusion', 'Mix'), macro('Scatter', 'chaos', 68, 'Spread', 'Jitter'),
    ],
  },
  {
    id: 'tape', name: 'Tape Memories', category: 'Vintage / Lo-Fi',
    tint: '#a4673c',
    description: 'Warm saturation, gentle wow and echoes that darken on each repeat.',
    chain: ['Tape', 'Wobble', 'Echo'],
    macros: [
      macro('Bias', 'film', 51, 'Drive', 'Tone'), macro('Dust', 'grain', 20, 'Noise', 'High cut'),
      macro('Wow', 'motion', 38, 'Rate', 'Drift'), macro('Flutter', 'heat', 14, 'Speed', 'Depth'),
      macro('Echo', 'echo', 44, 'Time', 'Feedback'), macro('Wear', 'crush', 30, 'Dropout', 'Mix'),
    ],
  },
  {
    id: 'orbit', name: 'Liquid Motion', category: 'Rhythmic / Movement',
    tint: '#42877e',
    description: 'Pulsing filters and stereo movement pull small phrases into orbit.',
    chain: ['Filter', 'Phase', 'Pan'],
    macros: [
      macro('Shape', 'prism', 36, 'Cutoff', 'Resonance'), macro('Pulse', 'energy', 58, 'Rate', 'Depth'),
      macro('Phase', 'echo', 46, 'Stages', 'Feedback'), macro('Flow', 'motion', 63, 'Rate', 'Offset'),
      macro('Width', 'chrome', 33, 'Spread', 'Mix'), macro('Focus', 'air', 47, 'Balance', 'Tone'),
    ],
  },
  {
    id: 'chrome-tears', name: 'Chrome Tears', category: 'Hyperpop',
    tint: '#7596a2',
    description: 'Silver pitch bends and bright comb reflections turn a vocal into liquid metal.',
    chain: ['Bend', 'Comb', 'Plate'],
    macros: [
      macro('Chrome', 'chrome', 74, 'Comb mix', 'Resonance'), macro('Bend', 'motion', 32, 'Pitch range', 'Glide'),
      macro('Gleam', 'prism', 56, 'High shelf', 'Excite'), macro('Tears', 'air', 46, 'Spread', 'Delay'),
      macro('Rattle', 'grain', 23, 'Flutter', 'Damping'), macro('Mirror', 'echo', 61, 'Plate mix', 'Pre-delay'),
    ],
  },
  {
    id: 'infinite-space', name: 'Infinite Space', category: 'Ambient / Dreamy',
    tint: '#6a6a98',
    description: 'A long, dark reverb opens into a frozen field of distant reflections.',
    chain: ['Diffuse', 'Freeze', 'Shimmer'],
    macros: [
      macro('Horizon', 'space', 78, 'Size', 'Decay'), macro('Mist', 'air', 54, 'Diffusion', 'High cut'),
      macro('Freeze', 'prism', 37, 'Hold', 'Feedback'), macro('Distance', 'echo', 62, 'Pre-delay', 'Early mix'),
      macro('Gravity', 'chrome', 29, 'Pitch shift', 'Low shelf'), macro('Stars', 'grain', 48, 'Density', 'Shimmer'),
    ],
  },
  {
    id: 'digital-heartbeat', name: 'Digital Heartbeat', category: 'Rhythmic / Movement',
    tint: '#a04a56',
    description: 'A gated signal snaps into a pulse, with repeats that can fall between beats.',
    chain: ['Gate', 'Clock', 'Repeat'],
    macros: [
      macro('Heart', 'energy', 71, 'Gate depth', 'Attack'), macro('Clock', 'crush', 56, 'Subdivision', 'Swing'),
      macro('Snap', 'heat', 44, 'Transient', 'Drive'), macro('Steps', 'echo', 27, 'Repeat rate', 'Feedback'),
      macro('Glitch', 'chaos', 19, 'Probability', 'Jitter'), macro('Push', 'motion', 63, 'Mod depth', 'Pan'),
    ],
  },
  {
    id: 'angel-exe', name: 'Angel.exe', category: 'Experimental',
    tint: '#9a6d92',
    description: 'A pristine octave double breaks into unstable halos and digital artifacts.',
    chain: ['Octave', 'Excite', 'Corrupt'],
    macros: [
      macro('Halo', 'air', 72, 'Octave mix', 'Spread'), macro('Grace', 'prism', 52, 'Shelf', 'Excite'),
      macro('Error', 'crush', 37, 'Bit depth', 'Dropout'), macro('Flicker', 'energy', 18, 'Gate rate', 'Chance'),
      macro('Stain', 'film', 28, 'Tape drive', 'Damping'), macro('Ascend', 'space', 63, 'Shimmer', 'Tail'),
    ],
  },
];

export const pads = [
  { name: 'Pickup', note: 'C4', length: 10 }, { name: 'Rise', note: 'C♯4', length: 13 },
  { name: 'Bloom', note: 'D4', length: 15 }, { name: 'Cut', note: 'D♯4', length: 9 },
  { name: 'Vowel', note: 'E4', length: 16 }, { name: 'Trace', note: 'F4', length: 11 },
  { name: 'Fall', note: 'F♯4', length: 14 }, { name: 'Tail', note: 'G4', length: 12 },
] as const;

export const demoName = 'Garden Glass.wav';

export function initialValues(preset: Preset) {
  return preset.macros.map(item => item.initial);
}
