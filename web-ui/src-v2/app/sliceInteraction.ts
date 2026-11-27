import type { SliceDto, SliceId } from '../bridge/protocol';

export const minDirectWidth = 24;
const boundaryGuard = 2;

export function resolveSlice(slices: readonly SliceDto[], id: SliceId | null): SliceDto | null {
  return slices.find(slice => slice.id === id) ?? slices[0] ?? null;
}

export function hitSlice(slices: readonly SliceDto[], frames: number, x: number, width: number) {
  if (frames <= 0 || width <= 0 || x < 0 || x >= width) return null;
  let match: SliceDto | undefined;
  for (const slice of slices) {
    const start = slice.startSample / frames * width;
    const end = slice.endSample / frames * width;
    if (x < start) break;
    if (x >= end) continue;
    // A malformed overlapping interval never receives a direct commit.
    if (match) return { id: match.id, action: 'focus' } as const;
    match = slice;
  }
  if (!match) return null;

  const start = match.startSample / frames * width;
  const end = match.endSample / frames * width;
  const nearCut = (match.startSample > 0 && x - start < boundaryGuard)
    || (match.endSample < frames && end - x <= boundaryGuard);
  return { id: match.id, action: end - start < minDirectWidth || nearCut ? 'focus' : 'select' } as const;
}

export function sliceKey(key: string, index: number, count: number): { focus: number } | { select: number } | null {
  switch (key) {
    case 'ArrowLeft': case 'ArrowUp': return { focus: Math.max(0, index - 1) };
    case 'ArrowRight': case 'ArrowDown': return { focus: Math.min(count - 1, index + 1) };
    case 'Home': return { focus: 0 };
    case 'End': return { focus: count - 1 };
    case 'Enter': case ' ': return { select: index };
    default: return null;
  }
}

export function sliceTime(frames: number, rate: number) {
  // Preserve enough decimal places to distinguish adjacent sample frames, including at 192 kHz.
  return (frames / rate).toFixed(Math.max(3, Math.ceil(Math.log10(rate))));
}
