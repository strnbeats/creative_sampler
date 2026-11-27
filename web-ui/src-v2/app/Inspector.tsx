import { useId, useRef, useState, type CSSProperties, type PointerEvent, type KeyboardEvent } from 'react';
import type { SliceDto, SliceParameterId, SliceParametersDto } from '../bridge/protocol';
import { clampSliceParameter, sliceParameterRanges, type SliceParameterValues } from './sliceParameterQueue';

const clamp = (value: number) => Math.max(0, Math.min(1, value));
const seconds = (value: number) => value >= 1 ? `${value.toFixed(2)} s`
  : `${Number((value * 1000).toFixed(2))} ms`;
const envelopeScale = (value: number) => Math.log1p(value * 1000) / Math.log1p(10_000);

function dialPoint(angle: number, radius: number) {
  const radians = angle * Math.PI / 180;
  return [32 + Math.sin(radians) * radius, 32 - Math.cos(radians) * radius] as const;
}

function arc(from: number, to: number) {
  return `M${dialPoint(from, 28).join(' ')}A28 28 0 ${to - from > 180 ? 1 : 0} 1 ${dialPoint(to, 28).join(' ')}`;
}

function Readout({ parameter, label, value, normalized, bipolar = false, pending, onChange }: {
  parameter: SliceParameterId; label: string; value: string; normalized: number; bipolar?: boolean;
  pending: boolean; onChange: (parameter: SliceParameterId, value: number) => void;
}) {
  const angle = clamp(normalized) * 260 - 130;
  const anchor = bipolar ? 0 : -130;
  const range = sliceParameterRanges[parameter];
  const dragRef = useRef<{ pointerId: number; normalized: number; startY: number } | null>(null);
  const [dragging, setDragging] = useState(false);
  const timeParameter = parameter === 'attack' || parameter === 'decay' || parameter === 'release';
  const fromNormalized = (amount: number) => timeParameter
    ? (Math.expm1(clamp(amount) * Math.log1p(10_000)) / 1000)
    : range.min + clamp(amount) * (range.max - range.min);
  const emitNormalized = (amount: number) => {
    const raw = fromNormalized(amount);
    const stepped = Math.round(raw / range.step) * range.step;
    onChange(parameter, clampSliceParameter(parameter, Number(stepped.toFixed(4))));
  };
  const pointerDown = (event: PointerEvent<HTMLSpanElement>) => {
    if (event.button !== 0) return;
    event.preventDefault();
    dragRef.current = { pointerId: event.pointerId, normalized: clamp(normalized), startY: event.clientY };
    event.currentTarget.setPointerCapture(event.pointerId);
    setDragging(true);
  };
  const pointerMove = (event: PointerEvent<HTMLSpanElement>) => {
    const drag = dragRef.current;
    if (!drag || drag.pointerId !== event.pointerId) return;
    emitNormalized(drag.normalized + (drag.startY - event.clientY) / 150);
  };
  const endDrag = (event: PointerEvent<HTMLSpanElement>) => {
    if (dragRef.current?.pointerId !== event.pointerId) return;
    dragRef.current = null;
    setDragging(false);
    if (event.currentTarget.hasPointerCapture(event.pointerId)) event.currentTarget.releasePointerCapture(event.pointerId);
  };
  const keyDown = (event: KeyboardEvent<HTMLSpanElement>) => {
    const amount = event.shiftKey ? 10 : event.altKey ? .1 : 1;
    const delta = range.step * amount;
    if (event.key === 'ArrowUp' || event.key === 'ArrowRight') { event.preventDefault(); onChange(parameter, clampSliceParameter(parameter, Number((valueNumber + delta).toFixed(4)))); }
    else if (event.key === 'ArrowDown' || event.key === 'ArrowLeft') { event.preventDefault(); onChange(parameter, clampSliceParameter(parameter, Number((valueNumber - delta).toFixed(4)))); }
    else if (event.key === 'PageUp') { event.preventDefault(); onChange(parameter, clampSliceParameter(parameter, Number((valueNumber + range.step * 10).toFixed(4)))); }
    else if (event.key === 'PageDown') { event.preventDefault(); onChange(parameter, clampSliceParameter(parameter, Number((valueNumber - range.step * 10).toFixed(4)))); }
    else if (event.key === 'Home') { event.preventDefault(); onChange(parameter, range.min); }
    else if (event.key === 'End') { event.preventDefault(); onChange(parameter, range.max); }
  };
  const valueNumber = fromNormalized(normalized);
  return <div className={`dial${pending ? ' is-pending' : ''}${dragging ? ' is-dragging' : ''}`}>
    <dt className="dial-label">{label}</dt>
    <dd>
      <span className="dial-body" role="slider" tabIndex={0} aria-label={label}
        aria-valuemin={range.min} aria-valuemax={range.max} aria-valuenow={Number(valueNumber.toFixed(4))}
        aria-valuetext={value} onKeyDown={keyDown} onPointerDown={pointerDown}
        onPointerMove={pointerMove} onPointerUp={endDrag} onPointerCancel={endDrag}>
        <svg viewBox="0 0 64 64" className="dial-scale">
          <path className="dial-track" d={arc(-130, 130)} />
          {Array.from({ length: 11 }, (_, i) => {
            const major = i === 0 || i === 5 || i === 10;
            const theta = i * 26 - 130;
            const [x1, y1] = dialPoint(theta, major ? 30 : 30.8);
            const [x2, y2] = dialPoint(theta, 32);
            return <line key={i} className={major ? 'dial-tick major' : 'dial-tick'} x1={x1} y1={y1} x2={x2} y2={y2} />;
          })}
          {Math.abs(angle - anchor) > .1 && <path className="dial-value-arc" d={arc(Math.min(angle, anchor), Math.max(angle, anchor))} />}
        </svg>
        <span className="dial-cap" style={{ '--angle': `${angle}deg` } as CSSProperties} aria-hidden="true"><i /></span>
      </span>
      <span className="dial-value">{value}</span>
    </dd>
  </div>;
}

