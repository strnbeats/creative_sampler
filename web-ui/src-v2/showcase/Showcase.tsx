import { useState } from 'react';
import { Button, IconButton } from '../components/Button';
import { RotaryControl } from '../components/RotaryControl';
import { InstrumentSurface, SubtleDivider } from '../components/Surface';
import { Label, Metadata, SectionTitle, Value } from '../components/Typography';
import { SliceIndicator, TransientMarker } from '../components/VisualizationPrimitives';
import './showcase.css';

const sliceData = [
  { id: 1, note: 'C3', left: 0, width: 17 },
  { id: 2, note: 'D♯3', left: 17, width: 18 },
  { id: 3, note: 'F3', left: 35, width: 21 },
  { id: 4, note: 'G♯3', left: 56, width: 16 },
  { id: 5, note: 'A♯3', left: 72, width: 15 },
  { id: 6, note: 'C4', left: 87, width: 13 },
];

const peaks = Array.from({ length: 128 }, (_, index) => {
  const x = index / 127;
  const body = Math.exp(-Math.pow((x - 0.19) / 0.11, 2)) * 0.64
    + Math.exp(-Math.pow((x - 0.48) / 0.15, 2)) * 0.92
    + Math.exp(-Math.pow((x - 0.76) / 0.1, 2)) * 0.74;
  const grain = 0.1 + 0.9 * Math.abs(Math.sin(index * 1.73) * Math.cos(index * 0.39));
  return Math.min(0.96, 0.035 + body * grain);
});

const transients = [7, 19, 29, 37, 45, 59, 68, 74, 82, 93];

function WaveformSpecimen({ selected, onSelect, playing }: { selected: number; onSelect: (id: number) => void; playing: boolean }) {
  return <div className={`cs-waveform${playing ? ' is-playing' : ''}`}>
    <div className="cs-waveform__notes" aria-hidden="true">
      {sliceData.map(slice => <span key={slice.id} style={{ left: `${slice.left}%`, width: `${slice.width}%` }}>{slice.note}</span>)}
    </div>
    <div className="cs-waveform__plot" role="img" aria-label="Demonstration waveform with six slices and transient positions">
      <span className="cs-waveform__centerline" />
      {sliceData.map(slice => <button key={slice.id} type="button"
        className={`cs-waveform__region${selected === slice.id ? ' is-selected' : ''}`}
        style={{ left: `${slice.left}%`, width: `${slice.width}%` }}
        onClick={() => onSelect(slice.id)} aria-label={`Select slice ${slice.id}, ${slice.note}`} aria-pressed={selected === slice.id} />)}
      {peaks.map((peak, index) => <span className="cs-waveform__peak" key={index}
        style={{ left: `${index / (peaks.length - 1) * 100}%`, height: `${peak * 82}%`, opacity: 0.45 + peak * 0.42 }} />)}
      {transients.map((position, index) => <TransientMarker key={position} position={`${position}%`} label={`Transient ${index + 1}`} />)}
      <span className="cs-waveform__playhead" aria-hidden="true" />
    </div>
    <div className="cs-waveform__ruler" aria-hidden="true"><span>0:00.000</span><i /><i /><i /><i /><i /><span>4.82 SEC</span></div>
    <div className="cs-waveform__slice-row" aria-label="Slice selection">
      {sliceData.map(slice => <SliceIndicator key={slice.id} index={slice.id} note={slice.note}
        selected={selected === slice.id} onSelect={() => onSelect(slice.id)} />)}
    </div>
  </div>;
}

