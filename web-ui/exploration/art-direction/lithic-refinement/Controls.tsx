import { useId, useRef, type CSSProperties, type KeyboardEvent, type PointerEvent } from 'react';
import type { Parameters } from './data';

type Parameter = keyof Parameters;
type Definition = { id: Parameter; label: string; min: number; max: number; step: number; bipolar?: boolean; format: (value: number) => string };

const milliseconds = (value: number) => `${value} ms`;
const voice: Definition[] = [
  { id: 'volume', label: 'Volume', min: 0, max: 2, step: .01, format: v => `${v.toFixed(2)}×` },
  { id: 'pan', label: 'Pan', min: -1, max: 1, step: .05, bipolar: true, format: v => v === 0 ? 'Center' : `${Math.round(Math.abs(v) * 100)} ${v < 0 ? 'L' : 'R'}` },
  { id: 'pitch', label: 'Pitch', min: -24, max: 24, step: 1, bipolar: true, format: v => `${v > 0 ? '+' : ''}${v} st` },
];
const envelope: Definition[] = [
  { id: 'attack', label: 'Attack', min: 0, max: 250, step: 1, format: milliseconds },
  { id: 'decay', label: 'Decay', min: 0, max: 600, step: 1, format: milliseconds },
  { id: 'sustain', label: 'Sustain', min: 0, max: 1, step: .01, format: v => `${Math.round(v * 100)}%` },
  { id: 'release', label: 'Release', min: 0, max: 600, step: 1, format: milliseconds },
];

function dialPoint(angle: number, radius: number) {
  const radians = angle * Math.PI / 180;
  return [32 + Math.sin(radians) * radius, 32 - Math.cos(radians) * radius] as const;
}

function arc(from: number, to: number) {
  const a = dialPoint(from, 28);
  const b = dialPoint(to, 28);
  return `M${a.join(' ')}A28 28 0 ${to - from > 180 ? 1 : 0} 1 ${b.join(' ')}`;
}

function Dial({ definition, value, change }: { definition: Definition; value: number; change: (value: number) => void }) {
  const { label, min, max, step, bipolar, format } = definition;
  const angle = (value - min) / (max - min) * 260 - 130;
  const valueText = format(value);
  const drag = useRef<{ y: number; value: number } | null>(null);
  const update = (raw: number) => change(Number(Math.min(max, Math.max(min, Math.round(raw / step) * step)).toFixed(3)));

  function onKey(event: KeyboardEvent<HTMLDivElement>) {
    const next: Record<string, number> = {
      ArrowUp: value + step, ArrowRight: value + step, ArrowDown: value - step, ArrowLeft: value - step,
      Home: min, End: max, PageUp: value + step * 10, PageDown: value - step * 10,
    };
    if (!Object.hasOwn(next, event.key)) return;
    event.preventDefault();
    update(next[event.key]);
  }

  function onPointerDown(event: PointerEvent<HTMLDivElement>) {
    if (event.button !== 0) return;
    event.currentTarget.focus();
    event.currentTarget.setPointerCapture(event.pointerId);
    drag.current = { y: event.clientY, value };
  }

  const anchor = bipolar ? 0 : -130;
  return <div className="dial" role="slider" tabIndex={0} aria-label={label}
    aria-valuemin={min} aria-valuemax={max} aria-valuenow={value} aria-valuetext={valueText}
    onKeyDown={onKey} onPointerDown={onPointerDown}
    onPointerMove={event => {
      if (drag.current) update(drag.current.value + (drag.current.y - event.clientY) * (max - min) / 180);
    }}
    onLostPointerCapture={() => { drag.current = null; }}>
    <span className="dial-label">{label}</span>
    <span className="dial-body" aria-hidden="true">
      <svg viewBox="0 0 64 64" className="dial-scale">
        <path className="dial-track" d={arc(-130, 130)} />
        {Array.from({ length: 11 }, (_, i) => {
          const major = i === 0 || i === 5 || i === 10;
          const theta = i * 26 - 130;
          return <line key={i} className={major ? 'dial-tick major' : 'dial-tick'}
            x1={dialPoint(theta, major ? 30 : 30.8)[0]} y1={dialPoint(theta, major ? 30 : 30.8)[1]}
            x2={dialPoint(theta, 32)[0]} y2={dialPoint(theta, 32)[1]} />;
        })}
        {Math.abs(angle - anchor) > .1 && <path className="dial-value-arc" d={arc(Math.min(angle, anchor), Math.max(angle, anchor))} />}
      </svg>
      <span className="dial-cap" style={{ '--angle': `${angle}deg` } as CSSProperties}><i /></span>
    </span>
    <span className="dial-value">{valueText}</span>
  </div>;
}

function Envelope({ values }: { values: Parameters }) {
  const id = useId();
  const a = 14 + Math.sqrt(values.attack / 250) * 39;
  const d = a + 18 + Math.sqrt(values.decay / 600) * 38;
  const s = 63 - values.sustain * 48;
  const r = 166 - Math.sqrt(values.release / 600) * 44;
  const path = `M6 63L${a} 15L${d} ${s}H${r}L177 63`;
  return <svg viewBox="0 0 184 73" className="envelope-graph" aria-label="Schematic amplitude envelope" role="img">
    <defs><linearGradient id={id} x2="0" y2="1">
      <stop stopColor="#b66546" stopOpacity=".24" /><stop offset="100%" stopColor="#b66546" stopOpacity=".025" />
    </linearGradient></defs>
    <path className="envelope-grid" d="M6 15H177M6 39H177M6 63H177" />
    <path d={`${path}Z`} fill={`url(#${id})`} />
    <path className="envelope-line" d={path} />
    {[[a, 15], [d, s], [r, s]].map(([x, y], i) => <circle key={i} cx={x} cy={y} r="2.35" />)}
  </svg>;
}

export function Controls({ values, change }: { values: Parameters; change: (id: Parameter, value: number) => void }) {
  const dial = (def: Definition) => <Dial key={def.id} definition={def} value={values[def.id]}
    change={value => change(def.id, value)} />;
  return <section className="deck" aria-label="Demonstration parameters">
    <section className="voice-module">
      <h2>Voice</h2>
      <div className="knob-row">{voice.map(dial)}</div>
    </section>
    <section className="env-module">
      <h2>Amplitude envelope</h2>
      <div className="env-inner"><Envelope values={values} /><div className="knob-row">{envelope.map(dial)}</div></div>
    </section>
  </section>;
}
