export type DemoSampleMetadata = {
  id: string;
  name: string;
  description: string;
  durationSeconds: number;
  sampleRate: number;
  channelCount: 1 | 2;
  bpm: number;
  key: string;
};

export type DemoWaveformPeak = { minimum: number; maximum: number };

export type DemoSlice = {
  id: string;
  index: number;
  name: string;
  note: string;
  startSample: number;
  endSample: number;
  volume: number;
  pan: number;
  pitch: number;
  attackSeconds: number;
  decaySeconds: number;
  sustain: number;
  releaseSeconds: number;
};

export type DemoTransient = { id: string; samplePosition: number; strength: number };

export type DemoDataset = {
  metadata: DemoSampleMetadata;
  peaks: readonly DemoWaveformPeak[];
  slices: readonly DemoSlice[];
  transients: readonly DemoTransient[];
  initialSliceId: string;
  initialPositionSamples: number;
};

const sampleRate = 48_000;

function makePeaks(count: number, density: 'open' | 'dense'): readonly DemoWaveformPeak[] {
  return Array.from({ length: count }, (_, index) => {
    const x = index / (count - 1);
    const broad = density === 'open'
      ? Math.exp(-Math.pow((x - 0.2) / 0.13, 2)) * 0.5
        + Math.exp(-Math.pow((x - 0.52) / 0.2, 2)) * 0.82
        + Math.exp(-Math.pow((x - 0.82) / 0.11, 2)) * 0.57
      : 0.24 + 0.4 * Math.abs(Math.sin(x * 21.8)) + 0.2 * Math.abs(Math.sin(x * 63.1));
    const texture = 0.18 + 0.82 * Math.abs(Math.sin(index * 1.71 + 0.28) * Math.cos(index * 0.37));
    const amplitude = Math.min(0.96, 0.025 + broad * texture);
    const asymmetry = 0.78 + 0.18 * Math.abs(Math.sin(index * 0.21));
    return {
      minimum: -amplitude * asymmetry,
      maximum: amplitude * (1.05 - asymmetry * 0.14),
    };
  });
}

function makeSlices(sampleCount: number, names: readonly string[], notes: readonly string[], fractions: readonly number[]): readonly DemoSlice[] {
  return names.map((name, index) => {
    const startSample = Math.round(fractions[index] * sampleCount);
    const endSample = Math.round(fractions[index + 1] * sampleCount);
    return {
      id: `slice-${String(index + 1).padStart(2, '0')}`,
      index: index + 1,
      name,
      note: notes[index],
      startSample,
      endSample,
      volume: [0.74, 0.68, 0.82, 0.61, 0.78, 0.72, 0.85, 0.66, 0.8, 0.63, 0.76, 0.71, 0.88, 0.69, 0.73, 0.81][index],
      pan: [-0.12, 0.08, 0, -0.2, 0.14, -0.05, 0.18, 0, -0.1, 0.12, -0.16, 0.04, 0.2, -0.08, 0.1, -0.03][index],
      pitch: [0, 2, 0, -1, 3, 0, -2, 1, 0, -3, 2, 0, 1, -1, 2, 0][index],
      attackSeconds: [0.005, 0.012, 0.008, 0.02, 0.006, 0.014, 0.009, 0.018, 0.007, 0.011, 0.005, 0.016, 0.01, 0.006, 0.013, 0.008][index],
      decaySeconds: [0.1, 0.18, 0.12, 0.24, 0.09, 0.16, 0.2, 0.11, 0.17, 0.13, 0.21, 0.1, 0.15, 0.19, 0.12, 0.16][index],
      sustain: [0.82, 0.68, 0.92, 0.56, 0.76, 0.88, 0.64, 0.8, 0.72, 0.9, 0.61, 0.84, 0.7, 0.94, 0.66, 0.78][index],
      releaseSeconds: [0.1, 0.16, 0.08, 0.22, 0.12, 0.18, 0.09, 0.14, 0.2, 0.11, 0.17, 0.1, 0.19, 0.13, 0.15, 0.12][index],
    };
  });
}

function makeTransients(sampleCount: number, count: number): readonly DemoTransient[] {
  return Array.from({ length: count }, (_, index) => {
    const fraction = ((index * 37 + 11) % count) / count;
    return {
      id: `transient-${index + 1}`,
      samplePosition: Math.round(fraction * sampleCount),
      strength: 0.32 + ((index * 17) % 61) / 100,
    };
  }).sort((a, b) => a.samplePosition - b.samplePosition);
}

function makeDataset(options: {
  id: string;
  name: string;
  description: string;
  durationSeconds: number;
  bpm: number;
  key: string;
  names: readonly string[];
  notes: readonly string[];
  boundaries: readonly number[];
  transientCount: number;
  peakCount: number;
  density: 'open' | 'dense';
  initialSlice: number;
}): DemoDataset {
  const sampleCount = Math.round(options.durationSeconds * sampleRate);
  const slices = makeSlices(sampleCount, options.names, options.notes, options.boundaries);
  const selectedSlice = slices[options.initialSlice - 1] ?? slices[0];
  return {
    metadata: {
      id: options.id,
      name: options.name,
      description: options.description,
      durationSeconds: options.durationSeconds,
      sampleRate,
      channelCount: 2,
      bpm: options.bpm,
      key: options.key,
    },
    peaks: makePeaks(options.peakCount, options.density),
    slices,
    transients: makeTransients(sampleCount, options.transientCount),
    initialSliceId: slices[options.initialSlice - 1]?.id ?? slices[0].id,
    initialPositionSamples: Math.round((selectedSlice.startSample + selectedSlice.endSample) / 2),
  };
}

export const demoDatasets = {
  standard: makeDataset({
    id: 'demo-garden-glass',
    name: 'Garden Glass',
    description: 'CHOPPED TEXTURE',
    durationSeconds: 4.82,
    bpm: 148,
    key: 'G♯ MIN',
    names: ['Glass A', 'Soft edge', 'Bloom', 'Glass B', 'Faint light', 'Afterimage', 'Tail'],
    notes: ['C3', 'D♯3', 'F3', 'G♯3', 'A♯3', 'C4', 'D♯4'],
    boundaries: [0, 0.105, 0.245, 0.382, 0.555, 0.705, 0.865, 1],
    transientCount: 13,
    peakCount: 256,
    density: 'open',
    initialSlice: 3,
  }),
  dense: makeDataset({
    id: 'demo-static-bloom',
    name: 'Static Bloom (Long Edit)',
    description: 'PERCUSSIVE FRAGMENT',
    durationSeconds: 8.64,
    bpm: 132,
    key: 'D MIN',
    names: ['Cut 01', 'Cut 02', 'Cut 03', 'Cut 04', 'Cut 05', 'Cut 06', 'Cut 07', 'Cut 08', 'Cut 09', 'Cut 10', 'Cut 11', 'Cut 12', 'Cut 13', 'Cut 14'],
    notes: ['D3', 'F3', 'A3', 'C4', 'D4', 'F4', 'A4', 'C5', 'D5', 'F5', 'A5', 'C6', 'D6', 'F6'],
    boundaries: [0, 0.038, 0.104, 0.164, 0.251, 0.306, 0.379, 0.428, 0.521, 0.574, 0.653, 0.716, 0.794, 0.864, 1],
    transientCount: 54,
    peakCount: 420,
    density: 'dense',
    initialSlice: 8,
  }),
} satisfies Record<string, DemoDataset>;