function Envelope({ values }: { values: SliceParametersDto['envelope'] }) {
  const id = useId();
  const { attackSeconds, decaySeconds, releaseSeconds, sustainLevel } = values;
  const plateau = Math.max(.12, (attackSeconds + decaySeconds + releaseSeconds) / 3);
  const total = attackSeconds + decaySeconds + releaseSeconds + plateau;
  const a = 6 + attackSeconds / total * 171;
  const d = 6 + (attackSeconds + decaySeconds) / total * 171;
  const r = 177 - releaseSeconds / total * 171;
  const s = 63 - clamp(sustainLevel) * 48;
  const path = `M6 63L${a} 15L${d} ${s}H${r}L177 63`;

  return <svg viewBox="0 0 184 73" className="envelope-graph" role="img" aria-label="Schematic amplitude envelope">
    <defs><linearGradient id={id} x2="0" y2="1">
      <stop stopColor="#b66546" stopOpacity=".24" /><stop offset="100%" stopColor="#b66546" stopOpacity=".025" />
    </linearGradient></defs>
    <path className="envelope-grid" d="M6 15H177M6 39H177M6 63H177" />
    <path d={`${path}Z`} fill={`url(#${id})`} />
    <path className="envelope-line" d={path} />
    {[[a, 15], [d, s], [r, s]].map(([x, y], i) => <circle key={i} cx={x} cy={y} r="2.35" />)}
  </svg>;
}

export function Inspector({ slice, pendingValues = {}, onParameterChange = () => undefined, error = null }: {
  slice: SliceDto | null; pendingValues?: SliceParameterValues;
  onParameterChange?: (parameter: SliceParameterId, value: number) => void; error?: string | null;
}) {
  if (!slice) return <section className="deck empty-deck" aria-label="Slice parameters">No slice parameters available</section>;
  const { volume: authoritativeVolume, pan: authoritativePan, pitchSemitones: authoritativePitch, envelope: authoritativeEnvelope } = slice.parameters;
  const volume = pendingValues.volume ?? authoritativeVolume;
  const pan = pendingValues.pan ?? authoritativePan;
  const pitchSemitones = pendingValues.pitch ?? authoritativePitch;
  const envelope = {
    attackSeconds: pendingValues.attack ?? authoritativeEnvelope.attackSeconds,
    decaySeconds: pendingValues.decay ?? authoritativeEnvelope.decaySeconds,
    sustainLevel: pendingValues.sustain ?? authoritativeEnvelope.sustainLevel,
    releaseSeconds: pendingValues.release ?? authoritativeEnvelope.releaseSeconds,
  };
  const { attackSeconds, decaySeconds, sustainLevel, releaseSeconds } = envelope;
  const panPercent = Math.round(Math.abs(pan) * 100);
  const pitch = Number(pitchSemitones.toFixed(2));

  return <section className="deck" aria-label="Slice parameters">
    <section className="voice-module">
      <div className="module-heading"><h2>Voice</h2><span>{error ? 'Not applied' : 'Per slice'}</span></div>
      <dl className="knob-row">
        <Readout parameter="volume" label="Volume" value={`${volume.toFixed(2)}×`} normalized={volume / 2} pending={pendingValues.volume !== undefined} onChange={onParameterChange} />
        <Readout parameter="pan" label="Pan" value={!panPercent ? 'Center' : `${panPercent} ${pan < 0 ? 'L' : 'R'}`} normalized={(pan + 1) / 2} bipolar pending={pendingValues.pan !== undefined} onChange={onParameterChange} />
        <Readout parameter="pitch" label="Pitch" value={`${pitch > 0 ? '+' : ''}${pitch} st`} normalized={(pitchSemitones + 24) / 48} bipolar pending={pendingValues.pitch !== undefined} onChange={onParameterChange} />
      </dl>
    </section>
    <section className="env-module">
      <h2>Amplitude envelope</h2>
      <div className="env-inner">
        <Envelope values={envelope} />
        <dl className="knob-row">
          <Readout parameter="attack" label="Attack" value={seconds(attackSeconds)} normalized={envelopeScale(attackSeconds)} pending={pendingValues.attack !== undefined} onChange={onParameterChange} />
          <Readout parameter="decay" label="Decay" value={seconds(decaySeconds)} normalized={envelopeScale(decaySeconds)} pending={pendingValues.decay !== undefined} onChange={onParameterChange} />
          <Readout parameter="sustain" label="Sustain" value={`${Math.round(sustainLevel * 100)}%`} normalized={sustainLevel} pending={pendingValues.sustain !== undefined} onChange={onParameterChange} />
          <Readout parameter="release" label="Release" value={seconds(releaseSeconds)} normalized={envelopeScale(releaseSeconds)} pending={pendingValues.release !== undefined} onChange={onParameterChange} />
        </dl>
      </div>
    </section>
    {error && <p className="parameter-status" role="status">Parameter change was not applied. {error}</p>}
  </section>;
}
