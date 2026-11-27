import type { Dataset, Slice } from './data';

export type AmplitudeMode = 'linear' | 'detail';

export const minDirectWidth = 24;
export const detailExponent = .8;

export function displayAmplitude(value: number, mode: AmplitudeMode) {
  return mode === 'linear' ? value : Math.sign(value) * Math.abs(value) ** detailExponent;
}

export function hitSlice(slices: readonly Slice[], at: number) {
  return slices.findIndex(slice => slice.start <= at && at < slice.end);
}

export function groupAttacks(events: Dataset['attacks'], width: number) {
  const groups: Array<{ start: number; end: number; count: number; peak: Dataset['attacks'][number] }> = [];
  for (const event of events) {
    const last = groups.at(-1);
    if (last && (event.at - last.start) * width < 9) {
      last.end = event.at;
      last.count++;
      if (event.strength > last.peak.strength) last.peak = event;
    } else {
      groups.push({ start: event.at, end: event.at, count: 1, peak: event });
    }
  }
  return groups;
}