export function Showcase() {
  const [selected, setSelected] = useState(3);
  const [playing, setPlaying] = useState(false);
  const [volume, setVolume] = useState(0.74);
  const [pan, setPan] = useState(-0.12);
  const [pitch, setPitch] = useState(0);

  return <div className="cs-showcase">
    <header className="cs-topline">
      <div className="cs-brand"><span className="cs-brand__mark">◉</span><span>CREATIVE<br />SAMPLER</span></div>
      <SubtleDivider vertical />
      <Metadata>UI FOUNDATION <span className="cs-topline__dot">/</span> COMPONENT STUDY</Metadata>
      <span className="cs-topline__spacer" />
      <Metadata className="cs-build-tag">ISOLATED SHOWCASE · NOT BRIDGE CONNECTED</Metadata>
    </header>

    <main className="cs-instrument">
      <section className="cs-sample-heading" aria-label="Demonstration sample information">
        <div className="cs-sample-heading__identity">
          <Label>ONE SOURCE, MANY VOICES</Label>
          <h1>Garden <em>Glass</em></h1>
          <Metadata>CHOPPED TEXTURE <span>·</span> 4.82 SEC <span>·</span> 48 kHz STEREO</Metadata>
        </div>
        <div className="cs-analysis-readout">
          <div><Label>TEMPO</Label><Value>148 <small>BPM</small></Value></div>
          <div><Label>KEY</Label><Value>G♯ <small>MIN</small></Value></div>
          <div><Label>ATTACKS</Label><Value>11</Value></div>
          <IconButton label="Sample actions" className="cs-more">···</IconButton>
        </div>
      </section>

      <section className="cs-source" aria-label="Waveform component specimen">
        <div className="cs-source__eyebrow"><Label>01 <span>—</span> SOURCE</Label><Metadata>SLICE {String(selected).padStart(2, '0')} <span>·</span> {sliceData[selected - 1]?.note} <span>·</span> 0.87 s</Metadata></div>
        <WaveformSpecimen selected={selected} onSelect={setSelected} playing={playing} />
      </section>

      <section className="cs-control-study" aria-label="Control component specimen">
        <div className="cs-control-study__lead">
          <Label>02 <span>—</span> SELECTED FRAGMENT</Label>
          <SectionTitle>Slice {String(selected).padStart(2, '0')}</SectionTitle>
          <Metadata>{sliceData[selected - 1]?.note} <span>·</span> MIDI {59 + selected}</Metadata>
          <div className="cs-state-row">
            <span className="cs-state-dot" />
            <Metadata>SELECTION FOLLOWS THE SOURCE</Metadata>
          </div>
        </div>

        <SubtleDivider vertical />

        <div className="cs-envelope">
          <div className="cs-group-heading"><Label>AMPLITUDE ENVELOPE</Label><Metadata>ADSR</Metadata></div>
          <svg viewBox="0 0 260 88" role="img" aria-label="ADSR envelope specimen">
            <path className="cs-envelope__guide" d="M4 72H256M4 16V72" />
            <path className="cs-envelope__curve" d="M8 70 L42 10 L103 32 L178 32 L252 70" />
            <circle cx="8" cy="70" r="3" /><circle cx="42" cy="10" r="3" /><circle cx="103" cy="32" r="3" /><circle cx="178" cy="32" r="3" /><circle cx="252" cy="70" r="3" />
          </svg>
          <div className="cs-envelope__labels"><span>A <b>5 ms</b></span><span>D <b>100 ms</b></span><span>S <b>100%</b></span><span>R <b>100 ms</b></span></div>
        </div>

        <SubtleDivider vertical />

        <div className="cs-controls">
          <div className="cs-group-heading"><Label>SLICE PLAYBACK</Label><Metadata>PER FRAGMENT</Metadata></div>
          <div className="cs-controls__rotaries">
            <RotaryControl label="Volume" min={0} max={2} step={0.01} value={volume} accent="mint"
              formatValue={value => `${Math.round(value * 100)}%`} onChange={setVolume} />
            <RotaryControl label="Pan" min={-1} max={1} step={0.01} value={pan} accent="violet"
              formatValue={value => Math.abs(value) < 0.01 ? 'CENTER' : `${value < 0 ? 'L' : 'R'} ${Math.round(Math.abs(value) * 100)}`}
              onChange={setPan} />
            <RotaryControl label="Pitch" min={-24} max={24} step={1} value={pitch} accent="amber"
              formatValue={value => `${value > 0 ? '+' : ''}${value} st`} onChange={setPitch} />
          </div>
        </div>
      </section>

      <footer className="cs-transport">
        <Button tone="transport" className={playing ? 'is-playing' : ''} aria-pressed={playing}
          onClick={() => setPlaying(value => !value)}>{playing ? 'Ⅱ' : '▶'}</Button>
        <Button tone="quiet" onClick={() => setPlaying(false)}>STOP</Button>
        <Value className="cs-transport__time">00:01.84 <span>/ 04.82 SEC</span></Value>
        <div className={`cs-transport__track${playing ? ' is-playing' : ''}`} aria-hidden="true"><span /></div>
        <Metadata className="cs-transport__output">MASTER <b>0.0</b> dB</Metadata>
      </footer>
    </main>

    <InstrumentSurface level="base" className="cs-component-legend" aria-label="Компонентные состояния">
      <span><Label>BUTTON STATES</Label><Button tone="accent">IMPORT SAMPLE</Button><Button tone="quiet" disabled>UNAVAILABLE</Button></span>
      <span><Label>SLICE STATES</Label><SliceIndicator index={1} note="C3" /><SliceIndicator index={3} note="F3" selected /></span>
      <span><Label>ACCENTS</Label><i className="cs-legend-swatch cs-legend-swatch--mint" /><i className="cs-legend-swatch cs-legend-swatch--violet" /><i className="cs-legend-swatch cs-legend-swatch--amber" /><Metadata>WAVE / SELECTION / TRANSIENT</Metadata></span>
    </InstrumentSurface>
    <div className="cs-showcase-note"><Metadata>DEMONSTRATION VALUES · LOCAL INTERACTION ONLY · NO SAMPLE OR DSP CONNECTION</Metadata></div>
  </div>;
}
