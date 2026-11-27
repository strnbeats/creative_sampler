import { useId, type CSSProperties } from 'react';
import { Pause, Play, Square } from 'lucide-react';
import type { DemoSlice } from '../../src-v2/demo/demoData';
import { formatSlice, length, sample, time } from './Signal';

export function SliceIdentity({ slice }: { slice: DemoSlice }) {
  return <div className="slice-id">
    <span className="slice-index mono">{formatSlice(slice.index)}</span>
    <div><span className="mono">Selected slice</span><strong>{slice.name}</strong><small>{slice.note} · {time((slice.endSample - slice.startSample) / sample.metadata.sampleRate)} s</small></div>
  </div>;
}

export function Envelope({ slice }: { slice: DemoSlice }) {
  const id = useId();
  const a = 12 + Math.sqrt(slice.attackSeconds) * 82;
  const d = a + Math.sqrt(slice.decaySeconds) * 92;
  const s = 92 - slice.sustain * 74;
  const r = 222 - Math.sqrt(slice.releaseSeconds) * 86;
  const curve = `M4 94L${a} 12L${d} ${s}H${r}L222 94`;
  const values = [
    ['Attack', `${Math.round(slice.attackSeconds * 1000)} ms`],
    ['Decay', `${Math.round(slice.decaySeconds * 1000)} ms`],
    ['Sustain', `${Math.round(slice.sustain * 100)}%`],
    ['Release', `${Math.round(slice.releaseSeconds * 1000)} ms`],
  ];
  return <section className="envelope" aria-label="Amplitude envelope">
    <h2>Envelope</h2>
    <svg viewBox="0 0 226 100" preserveAspectRatio="none" aria-hidden="true">
      <defs><linearGradient id={id} x1="0" y1="0" x2="0" y2="1">
        <stop stopColor="currentColor" stopOpacity=".28"/><stop offset="100%" stopColor="currentColor" stopOpacity="0"/>
      </linearGradient></defs>
      <path d="M4 94H222M4 54H222M4 12H222" className="envelope-guides"/>
      <path d={`${curve}Z`} fill={`url(#${id})`} />
      <path d={curve} className="envelope-curve"/>
      {[[a, 12], [d, s], [r, s]].map(([x, y]) => <circle key={x} cx={x} cy={y} r="2.5" className="envelope-node"/>)}
    </svg>
    <dl>{values.map(([label, value]) => <div key={label}><dt>{label}</dt><dd>{value}</dd></div>)}</dl>
  </section>;
}

export function Parameters({ slice, type }: { slice: DemoSlice; type: 'dial' | 'bar' | 'rail' }) {
  const pan = Math.round(slice.pan * 100);
  const params = [
    { label: 'Volume', value: `${slice.volume.toFixed(2)}×`, point: slice.volume * 50 },
    { label: 'Pan', value: pan === 0 ? 'Center' : `${Math.abs(pan)} ${pan < 0 ? 'L' : 'R'}`, point: 50 + pan / 2 },
    { label: 'Pitch', value: `${slice.pitch > 0 ? '+' : ''}${slice.pitch} st`, point: 50 + slice.pitch / 48 * 100 },
  ];
  return <dl className={`parameters parameters--${type}`} aria-label="Slice parameters, read only">
    {params.map(({ label, value, point }) => <div key={label} style={{
      '--point': `${point}%`, '--angle': `${point * 2.6 - 130}deg`, '--arc': `${point * 2.6}deg`,
    } as CSSProperties}>
      <dt>{label}</dt>
      <span className="parameter-mark" aria-hidden="true"><i /></span>
      <dd>{value}</dd>
    </div>)}
  </dl>;
}

export type Playback = { position: number; playing: boolean; toggle: () => void; stop: () => void };

export function Transport({ position, playing, toggle, stop }: Playback) {
  return <div className="transport" aria-label="Simulated transport">
    <div className="transport-controls">
      <button type="button" className="play" onClick={toggle} aria-label={playing ? 'Pause simulation' : 'Play simulation'}>
        {playing ? <Pause size={17} fill="currentColor" /> : <Play size={17} fill="currentColor" />}
      </button>
      <button type="button" className="stop" onClick={stop} aria-label="Stop simulation"><Square size={11} fill="currentColor" /></button>
    </div>
    <div className="transport-state">
      <span className="mono">{playing ? 'Running' : 'Preview'}</span>
      <strong>{time(position / sample.metadata.sampleRate)} <span>/ {time(length / sample.metadata.sampleRate)} s</span></strong>
    </div>
  </div>;
}
