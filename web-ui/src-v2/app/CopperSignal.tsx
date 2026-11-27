import { memo, useId, useMemo } from 'react';
import type { WaveformDto } from '../bridge/protocol';

export type AmplitudeMode = 'linear' | 'detail';
export const plotWidth = 1000;
export const plotHeight = 264;
const monoCenters = [132];
const stereoCenters = [63, 201];

function makePath(peaks: WaveformDto['channels'][number], center: number, scale: number, mode: AmplitudeMode) {
  if (!peaks.length) return '';
  const amplitude = (value: number) => mode === 'linear' ? value : Math.sign(value) * Math.abs(value) ** .8;
  const upper: string[] = [];
  const lower: string[] = [];
  const append = (x: number, peak: (typeof peaks)[number]) => {
    upper.push(`${x.toFixed(2)},${(center - amplitude(peak.max) * scale).toFixed(2)}`);
    lower.push(`${x.toFixed(2)},${(center - amplitude(peak.min) * scale).toFixed(2)}`);
  };
  // The bridge exposes coalesced buckets. Exact slice and transient positions use sample frames separately.
  append(0, peaks[0]);
  peaks.forEach((peak, index) => append((index + .5) / peaks.length * plotWidth, peak));
  append(plotWidth, peaks[peaks.length - 1]);
  return `M${upper.join('L')}L${lower.reverse().join('L')}Z`;
}

export const CopperSignal = memo(function CopperSignal({ waveform, stereo, mode, start, end, divisions }: {
  waveform: WaveformDto | null; stereo: boolean; mode: AmplitudeMode; start: number; end: number; divisions: number;
}) {
  const id = useId();
  const centers = stereo ? stereoCenters : monoCenters;
  const paths = useMemo(() => waveform?.channels.map((peaks, index) =>
    makePath(peaks, centers[index], stereo ? 54 : 112, mode)) ?? [], [waveform, centers, stereo, mode]);
  const selection = { x: start * plotWidth, width: (end - start) * plotWidth };

  return <svg className="wave-ink" viewBox={`0 0 ${plotWidth} ${plotHeight}`} preserveAspectRatio="none" aria-hidden="true">
    <defs>
      <linearGradient id={`${id}-copper`} x2="0%" y2="100%">
        <stop stopColor="#edbc97" /><stop offset=".24" stopColor="#cd8864" />
        <stop offset=".52" stopColor="#b46c4c" /><stop offset="1" stopColor="#8c4936" />
      </linearGradient>
      <linearGradient id={`${id}-polish`} x2="0%" y2="100%">
        <stop stopColor="#ffddae" /><stop offset=".35" stopColor="#e9a675" />
        <stop offset=".65" stopColor="#ce8055" /><stop offset="1" stopColor="#ad5c41" />
      </linearGradient>
      <clipPath id={`${id}-selection`}><rect {...selection} height={plotHeight} /></clipPath>
      <clipPath id={`${id}-surface`}>{paths.map((path, index) => <path key={index} d={path} />)}</clipPath>
      <pattern id={`${id}-grain`} width="3.7" height={plotHeight} patternUnits="userSpaceOnUse">
        <path d={`M1 0V${plotHeight}`} stroke="#3c221a" strokeOpacity={stereo ? .19 : .27} strokeWidth="1" />
      </pattern>
    </defs>
    {stereo && <rect className="lane-gap" y="122" width={plotWidth} height="20" />}
    <path className="division" d={stereo ? 'M0 122H1000M0 142H1000' : 'M0 66H1000M0 198H1000'} />
    {Array.from({ length: divisions - 1 }, (_, index) => (
      <path className="plot-grid" key={index} d={`M${(index + 1) / divisions * plotWidth} 0V${plotHeight}`} />
    ))}
    <rect {...selection} height={plotHeight} className="selection-wash" />
    {centers.map(center => <path className="zero-line" key={center} d={`M0 ${center}H1000`} />)}
    {paths.map((d, index) => <g key={index}>
      <path d={d} fill={`url(#${id}-copper)`} className="peak-edge" />
      <path d={d} fill={`url(#${id}-polish)`} clipPath={`url(#${id}-selection)`} />
    </g>)}
    <rect width={plotWidth} height={plotHeight} fill={`url(#${id}-grain)`} clipPath={`url(#${id}-surface)`} />
  </svg>;
});
