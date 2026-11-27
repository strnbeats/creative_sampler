import { useCallback, useLayoutEffect, useMemo, useRef, useState, type MouseEvent, type PointerEvent } from 'react';
import { ChevronLeft, ChevronRight } from 'lucide-react';
import type { NativeSnapshot, SampleMetadataDto, SliceDto, SliceId, WaveformDto } from '../bridge/protocol';
import { CopperSignal, type AmplitudeMode } from './CopperSignal';
import { SliceIndex, sliceNumber } from './SliceIndex';
import { hitSlice, minDirectWidth, sliceTime } from './sliceInteraction';
import { groupTransients, TransientLane, type Transients } from './TransientLane';

export type { AmplitudeMode } from './CopperSignal';

const pitchNames = ['C', 'C♯', 'D', 'D♯', 'E', 'F', 'F♯', 'G', 'G♯', 'A', 'A♯', 'B'];
const noteName = (note: number) => `${pitchNames[note % 12]}${Math.floor(note / 12) - 1}`;
const pct = (value: number) => `${value * 100}%`;

export function Waveform({ sample, waveform, slices, selected, select, transients, transport, mode, setMode, waveformError }: {
  sample: SampleMetadataDto; waveform: WaveformDto | null; slices: readonly SliceDto[];
  selected: SliceDto | null; select: (id: SliceId) => void;
  transients: Transients | null; transport: NativeSnapshot['sampleTransport'];
  mode: AmplitudeMode; setMode: (mode: AmplitudeMode) => void; waveformError: string | null;
}) {
  const plotRef = useRef<HTMLDivElement>(null);
  const plateRef = useRef<HTMLElement>(null);
  const pressed = useRef<{ id: SliceId; x: number; y: number } | null>(null);
  const [width, setWidth] = useState(0);
  const [hovered, setHovered] = useState<SliceId | null>(null);
  const [focused, setFocused] = useState<SliceId | null>(null);
  const totalFrames = Math.max(sample.sampleCount, 1);
  const stereo = sample.channelCount === 2;
  const intervals = useMemo(() => slices.map(slice => ({
    start: slice.startSample / totalFrames,
    end: slice.endSample / totalFrames,
    pixels: (slice.endSample - slice.startSample) / totalFrames * width,
  })), [slices, totalFrames, width]);
  const showIndex = slices.length > 1 || intervals.some(interval => interval.pixels < minDirectWidth);
  const indexFocus = showIndex && slices.some(slice => slice.id === focused) ? focused : null;
  const linked = hovered ?? indexFocus;
  const onIndexFocus = useCallback((id: SliceId | null) => {
    setFocused(id);
    if (id !== null) setHovered(null);
  }, []);
  const marks = useMemo(() => groupTransients(transients, sample, width), [transients, sample, width]);
  const grouped = marks.some(mark => mark.count > 1);
  const divisions = width < 640 ? 4 : 8;
  const selectedIndex = selected ? slices.findIndex(slice => slice.id === selected.id) : -1;
  const duration = selected && sample.sampleRate > 0 ? (selected.endSample - selected.startSample) / sample.sampleRate : 0;

  const position = transport && Number.isFinite(transport.positionSamples)
    ? Math.min(1, Math.max(0, transport.positionSamples / totalFrames)) : null;
  const hasPeaks = waveform?.channels.some(channel => channel.length > 0) === true;

  useLayoutEffect(() => {
    const plot = plotRef.current;
    if (!plot) return;
    setWidth(plot.getBoundingClientRect().width);
    const observer = new ResizeObserver(([entry]) => setWidth(entry.contentRect.width));
    observer.observe(plot);
    return () => observer.disconnect();
  }, []);

  function eventHit(event: MouseEvent<HTMLDivElement>) {
    const rect = event.currentTarget.getBoundingClientRect();
    return hitSlice(slices, totalFrames, event.clientX - rect.left, rect.width);
  }

  function onPlotPointerDown(event: PointerEvent<HTMLDivElement>) {
    const hit = event.button === 0 ? eventHit(event) : null;
    pressed.current = hit ? { id: hit.id, x: event.clientX, y: event.clientY } : null;
  }

  function onPlotClick(event: MouseEvent<HTMLDivElement>) {
    const down = pressed.current;
    pressed.current = null;
    const hit = eventHit(event);
    if (!down || !hit || down.id !== hit.id || Math.hypot(event.clientX - down.x, event.clientY - down.y) > 5) return;
    if (hit.action === 'select') select(hit.id);
    else {
      const index = slices.findIndex(slice => slice.id === hit.id);
      plateRef.current?.querySelector<HTMLButtonElement>(`[data-slice-index="${index}"]`)?.focus();
    }
  }

  return <section className="plate" aria-label="Sample waveform" ref={plateRef}>
    <div className="plate-head">
      <span className="plate-label"><i className="copper-notch" />Source waveform</span>
      <div className="plate-tools">
        <span className="plate-summary">{slices.length} {slices.length === 1 ? 'slice' : 'slices'}</span>
        <div className="amplitude-switch" role="group" aria-label="Waveform amplitude display">
          <button type="button" disabled={!hasPeaks} aria-pressed={mode === 'linear'}
            onClick={() => setMode('linear')} title="Linear amplitude">Linear</button>
          <button type="button" disabled={!hasPeaks} aria-pressed={mode === 'detail'}
            onClick={() => setMode('detail')} title="Visual boost for quiet peaks (power 0.8)">Detail</button>
        </div>
      </div>
    </div>

    <div className="signal">
      <div className={`channel-legend${stereo ? ' is-stereo' : ''}`} aria-hidden="true">
        {stereo ? <><span>L</span><span>R</span></> : <span>M</span>}
      </div>
      <div className="plot" ref={plotRef} aria-label={stereo ? 'Left and right channel peaks' : 'Sample peak envelope'}>
        <CopperSignal waveform={waveform} stereo={stereo} mode={mode} divisions={divisions}
          start={selected ? selected.startSample / totalFrames : 0}
          end={selected ? selected.endSample / totalFrames : 0} />
        <div className="cut-regions" aria-label="Slice ranges" onPointerDown={onPlotPointerDown}
          onPointerCancel={() => { pressed.current = null; }} onClick={onPlotClick}>
          {slices.map((slice, index) => {
            const interval = intervals[index];
            const active = selected?.id === slice.id;
            const className = `cut-region${active ? ' is-selected' : ''}${linked === slice.id ? ' is-linked' : ''}`;
            const label = `Slice ${sliceNumber(index)} · ${noteName(slice.midiNote)}`;
            const props = {
              style: { left: pct(interval.start), width: pct(interval.end - interval.start) },
              onPointerEnter: () => setHovered(slice.id), onPointerLeave: () => setHovered(null),
            };
            if (interval.pixels < minDirectWidth) return <div key={slice.id} {...props} className={`${className} is-narrow`}
              title={`${label} · Use slice index`} />;
            return <button key={slice.id} {...props} className={className}
              type="button" tabIndex={showIndex ? -1 : 0} aria-label={label} aria-pressed={active} title={label}
              onClick={event => { if (event.detail === 0) { event.stopPropagation(); select(slice.id); } }}>
              <span>{sliceNumber(index)}{interval.pixels >= 74 && <small>{noteName(slice.midiNote)}</small>}</span>
            </button>;
          })}
        </div>
        {!hasPeaks && <span className="wave-status" role="status">{waveformError ?? 'Waiting for waveform…'}</span>}
        <TransientLane marks={marks} duration={sample.durationSeconds} />
        {position !== null && <div className={`playhead${transport?.status === 'playing' ? ' is-playing' : ''}`}
          style={{ left: pct(position) }} aria-hidden="true" />}
      </div>
    </div>
    <div className="scale" aria-label="Time in seconds">
      {Array.from({ length: divisions + 1 }, (_, index) => <span key={index} style={{ left: pct(index / divisions) }}>
        {(index / divisions * sample.durationSeconds).toFixed(2)}{index === divisions ? ' s' : ''}
      </span>)}
    </div>
    {showIndex && <SliceIndex slices={slices} selectedId={selected?.id} select={select}
      linked={linked} focused={indexFocus} onHover={setHovered} onFocusChange={onIndexFocus} />}
    <div className="plate-selection" aria-label="Selected range">
      {selected ? <div className="selection-context">
        <div className="selection-arrows" role="group" aria-label="Step through slices">
          <button type="button" aria-label="Previous slice" disabled={selectedIndex <= 0}
            onClick={() => select(slices[selectedIndex - 1].id)}><ChevronLeft size={14} /></button>
          <button type="button" aria-label="Next slice" disabled={selectedIndex >= slices.length - 1}
            onClick={() => select(slices[selectedIndex + 1].id)}><ChevronRight size={14} /></button>
        </div>
        <span className="selection-name" aria-live="polite"><b>Slice {sliceNumber(selectedIndex)}</b><i />{noteName(selected.midiNote)}</span>
        <span className="selection-time" title={`Frames ${selected.startSample}–${selected.endSample} (exclusive end)`}>
          {sliceTime(selected.startSample, sample.sampleRate)}–{sliceTime(selected.endSample, sample.sampleRate)} s
          {duration < .1 && <span className="micro-length"> · {(duration * 1000).toFixed(duration < .001 ? 2 : 0)} ms</span>}
        </span>
      </div> : <span>No slice selected</span>}
      {transients && <span className="transient-readout"
        title={grouped ? 'Double marks show groups. Hover a mark for its count and time.' : undefined}>
        {grouped && <i className="cluster-key" aria-hidden="true" />}
        {transients.totalEventCount} {transients.totalEventCount === 1 ? 'transient' : 'transients'}
        {(grouped || transients.truncated) && ` / ${marks.length} ${marks.length === 1 ? 'mark' : 'marks'}`}
      </span>}
    </div>
    {showIndex && <p className="slice-index-help" id="slice-index-help">
      {indexFocus ? 'Arrow keys move focus · Enter confirms' : 'Use the index for short slices and cuts'}
    </p>}
  </section>;
}
