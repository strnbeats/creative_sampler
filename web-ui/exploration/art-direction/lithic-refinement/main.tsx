/// <reference types="vite/client" />
import { useEffect, useState } from 'react';
import { createRoot } from 'react-dom/client';
import { AudioLines, Pause, Play, Square } from 'lucide-react';
import { datasets, type DatasetId, type Parameters } from './data';
import { Controls } from './Controls';
import { Waveform } from './Waveform';
import type { AmplitudeMode } from './display';
import './styles.css';

const scenarios: [DatasetId, string][] = [
  ['sparse', 'Sparse'], ['stereo', 'Stereo'], ['slices', '28 slices'],
  ['dynamics', 'Dynamics'], ['comparison', 'Garden Glass'],
];

function Scenario({ id, mode, setMode }: { id: DatasetId; mode: AmplitudeMode; setMode: (mode: AmplitudeMode) => void }) {
  const dataset = datasets[id];
  const [selected, select] = useState(dataset.selected);
  const [position, setPosition] = useState(dataset.position);
  const [playing, setPlaying] = useState(false);
  const [changes, setChanges] = useState<Partial<Record<number, Parameters>>>({});
  const values = changes[selected] ?? dataset.slices[selected].params;

  useEffect(() => {
    if (!playing) return;
    let last = performance.now();
    let frame = 0;
    const tick = (now: number) => {
      const step = (now - last) / (dataset.duration * 1000);
      last = now;
      setPosition(previous => (previous + step) % 1);
      frame = requestAnimationFrame(tick);
    };
    frame = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(frame);
  }, [playing, dataset.duration]);

  const format = (fraction: number) => (fraction * dataset.duration).toFixed(2);

  return <main className="instrument">
    <header className="sample-header">
      <div className="sample-ident">
        <p>{dataset.rate / 1000} kHz <i /> {dataset.channels.length === 2 || id === 'comparison' ? 'Stereo' : 'Mono'}</p>
        <h1 title={dataset.name}>{dataset.name}</h1>
      </div>
      <dl className="analysis" aria-label="Sample analysis">
        <div><dt>Tempo</dt><dd>{dataset.bpm ?? '—'} {dataset.bpm && <small>BPM</small>}</dd></div>
        <div><dt>Key</dt><dd>{dataset.key ?? '—'}</dd></div>
      </dl>
    </header>

    <Waveform dataset={dataset} selected={selected} select={select} position={position} playing={playing} mode={mode} setMode={setMode} />
    <Controls key={selected} values={values} change={(parameter, value) => setChanges(previous => ({
      ...previous, [selected]: { ...(previous[selected] ?? dataset.slices[selected].params), [parameter]: value },
    }))} />

    <footer className="status">
      <div className="transport" aria-label="Visual playback simulation">
        <button className="transport-play" type="button" onClick={() => setPlaying(!playing)}
          aria-label={playing ? 'Pause simulation' : 'Play simulation'}>
          {playing ? <Pause size={15} fill="currentColor" /> : <Play size={15} fill="currentColor" />}
        </button>
        <button type="button" onClick={() => { setPlaying(false); setPosition(0); }} aria-label="Stop simulation">
          <Square size={10} fill="currentColor" />
        </button>
        <span className="timecode"><strong>{format(position)}</strong> / {format(1)} s</span>
      </div>
      <span className="demo-state"><i className={playing ? 'running' : ''} /> {playing ? 'Running' : 'Paused'} · Visual demo</span>
    </footer>
  </main>;
}

function readScenario(): DatasetId {
  const hash = window.location.hash.slice(1);
  return scenarios.find(([id]) => id === hash)?.[0] ?? 'stereo';
}

function App() {
  const [scenario, setScenario] = useState(readScenario);
  const [mode, setMode] = useState<AmplitudeMode>(() => new URLSearchParams(location.search).get('amplitude') === 'detail' ? 'detail' : 'linear');
  useEffect(() => {
    const read = () => setScenario(readScenario());
    window.addEventListener('hashchange', read);
    return () => window.removeEventListener('hashchange', read);
  }, []);

  return <div className="lithic">
    <header className="rail">
      <div className="brand"><AudioLines size={20} aria-hidden="true" /> <span>Creative Sampler</span></div>
      <nav aria-label="Stress-test samples">{scenarios.map(([id, label]) =>
        <a key={id} href={`#${id}`} aria-current={scenario === id ? 'page' : undefined}>{label}</a>)}</nav>
      <span className="wordmark">Lithic</span>
    </header>
    <Scenario key={scenario} id={scenario} mode={mode} setMode={setMode} />
  </div>;
}

createRoot(document.getElementById('root')!).render(<App />);
