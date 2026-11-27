import { useId, useLayoutEffect, useMemo, useRef, useState, type CSSProperties, type MouseEvent } from 'react';
import type { DemoWaveformPeak } from '../../../src-v2/demo/demoData';
import { ChevronLeft, ChevronRight } from 'lucide-react';
import { datasets, number, type Dataset } from './data';
import { displayAmplitude, groupAttacks, hitSlice, minDirectWidth, type AmplitudeMode } from './display';
import { SliceIndex } from './SliceIndex';

const plotWidth = 1000;
const plotHeight = 264;

function makePath(peaks: readonly DemoWaveformPeak[], center: number, scale: number, mode: AmplitudeMode) {
  const amplitude = (value: number) => displayAmplitude(value, mode) * scale;
  const points = peaks.map(({ minimum, maximum }, i) => {
    const x = (i / (peaks.length - 1) * plotWidth).toFixed(2);
    return [`${x},${(center - amplitude(maximum)).toFixed(2)}`, `${x},${(center - amplitude(minimum)).toFixed(2)}`];
  });
  return `M${points.map(p => p[0]).join('L')}L${points.reverse().map(p => p[1]).join('L')}Z`;
}

const pct = (fraction: number) => `${fraction * 100}%`;

export function Waveform({ dataset, selected, position, select, playing, mode, setMode }: {
  dataset: Dataset; selected: number; position: number; select: (index: number) => void; playing: boolean;
  mode: AmplitudeMode; setMode: (mode: AmplitudeMode) => void;
}) {
  const id = useId();
  const plotRef = useRef<HTMLDivElement>(null);
  const [width, setWidth] = useState(1000);
  const [hovered, setHovered] = useState<number | null>(null);
  const indexRef = useRef<HTMLDivElement>(null);
  const narrow = dataset.slices.map(slice => (slice.end - slice.start) * width < minDirectWidth);
  const showIndex = dataset.slices.length > 12 || narrow.some(Boolean);
  const stereo = dataset.channels.length === 2;
  const centers = useMemo(() => stereo ? [63, 201] : [plotHeight / 2], [stereo]);
  const paths = useMemo(() => dataset.channels.map((peaks, i) =>
    makePath(peaks, centers[i], stereo ? 54 : 112, mode)), [dataset, centers, stereo, mode]);

  useLayoutEffect(() => {
    const plot = plotRef.current;
    if (!plot) return;
    setWidth(plot.clientWidth);
    const observer = new ResizeObserver(([entry]) => setWidth(entry.contentRect.width));
    observer.observe(plot);
    return () => observer.disconnect();
  }, []);

  const attacks = useMemo(() => groupAttacks(dataset.attacks, width), [dataset, width]);
  const hasClusters = attacks.some(group => group.count > 1);

  function onPlotClick(event: MouseEvent<HTMLDivElement>) {
    const rect = event.currentTarget.getBoundingClientRect();
    const index = hitSlice(dataset.slices, (event.clientX - rect.left) / rect.width);
    if (index < 0) return;
    if (!narrow[index]) {
      select(index);
      return;
    }
    indexRef.current?.querySelector<HTMLButtonElement>(`[data-slice-index="${index}"]`)?.focus();
  }

  const slice = dataset.slices[selected];
  const span = slice.end - slice.start;
  const duration = span * dataset.duration;
  const partName = slice.name === `Cut ${number(selected)}` ? null : slice.name;
  const startX = slice.start * plotWidth;
  const endX = slice.end * plotWidth;
  const format = (part: number) => (part * dataset.duration).toFixed(duration < .1 ? 3 : 2);
  const visibleTicks = width < 640 ? 4 : 8;
  const ticks = Array.from({ length: visibleTicks + 1 }, (_, i) => i / visibleTicks);

  return <section className="plate" aria-label="Sample waveform">
    <div className="plate-head">
      <div className="plate-label"><span className="copper-notch" /> Source waveform</div>
      <div className="plate-tools">
        <span className="plate-summary">{dataset.slices.length} slices</span>
        <div className="amplitude-switch" role="group" aria-label="Waveform amplitude display">
          <button type="button" aria-pressed={mode === 'linear'} onClick={() => setMode('linear')}
            title="Linear amplitude: equal scales for both channels">Linear</button>
          <button type="button" aria-pressed={mode === 'detail'} onClick={() => setMode('detail')}
            title="Detail: boosts quiet peaks visually (|a| to the power of 0.8)">Detail</button>
        </div>
      </div>
    </div>
    <div className="signal">
      <div className={`channel-legend${stereo ? ' is-stereo' : ''}`} aria-hidden="true">
        {stereo ? <><span>L</span><span>R</span></> : <span>{dataset === datasets.comparison ? 'Σ' : 'M'}</span>}
      </div>
      <div className="plot" ref={plotRef} aria-label={stereo ? 'Left and right channel peaks' : 'Sample peak envelope'}>
        <svg className="wave-ink" viewBox={`0 0 ${plotWidth} ${plotHeight}`} preserveAspectRatio="none" aria-hidden="true">
          <defs>
            <linearGradient id={`${id}-copper`} x2="0%" y2="100%">
              <stop stopColor="#edbc97" /><stop offset=".24" stopColor="#cd8864" />
              <stop offset=".52" stopColor="#b46c4c" /><stop offset="1" stopColor="#8c4936" />
            </linearGradient>
            <linearGradient id={`${id}-polish`} x2="0%" y2="100%">
              <stop stopColor="#ffddae" /><stop offset=".35" stopColor="#e9a675" />
              <stop offset=".65" stopColor="#ce8055" /><stop offset="1" stopColor="#ad5c41" />
            </linearGradient>
            <clipPath id={`${id}-selection`}><rect x={startX} width={endX - startX} height={plotHeight} /></clipPath>
            <clipPath id={`${id}-surface`}>{paths.map((path, i) => <path key={i} d={path} />)}</clipPath>
            <pattern id={`${id}-grain`} width="3.7" height={plotHeight} patternUnits="userSpaceOnUse">
              <path d={`M1 0V${plotHeight}`} stroke="#3c221a" strokeOpacity={stereo ? .19 : .27} strokeWidth="1" />
            </pattern>
          </defs>
          {stereo && <rect className="lane-gap" y="122" width={plotWidth} height="20" />}
          <path className="division" d={stereo ? 'M0 122H1000M0 142H1000' : 'M0 66H1000M0 198H1000'} />
          {ticks.slice(1, -1).map(t => <path className="plot-grid" key={t} d={`M${t * plotWidth} 0V${plotHeight}`} />)}
          <rect x={startX} y={0} width={endX - startX} height={plotHeight} className="selection-wash" />
          {centers.map(center => <path className="zero-line" key={center} d={`M0 ${center}H1000`} />)}
          {paths.map((d, i) => <g key={i}>
            <path d={d} fill={`url(#${id}-copper)`} className="peak-edge" />
            <path d={d} fill={`url(#${id}-polish)`} clipPath={`url(#${id}-selection)`} />
          </g>)}
          <rect width={plotWidth} height={plotHeight} fill={`url(#${id}-grain)`} clipPath={`url(#${id}-surface)`} />
        </svg>
        <div className="cut-regions" aria-label="Slice ranges" onClick={onPlotClick}>
          {dataset.slices.map((part, i) => {
            const pixels = (part.end - part.start) * width;
            const className = `cut-region${i === selected ? ' is-selected' : ''}${i === hovered ? ' is-linked' : ''}`;
            const common = {
              className, style: { left: pct(part.start), width: pct(part.end - part.start) },
              onPointerEnter: () => setHovered(i), onPointerLeave: () => setHovered(null),
            };
            const label = `${number(i)} · ${part.name}`;
            if (narrow[i]) {
              return <div key={i} {...common} className={`${className} is-narrow`}
                title={`${label} · Use slice index`} />;
            }
            return <button key={i} {...common} type="button" tabIndex={showIndex ? -1 : 0}
              aria-pressed={i === selected} aria-label={`Slice ${label}`} title={label}
              onClick={event => { if (event.detail === 0) { event.stopPropagation(); select(i); } }}>
              <span>{number(i)}{pixels >= 74 && <small>{part.note}</small>}</span>
            </button>;
          })}
        </div>
        <div className="transient-lane" aria-label="Transient markers">
          {attacks.map(group => {
            const grouped = group.count > 1;
            const at = grouped ? (group.start + group.end) / 2 : group.peak.at;
            const timing = grouped
              ? `${(group.start * dataset.duration).toFixed(3)}–${(group.end * dataset.duration).toFixed(3)} s`
              : `${(at * dataset.duration).toFixed(3)} s`;
            return <span key={group.start} className={grouped ? 'is-grouped' : undefined}
              title={`${group.count} ${grouped ? 'transients' : 'transient'} · ${timing}`}
              style={{ left: pct(at), '--strength': group.peak.strength } as CSSProperties} />;
          })}
        </div>
        <div className={`playhead${playing ? ' is-playing' : ''}`} style={{ left: pct(position) }} aria-hidden="true" />
      </div>
    </div>
    <div className="scale" aria-label="Time in seconds">
      {ticks.map(t => <span key={t} style={{ left: pct(t) }}>{(t * dataset.duration).toFixed(2)}{t === 1 ? ' s' : ''}</span>)}
    </div>
    {showIndex && <SliceIndex slices={dataset.slices} selected={selected} select={select}
      hovered={hovered} onHover={setHovered} indexRef={indexRef} />}
    <div className="plate-selection" aria-label="Selected range">
      <div className="selection-context">
        <div className="selection-arrows" role="group" aria-label="Step through slices">
          <button type="button" title="Previous slice" aria-label="Previous slice" disabled={selected === 0}
            onClick={() => select(selected - 1)}><ChevronLeft size={14} /></button>
          <button type="button" title="Next slice" aria-label="Next slice" disabled={selected === dataset.slices.length - 1}
            onClick={() => select(selected + 1)}><ChevronRight size={14} /></button>
        </div>
        <span className="selection-name"><b>Slice {number(selected)}</b><i />{partName && `${partName} · `}{slice.note}</span>
        <span className="selection-time">{format(slice.start)}–{format(slice.end)} s
          {(duration < .1 || duration >= 1) && <span className={duration < .1 ? 'micro-length' : 'slice-length'}>
            {' · '}{duration < .1 ? `${Math.round(duration * 1000)} ms` : `${duration.toFixed(2)} s`}
          </span>}
        </span>
      </div>
      <span className="transient-readout" title={hasClusters ? 'Double marks show grouped transients. Hover a mark for its count and time.' : undefined}>
        {hasClusters && <i className="cluster-key" aria-hidden="true" />}
        {dataset.attacks.length} transients{hasClusters && ` / ${attacks.length} marks`}
      </span>
    </div>
  </section>;
}
