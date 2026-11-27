import { useId } from 'react';
import type { CSSProperties } from 'react';
import { demoDatasets } from '../../src-v2/demo/demoData';

export const sample = demoDatasets.standard;
export const length = Math.round(sample.metadata.durationSeconds * sample.metadata.sampleRate);
export const formatSlice = (index: number) => String(index).padStart(2, '0');
export const time = (seconds: number) => seconds.toFixed(2);

export type Select = (id: string) => void;
type Mode = 'etch' | 'facet' | 'trace';

function outline(start: number, end: number, center: number, scale: number) {
  const first = Math.floor(start / length * sample.peaks.length);
  const last = Math.min(sample.peaks.length - 1, Math.ceil(end / length * sample.peaks.length) - 1);
  const points = sample.peaks.slice(first, last + 1).map(({ minimum, maximum }, index) => {
    const x = (index / (last - first)) * 1000;
    return { x, top: center - maximum * scale, bottom: center - minimum * scale };
  });
  const top = points.map(p => `${p.x.toFixed(2)},${p.top.toFixed(2)}`);
  const bottom = [...points].reverse().map(p => `${p.x.toFixed(2)},${p.bottom.toFixed(2)}`);
  return `M${top.join(' L')} L${bottom.join(' L')} Z`;
}

export function Signal({ mode, selectedId, onSelect, position, start = 0, end = length }:
  { mode: Mode; selectedId: string; onSelect: Select; position: number; start?: number; end?: number }) {
  const id = useId();
  const fraction = (value: number) => (value - start) / (end - start);
  const slices = sample.slices.filter(slice => slice.startSample >= start && slice.endSample <= end);
  const selected = slices.find(slice => slice.id === selectedId);
  const chosen = selected ? { x: fraction(selected.startSample) * 1000, w: (selected.endSample - selected.startSample) / (end - start) * 1000 } : null;
  const isFacet = mode === 'facet';
  const isTrace = mode === 'trace';
  const center = isFacet ? 132 : 120;
  const scale = isFacet ? 110 : isTrace ? 128 : 138;
  const path = outline(start, end, center, scale);

  return <div className={`signal signal--${mode}`}>
    <svg viewBox="0 0 1000 240" preserveAspectRatio="none" aria-hidden="true">
      <defs>
        <clipPath id={`${id}-selected`}><rect x={chosen?.x ?? 0} width={chosen?.w ?? 0} height="240" /></clipPath>
        <clipPath id={`${id}-wave`}><path d={path} /></clipPath>
        <linearGradient id={`${id}-tone`} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="var(--signal-high)" />
          <stop offset="51%" stopColor="var(--signal-core)" />
          <stop offset="100%" stopColor="var(--signal-low)" />
        </linearGradient>
        <linearGradient id={`${id}-selection`} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="var(--selected-high)" />
          <stop offset="100%" stopColor="var(--selected-low)" />
        </linearGradient>
        <pattern id={`${id}-grain`} width={isTrace ? 7 : 4} height={isTrace ? 6 : 9} patternUnits="userSpaceOnUse">
          <path d={isTrace ? 'M0 3H7 M3.5 0V6' : 'M1 0V9'} stroke="var(--signal-ink)" opacity={isTrace ? .37 : .32} strokeWidth="1" />
        </pattern>
      </defs>
      {chosen && <rect className="signal-window" x={chosen.x} width={chosen.w} y={isTrace ? 8 : 16} height={isTrace ? 224 : 208} />}
      <path className="signal-axis" d={`M0 ${center}H1000`} />
      {isFacet && [38, 28, 18, 9].map((depth, i) => <path key={depth} d={path}
        transform={`translate(0, ${-depth})`} className="signal-depth" style={{ opacity: .24 + i * .1 }} />)}
      <path d={path} fill={`url(#${id}-tone)`} />
      <path d={path} fill={`url(#${id}-selection)`} clipPath={`url(#${id}-selected)`} />
      <rect width="1000" height="240" fill={`url(#${id}-grain)`} clipPath={`url(#${id}-wave)`} />
      {isFacet && <path d={path} className="signal-edge" />}
      {sample.transients.filter(event => event.samplePosition >= start && event.samplePosition < end).map(event => {
        const x = fraction(event.samplePosition) * 1000;
        const y = isFacet ? 199 : isTrace ? 20 : 208;
        return <path key={event.id} className="signal-attack" d={`M${x} ${y}v${7 + event.strength * 16}`} />;
      })}
      {position >= start && (position < end || end === length) && <g className="signal-playhead" transform={`translate(${fraction(position) * 1000}, 0)`}>
        <path d="M0 9V231" /><path d="M-4 9H4L0 17Z" />
      </g>}
    </svg>
    <div className="signal-slices" aria-label="Waveform slices">
      {slices.map(slice => <button key={slice.id} aria-pressed={selectedId === slice.id} aria-label={`Slice ${formatSlice(slice.index)} · ${slice.name}`}
        title={`${slice.name} · ${slice.note}`} onClick={() => onSelect(slice.id)}
        style={{ '--start': `${fraction(slice.startSample) * 100}%`, '--width': `${(slice.endSample - slice.startSample) / (end - start) * 100}%` } as CSSProperties}>
        <span>{formatSlice(slice.index)}</span><small>{slice.note}</small>
      </button>)}
    </div>
  </div>;
}
