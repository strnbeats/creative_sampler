import React, { useCallback, useEffect, useRef, useState } from 'react';
import { createRoot } from 'react-dom/client';
import { callNative } from './nativeBridge';
import './style.css';

type Slice = { id: number; start: number; end: number; note: number; volume: number; pan: number; pitch: number; attack: number; decay: number; sustain: number; release: number };
type State = { sampleLoaded: boolean; status: string; playing: boolean; file: string; duration: number; sampleRate: number; channels: number; slices: Slice[]; peaks: number[]; selectedSliceId: number; analysisReady: boolean; bpm: number | null; key: string | null; transientCount: number | null };
const empty: State = { sampleLoaded: false, status: 'Choose WAV or AIFF (CAF on macOS) to begin.', playing: false, file: 'DROP A SAMPLE TO BEGIN', duration: 0, sampleRate: 0, channels: 0, slices: [], peaks: [], selectedSliceId: 0, analysisReady: false, bpm: null, key: null, transientCount: null };
function App() {
  const [state, setState] = useState(empty), [selected, setSelected] = useState(0);
  const requestSerial = useRef(0);
  const acceptState = useCallback((next: Partial<State> | undefined, serial: number) => {
    if (!next || serial !== requestSerial.current) return;
    const current = { ...empty, ...next, slices: next.slices ?? [], peaks: next.peaks ?? [] };
    setState(current);
    setSelected(current.selectedSliceId || (current.slices[0]?.id ?? 0));
  }, []);
  const invoke = useCallback(async (name: string, ...params: unknown[]) => {
    const serial = ++requestSerial.current;
    acceptState(await callNative<Partial<State>>(name, ...params), serial);
  }, [acceptState]);
  const refresh = useCallback(async () => {
    const serial = ++requestSerial.current;
    acceptState(await callNative<Partial<State>>('getState'), serial);
  }, [acceptState]);
  useEffect(() => { void refresh(); const timer = window.setInterval(() => void refresh(), 700); return () => clearInterval(timer); }, [refresh]);
  const slice = state.slices.find(s => s.id === selected);
  const edit = async (key: string, value: number) => { if (slice) await invoke('editParameter', slice.id, key, value); };
  const choose = async (id: number) => { setSelected(id); await invoke('selectSlice', id); };
  return <main><header><b className="logo">◉ <span>CREATIVE<br/>SAMPLER</span></b><div><small>CURRENT SAMPLE</small><strong>{state.file || 'DROP A SAMPLE TO BEGIN'}</strong><small>{state.duration.toFixed(2)} SEC　·　{state.sampleRate ? `${(state.sampleRate / 1000).toFixed(1)} kHz` : '— kHz'}　·　{state.channels || '—'} CH</small></div><button className="import" onClick={() => void invoke('importSample')}>＋ IMPORT SAMPLE</button></header>
  <nav><button className="on">≋<small>SAMPLE</small></button><button>◉<small>SLICES</small></button><button>✧<small>ANALYSIS</small></button></nav>
  <section><div className="title"><div><small>SAMPLE / EDITOR</small><h1>{slice ? `Slice ${String(state.slices.indexOf(slice) + 1).padStart(2, '0')}` : 'Sample workspace'}</h1></div><div><button onClick={() => void invoke('splitSlice')}>＋ SPLIT</button><button onClick={() => void invoke('deleteSlice')}>DELETE</button><button onClick={() => void invoke('resetSlices')}>RESET</button></div></div><article><div className="label"><span>● SOURCE WAVEFORM</span><span>{state.slices.length} SLICES　·　{state.duration.toFixed(2)} s</span></div><div className="wave">{state.peaks.map((v, i) => <span className="peak" key={i} style={{ left: `${100 * (i + 0.5) / state.peaks.length}%`, height: `${Math.max(2, v * 92)}%` }} />)}{state.slices.map((x, i) => <button className={`marker ${selected === x.id ? 'selected' : ''}`} key={x.id} style={{ left: `${state.duration ? 100 * x.start / (state.duration * state.sampleRate) : 0}%`, width: `${state.duration ? Math.max(1, 100 * (x.end - x.start) / (state.duration * state.sampleRate)) : 1}%` }} onClick={() => void choose(x.id)}>{String(i + 1).padStart(2, '0')}</button>)}{!state.sampleLoaded && <div className="empty">IMPORT OR DROP AN AUDIO SAMPLE</div>}</div><div className="hint"><span>00:00.000</span><span>SELECT SLICE　·　USE SPLIT TO CREATE</span><span>{state.duration.toFixed(2)} s</span></div><div className="analysis"><div><small>TEMPO</small><b>{state.bpm?.toFixed(1) ?? '—'} <small>BPM</small></b></div><div><small>KEY</small><b>{state.key ?? '—'}</b></div><div><small>TRANSIENTS</small><b>{state.transientCount ?? '—'}</b></div><button>✧ ANALYZE SAMPLE</button></div></article><div className="status">{state.status}</div></section>
  <aside><small>SLICE PARAMETERS</small><h2>{slice ? `Slice ${String(state.slices.indexOf(slice) + 1).padStart(2, '0')}` : 'No slice selected'}</h2><div className="group"><small>AMPLITUDE ENVELOPE <span>ADSR</span></small>{slice ? <div className="adsrValues"><span>A <b>{slice.attack.toFixed(3)} s</b></span><span>D <b>{slice.decay.toFixed(3)} s</b></span><span>S <b>{Math.round(slice.sustain * 100)}%</b></span><span>R <b>{slice.release.toFixed(3)} s</b></span></div> : <p>Select a slice to view its envelope.</p>}</div><div className="group"><small>SLICE PLAYBACK <span>{slice ? `MIDI ${slice.note}` : 'PER SLICE'}</span></small>{slice ? <><label>PITCH <strong>{slice.pitch > 0 ? '+' : ''}{slice.pitch.toFixed(1)} st</strong></label><label>VOLUME <strong>{Math.round(slice.volume * 100)}%</strong><input type="range" min="0" max="2" step="0.01" value={slice.volume} onChange={e => void edit('volume', Number(e.target.value))}/></label><label>PAN <strong>{Math.abs(slice.pan) < .01 ? 'CENTER' : `${slice.pan < 0 ? 'L' : 'R'} ${Math.round(Math.abs(slice.pan) * 100)}`}</strong><input type="range" min="-1" max="1" step="0.01" value={slice.pan} onChange={e => void edit('pan', Number(e.target.value))}/></label></> : <p>Select a slice in the waveform to edit its playback.</p>}</div><div className="sliceList"><small>SLICE MAP</small>{state.slices.map((x, i) => <button className={x.id === selected ? 'active' : ''} key={x.id} onClick={() => void choose(x.id)}><span>{String(i + 1).padStart(2, '0')}</span><span>C{x.note - 60}</span><span>{((x.end - x.start) / state.sampleRate).toFixed(2)} s</span></button>)}</div></aside>
  <footer><button className="play" onClick={() => void invoke('playSample')}>▶</button><button onClick={() => void invoke('stopSample')}>Ⅱ</button><span>00:00.00 <i>/ {state.duration.toFixed(2)}</i></span><div className="track"/><small>MASTER　0.0 dB</small></footer></main>
}
createRoot(document.getElementById('root')!).render(<App/>);
