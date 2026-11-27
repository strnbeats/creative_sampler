import { demoDatasets, type DemoWaveformPeak } from '../../../src-v2/demo/demoData';

export type Parameters = {
  volume: number; pan: number; pitch: number;
  attack: number; decay: number; sustain: number; release: number;
};

export type Slice = { start: number; end: number; name: string; note: string; params: Parameters };
type Attack = { at: number; strength: number };

export type Dataset = {
  name: string; duration: number; rate: number; bpm: number | null; key: string | null;
  channels: readonly (readonly DemoWaveformPeak[])[];
  slices: readonly Slice[]; attacks: readonly Attack[]; selected: number; position: number;
};

const notes = ['C3', 'D♯3', 'F3', 'G♯3', 'A♯3', 'C4', 'D♯4'];
const defaults = (i: number): Parameters => ({
  volume: .62 + i % 7 * .075, pan: (i % 5 - 2) * .18, pitch: i % 7 - 3,
  attack: 6 + i % 5 * 7, decay: 90 + i % 7 * 18, sustain: .52 + i % 6 * .08, release: 70 + i % 11 * 16,
});

function sliceRanges(ranges: readonly (readonly [number, number])[], names: readonly string[] = []): Slice[] {
  return ranges.map(([start, end], i) => ({
    start, end, name: names[i] ?? `Cut ${number(i)}`, note: notes[i % notes.length], params: defaults(i),
  }));
}

function adjacent(boundaries: readonly number[]) {
  return boundaries.slice(1).map((end, index) => [boundaries[index], end] as const);
}

function peaks(envelope: (x: number) => number, seed = 0): DemoWaveformPeak[] {
  return Array.from({ length: 768 }, (_, i) => {
    const x = i / 767;
    const texture = .29 + .71 * Math.abs(Math.sin(i * 1.719 + seed) * Math.cos(i * .211 - seed));
    const strength = Math.min(.99, Math.max(0, envelope(x)) * texture);
    return { minimum: -strength * (.73 + .24 * Math.abs(Math.sin(i * .59 + seed))), maximum: strength };
  });
}

function pulse(x: number, at: number, strength: number, tail: number) {
  const d = x - at;
  return strength * Math.exp(d < 0 ? d / .0014 : -d / tail);
}

const marks = (values: readonly number[]): Attack[] =>
  values.map((at, i) => ({ at, strength: .38 + (i * 17 % 23) / 37 }));

const sparseTimes = [.083, .364, .701, .898];
const denseTimes = Array.from({ length: 111 }, (_, i) => .01 + i * .00878 + i % 4 * .00046);
const microWidths = [6, .9, 4, 2.9, 6.5, 1.1, 3, 6, 1.5, 5.1, 2, 4, 2.7, 5.1, .36, .71, .48, 13.91, 1.1, 3.4, 4, 1.9, 5.6, 2.3, 5.2, 2.7, 5.1, 4.4];
const microTotal = microWidths.reduce((sum, width) => sum + width, 0);
let cumulative = 0;
const microBoundaries = [0, ...microWidths.map(width => (cumulative += width) / microTotal)];
const dynamicsTimes = [.05, .217, .278, .356, .456, .512, .669, .718, .803, .952];

function denseChannel(shift: number, phase: number) {
  return peaks(x => {
    const body = .24 + .43 * Math.pow(Math.abs(Math.sin((x + shift) * 14.6)), 1.5);
    const bursts = denseTimes.reduce((max, at, i) => Math.max(max, pulse(x, at + shift * .13, .34 + .43 * Math.abs(Math.sin(i * 2.37 + phase)), .005)), 0);
    return body + bursts;
  }, phase);
}

const standard = demoDatasets.standard;
const comparisonLength = standard.metadata.durationSeconds * standard.metadata.sampleRate;
export const datasets = {
  sparse: {
    name: 'Distant Impacts.wav', duration: 12.8, rate: 48000, bpm: null, key: null,
    channels: [peaks(x => .006 + sparseTimes.reduce((sum, at, i) => sum + pulse(x, at, [.7, 1.45, .34, 1.13][i], [.012, .03, .014, .036][i]), 0))],
    slices: sliceRanges([[.071, .157], [.351, .48], [.69, .754], [.884, .984]], ['First impact', 'Room strike', 'Distant tap', 'Long tail']),
    attacks: marks(sparseTimes), selected: 1, position: .397,
  },
  stereo: {
    name: 'Chrome Choir — Stereo Render.wav', duration: 8.65, rate: 96000, bpm: 146, key: 'D♯ min',
    channels: [denseChannel(0, .2), denseChannel(.063, 2.42)],
    slices: sliceRanges(adjacent([0, .11, .19, .298, .426, .532, .706, .836, .912, 1])),
    attacks: marks(denseTimes), selected: 4, position: .473,
  },
  slices: {
    name: 'Microcuts — Vocal Stutter.wav', duration: 5.12, rate: 48000, bpm: 164, key: 'F min',
    channels: [peaks(x => .08 + .21 * Math.pow(Math.abs(Math.sin(x * 16)), 1.6) +
      microBoundaries.slice(0, -1).reduce((max, at, i) => Math.max(max, pulse(x, at, .53 + i % 5 * .1, .019)), 0), 1.3)],
    slices: sliceRanges(adjacent(microBoundaries)),
    attacks: marks(microBoundaries.slice(0, -1)), selected: 14, position: .546,
  },
  dynamics: {
    name: 'Pressure Study — Whisper to Clipping.wav', duration: 15.6, rate: 48000, bpm: 112, key: 'A min',
    channels: [peaks(x => {
      const bed = x < .18 || (x > .53 && x < .66) ? .019 : x < .39 ? .24 : x < .53 ? .62 : .1;
      const tail = dynamicsTimes.reduce((max, at, i) => Math.max(max, pulse(x, at, [.06, .42, .76, .38, 1.4, 1.65, .21, .98, .34, .12][i], .012 + i % 3 * .008)), 0);
      return bed + tail + .046 * Math.pow(Math.abs(Math.sin(x * 215)), 6);
    }, 4)],
    slices: sliceRanges(adjacent([0, .186, .426, .537, .657, .86, 1]), ['Whisper', 'Build', 'Pressure', 'Breath', 'Impact', 'Decay']),
    attacks: marks(dynamicsTimes), selected: 3, position: .578,
  },
  comparison: {
    name: standard.metadata.name, duration: standard.metadata.durationSeconds, rate: standard.metadata.sampleRate,
    bpm: standard.metadata.bpm, key: 'G♯ minor', channels: [standard.peaks],
    slices: standard.slices.map(s => ({
      start: s.startSample / comparisonLength, end: s.endSample / comparisonLength, name: s.name, note: s.note,
      params: { volume: s.volume, pan: s.pan, pitch: s.pitch, attack: s.attackSeconds * 1000,
        decay: s.decaySeconds * 1000, sustain: s.sustain, release: s.releaseSeconds * 1000 },
    })),
    attacks: standard.transients.map(t => ({ at: t.samplePosition / comparisonLength, strength: t.strength })),
    selected: 2, position: standard.initialPositionSamples / comparisonLength,
  },
} satisfies Record<string, Dataset>;

export type DatasetId = keyof typeof datasets;
export function number(index: number) { return String(index + 1).padStart(2, '0'); }
