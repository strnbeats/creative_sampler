import { memo, useMemo, type CSSProperties } from 'react';
import type { AnalysisDto, SampleMetadataDto } from '../bridge/protocol';

export type Transients = NonNullable<AnalysisDto['transients']>;
type Mark = { start: number; end: number; count: number; strength: number };

export function groupTransients(data: Transients | null, sample: SampleMetadataDto, width: number): Mark[] {
  if (!data || data.sampleRate <= 0 || sample.durationSeconds <= 0) return [];
  const frames = data.sampleRate * sample.durationSeconds;
  const marks: Mark[] = [];
  const events = data.positions.filter(event => event.samplePosition >= 0 && event.samplePosition < frames)
    .sort((left, right) => left.samplePosition - right.samplePosition);
  for (const event of events) {
    const at = event.samplePosition / frames;
    const last = marks.at(-1);
    if (last && (at - last.start) * width < 9) {
      last.end = at;
      last.count++;
      last.strength = Math.max(last.strength, event.strength);
    } else {
      marks.push({ start: at, end: at, count: 1, strength: event.strength });
    }
  }
  return marks;
}

export const TransientLane = memo(function TransientLane({ marks, duration }: { marks: readonly Mark[]; duration: number }) {
  const milliseconds = useMemo(() => marks.map(mark => {
    const start = (mark.start * duration).toFixed(3);
    const end = (mark.end * duration).toFixed(3);
    return mark.count === 1 ? `1 transient · ${start} s` : `${mark.count} transients · ${start}–${end} s`;
  }), [marks, duration]);

  return <div className="transient-lane" aria-label="Transient markers">
    {marks.map((mark, index) => <span key={index} className={mark.count > 1 ? 'is-grouped' : undefined}
      title={milliseconds[index]} style={{
        left: `${(mark.start + mark.end) * 50}%`, '--strength': Math.min(1, Math.max(0, mark.strength)),
      } as CSSProperties} />)}
  </div>;
});
