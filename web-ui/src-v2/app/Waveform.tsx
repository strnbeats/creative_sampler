import { useEffect, useRef, type CSSProperties } from 'react';
import { Metadata, Label } from '../components/Typography';
import { TransientMarker } from '../components/VisualizationPrimitives';
import type { ViewSample, ViewSlice, ViewTransient } from '../bridge/protocol';

type WaveformPeak = { min: number; max: number };

export type WaveformProps = {
  metadata: ViewSample | null;
  peaks: readonly (readonly WaveformPeak[])[];
  sampleCount: number;
  slices: readonly ViewSlice[];
  transients: readonly ViewTransient[];
  selectedSliceId: string | null;
  playheadPositionSamples: number;
  playing: boolean;
  onSelectSlice: (sliceId: string) => void;
};

function formatTime(seconds: number) {
  const minutes = Math.floor(seconds / 60);
  const remaining = (seconds - minutes * 60).toFixed(2).padStart(5, '0');
  return `${String(minutes).padStart(2, '0')}:${remaining}`;
}

export function Waveform({ metadata, peaks, sampleCount, slices, transients, selectedSliceId,
  playheadPositionSamples, playing, onSelectSlice }: WaveformProps) {
  const plotRef = useRef<HTMLDivElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const selected = slices.find(slice => slice.id === selectedSliceId) ?? null;
  const selectedDuration = metadata && selected
    ? (selected.endSample - selected.startSample) / metadata.sampleRate : 0;
  const playheadPercent = sampleCount > 0 ? Math.min(1, Math.max(0, playheadPositionSamples / sampleCount)) : 0;

  useEffect(() => {
    const plot = plotRef.current;
    const canvas = canvasRef.current;
    if (!plot || !canvas || peaks.length === 0) return;
    const context = canvas.getContext('2d');
    if (!context) return;

    const draw = () => {
      const { width, height } = plot.getBoundingClientRect();
      if (width <= 0 || height <= 0) return;
      const pixelRatio = Math.min(window.devicePixelRatio || 1, 2);
      canvas.width = Math.round(width * pixelRatio);
      canvas.height = Math.round(height * pixelRatio);
      context.setTransform(pixelRatio, 0, 0, pixelRatio, 0, 0);
      context.clearRect(0, 0, width, height);

      const styles = getComputedStyle(plot);
      const gradient = context.createLinearGradient(0, 0, 0, height);
      gradient.addColorStop(0, styles.getPropertyValue('--cs-color-wave-high').trim());
      gradient.addColorStop(1, styles.getPropertyValue('--cs-color-wave-low').trim());
      const channelCount = peaks.length;
      const laneHeight = height / channelCount;
      const centerOffset = channelCount === 1 ? height / 2 : laneHeight / 2;
      const amplitudeScale = channelCount === 1 ? height * 0.46 : laneHeight * 0.44;
      context.lineCap = 'round';
      context.strokeStyle = gradient;
      peaks.forEach((channel, channelIndex) => {
        if (channel.length === 0) return;
        const stepWidth = width / channel.length;
        context.lineWidth = Math.max(0.8, Math.min(1.35, stepWidth * 0.42));
        const center = channelCount === 1 ? centerOffset : channelIndex * laneHeight + centerOffset;
        channel.forEach((peak, index) => {
          const x = (index + 0.5) * stepWidth;
          const top = center - Math.max(0, peak.max) * amplitudeScale;
          const bottom = center - Math.min(0, peak.min) * amplitudeScale;
          const strength = Math.min(1, Math.max(Math.abs(peak.min), Math.abs(peak.max)));
          context.globalAlpha = 0.48 + strength * 0.42;
          context.beginPath();
          context.moveTo(x, top);
          context.lineTo(x, bottom);
          context.stroke();
        });
      });
      context.globalAlpha = 1;
      context.strokeStyle = styles.getPropertyValue('--cs-color-border').trim();
      context.lineWidth = 1;
      for (let channel = 0; channel < channelCount; channel += 1) {
        const center = channelCount === 1 ? centerOffset : channel * laneHeight + centerOffset;
        context.beginPath();
        context.moveTo(0, center + 0.5);
        context.lineTo(width, center + 0.5);
        context.stroke();
      }
    };

    const observer = new ResizeObserver(draw);
    observer.observe(plot);
    draw();
    return () => observer.disconnect();
  }, [peaks]);

  return <section className={`cs-main-waveform${playing ? ' is-playing' : ''}${transients.length > 24 ? ' is-dense' : ''}`} aria-label="Sample waveform">
    <div className="cs-waveform-heading">
      <Label>01 <span>—</span> SOURCE</Label>
      <Metadata>{selected
        ? `SLICE ${String(selected.index).padStart(2, '0')} · ${selected.note} · ${selectedDuration.toFixed(2)} s`
        : 'NO SLICE SELECTED'}</Metadata>
    </div>

    {metadata === null ? <div className="cs-waveform-empty">
      <div className="cs-waveform-empty__mark" aria-hidden="true">⌁</div>
      <Label>NO SAMPLE IN THE SOURCE</Label>
      <p>Choose a demonstration state above to explore the waveform layout.</p>
    </div> : <>
      <div className="cs-waveform-note-ruler" aria-hidden="true">
        {slices.map(slice => {
          const start = sampleCount > 0 ? slice.startSample / sampleCount * 100 : 0;
          const width = sampleCount > 0 ? (slice.endSample - slice.startSample) / sampleCount * 100 : 0;
          const showNote = slices.length <= 8 ? width >= 5.5 : false;
          return showNote ? <span key={slice.id} style={{ left: `${start}%`, width: `${width}%` }}>{slice.note}</span> : null;
        })}
      </div>

      <div className="cs-waveform-plot" ref={plotRef}>
        <canvas ref={canvasRef} className="cs-waveform-canvas" role="img"
          aria-label={`${metadata.name}, waveform amplitude`} />
        {slices.map(slice => {
          const left = sampleCount > 0 ? slice.startSample / sampleCount * 100 : 0;
          const width = sampleCount > 0 ? (slice.endSample - slice.startSample) / sampleCount * 100 : 0;
          const showNumber = width >= (slices.length > 8 ? 3.8 : 5.5);
          const selectedState = slice.id === selectedSliceId;
          return <button key={slice.id} type="button"
            className={`cs-slice-region${selectedState ? ' is-selected' : ''}`}
            style={{ left: `${left}%`, width: `${width}%` } as CSSProperties}
            onClick={() => onSelectSlice(slice.id)} aria-pressed={selectedState}
            aria-label={`Выбрать слайс ${slice.index}, ${slice.name}, ${slice.note}`}>
            {showNumber && <span>{String(slice.index).padStart(2, '0')}</span>}
          </button>;
        })}
        {transients.map(event => <TransientMarker key={event.id}
          position={`${sampleCount > 0 ? event.samplePosition / sampleCount * 100 : 0}%`}
          strength={event.strength}
          label={`Транзиент, сила ${Math.round(event.strength * 100)}%`} />)}
        <span className="cs-playhead" style={{ left: `${playheadPercent * 100}%` } as CSSProperties} aria-hidden="true">
          <i />
        </span>
      </div>

      <div className="cs-time-ruler" aria-label="Временная шкала">
        <span>{formatTime(0)}</span>
        <i /><i /><i /><i /><i />
        <span>{formatTime(metadata.durationSeconds)}</span>
      </div>

      <div className={`cs-slice-sequence${slices.length > 8 ? ' is-dense' : ''}`} aria-label="Последовательность слайсов">
        {slices.map(slice => <button key={slice.id} type="button"
          className={`cs-sequence-item${slice.id === selectedSliceId ? ' is-selected' : ''}`}
          onClick={() => onSelectSlice(slice.id)} aria-pressed={slice.id === selectedSliceId}
          aria-label={`Выбрать слайс ${slice.index}, ${slice.name}, ${slice.note}`}>
          <span>{String(slice.index).padStart(2, '0')}</span>
          {slices.length <= 8 && <b>{slice.note}</b>}
        </button>)}
      </div>
    </>}
  </section>;
}
