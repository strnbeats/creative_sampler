import { useEffect, useState } from 'react';
import { createRoot } from 'react-dom/client';
import { AudioLines, ArrowDownRight, CornerDownRight, MoveRight } from 'lucide-react';
import type { DemoSlice } from '../../src-v2/demo/demoData';
import { Envelope, Parameters, SliceIdentity, Transport, type Playback } from './Controls';
import { formatSlice, length, sample, Signal, time, type Select } from './Signal';
import './styles.css';

const directions = [
  { id: 'lithic', name: 'Lithic' },
  { id: 'prism', name: 'Prism' },
  { id: 'fold', name: 'Fold' },
] as const;

type Direction = typeof directions[number]['id'];
type Study = { selected: DemoSlice; select: Select; playback: Playback };

function readDirection(): Direction {
  return directions.find(({ id }) => '#' + id === location.hash)?.id ?? 'lithic';
}

function Brand() {
  return <div className="brand"><AudioLines size={18} strokeWidth={2.4} aria-hidden="true" /> Creative Sampler</div>;
}

function Facts() {
  return <dl className="facts" aria-label="Sample analysis">
    <div><dt>Tempo</dt><dd>{sample.metadata.bpm} <small>BPM</small></dd></div>
    <div><dt>Key</dt><dd>G♯ minor</dd></div>
    <div><dt>Transients</dt><dd>{sample.transients.length}</dd></div>
  </dl>;
}

function SampleTitle() {
  return <div className="sample-title"><h1>{sample.metadata.name}</h1>
    <p>{sample.metadata.durationSeconds.toFixed(2)} s <i /> {sample.metadata.sampleRate / 1000} kHz <i /> Stereo</p>
  </div>;
}

function Lithic({ selected, select, playback }: Study) {
  return <main className="instrument lithic">
    <header className="lithic-header"><SampleTitle /><Facts /></header>
    <section className="lithic-stage" aria-label="Source waveform">
      <div className="stage-meta mono"><span>Source waveform</span><span>{sample.slices.length} slices</span></div>
      <Signal mode="etch" selectedId={selected.id} onSelect={select} position={playback.position} />
      <div className="time-ends mono"><span>0.00 s</span><span>{sample.metadata.durationSeconds.toFixed(2)} s</span></div>
    </section>
    <section className="lithic-deck" aria-label="Slice settings">
      <SliceIdentity slice={selected} /><Envelope slice={selected} /><Parameters slice={selected} type="dial" />
    </section>
    <footer className="lithic-footer"><Transport {...playback} /><span className="mono detail"><span className="attack-key" /> Transient</span></footer>
  </main>;
}

function Prism({ selected, select, playback }: Study) {
  return <main className="instrument prism">
    <section className="prism-stage" aria-label="Source waveform">
      <div className="prism-identity"><SampleTitle /><Facts /></div>
      <Signal mode="facet" selectedId={selected.id} onSelect={select} position={playback.position} />
      <div className="time-ends mono"><span>0.00 s</span><span>{sample.metadata.durationSeconds.toFixed(2)} s</span></div>
      <footer className="prism-footer"><Transport {...playback} /><span className="mono detail"><span className="attack-key" /> Transient</span></footer>
    </section>
    <aside className="prism-deck" aria-label="Slice settings">
      <SliceIdentity slice={selected} /><Parameters slice={selected} type="bar" /><Envelope slice={selected} />
    </aside>
  </main>;
}

const foldBoundary = sample.slices[3].endSample;

function Fold({ selected, select, playback }: Study) {
  return <main className="instrument fold">
    <aside className="fold-spine">
      <SampleTitle /><Facts /><Transport {...playback} />
    </aside>
    <div className="fold-work">
      <section className="fold-map" aria-label="Source waveform">
        <div className="fold-chapter">
          <div className="stage-meta mono"><span>0.00 s</span><MoveRight size={16} aria-hidden="true" /><span>{time(foldBoundary / sample.metadata.sampleRate)} s</span></div>
          <Signal mode="trace" selectedId={selected.id} onSelect={select} position={playback.position} end={foldBoundary} />
        </div>
        <div className="fold-chapter">
          <div className="stage-meta mono"><span><CornerDownRight size={14} aria-hidden="true" />{time(foldBoundary / sample.metadata.sampleRate)} s</span><MoveRight size={16} aria-hidden="true" /><span>{sample.metadata.durationSeconds.toFixed(2)} s</span></div>
          <Signal mode="trace" selectedId={selected.id} onSelect={select} position={playback.position} start={foldBoundary} />
        </div>
      </section>
      <section className="fold-deck" aria-label="Slice settings">
        <div className="fold-current mono">Slice {formatSlice(selected.index)} <ArrowDownRight size={14} aria-hidden="true" /> <b>{selected.name} · {selected.note}</b></div>
        <Envelope slice={selected} /><Parameters slice={selected} type="rail" />
      </section>
    </div>
  </main>;
}

function App() {
  const [direction, setDirection] = useState(readDirection);
  const [selectedId, setSelectedId] = useState(sample.initialSliceId);
  const [position, setPosition] = useState(sample.initialPositionSamples);
  const [playing, setPlaying] = useState(false);
  const selected = sample.slices.find(slice => slice.id === selectedId) ?? sample.slices[0];

  useEffect(() => {
    const update = () => setDirection(readDirection());
    window.addEventListener('hashchange', update);
    return () => window.removeEventListener('hashchange', update);
  }, []);

  useEffect(() => {
    if (!playing) return;
    let last = performance.now();
    let frame = 0;
    const tick = (now: number) => {
      const step = (now - last) * sample.metadata.sampleRate / 1000;
      last = now;
      setPosition(current => (current + step) % length);
      frame = requestAnimationFrame(tick);
    };
    frame = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(frame);
  }, [playing]);

  const props = {
    selected, select: setSelectedId,
    playback: { position, playing, toggle: () => setPlaying(value => !value), stop: () => { setPlaying(false); setPosition(0); } },
  };

  return <div className={`study study--${direction}`}>
    <header className="study-bar">
      <Brand />
      <nav aria-label="Art directions">{directions.map(({ id, name }) => <a key={id} href={`#${id}`}
        aria-current={direction === id ? 'page' : undefined}>{name}</a>)}</nav>
      <span className="study-tag mono">Visual study</span>
    </header>
    {direction === 'lithic' ? <Lithic {...props} /> : direction === 'prism' ? <Prism {...props} /> : <Fold {...props} />}
  </div>;
}

createRoot(document.getElementById('root')!).render(<App />);
