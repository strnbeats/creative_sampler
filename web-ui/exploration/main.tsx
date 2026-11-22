import React, { useState } from 'react';
import { createRoot } from 'react-dom/client';
import './styles.css';

type Direction = 'precision' | 'expressive' | 'experimental';
type ScenarioId = 'minimal' | 'dense' | 'transient' | 'metadata' | 'performance';
type Slice = { id: number; start: number; end: number; note: string; label?: string; midi: number; length: string; tint: string };
type Scenario = { id: ScenarioId; label: string; title: string; secondTitle: string; descriptor: string; duration: string; sampleRate: string; channels: string; bpm: string; key: string; slices: Slice[]; transients: number[]; bars: number; peaks: number[] };

const slices: Slice[] = [
  { id: 1, start: 0, end: 0.16, note: 'C3', midi: 48, length: '0.77 s', tint: '#91d9ce' },
  { id: 2, start: 0.16, end: 0.31, note: 'D#3', midi: 51, length: '0.72 s', tint: '#b5a0f2' },
  { id: 3, start: 0.31, end: 0.49, note: 'F3', midi: 53, length: '0.87 s', tint: '#86c7e8' },
  { id: 4, start: 0.49, end: 0.65, note: 'G#3', midi: 56, length: '0.77 s', tint: '#cf9bd7' },
  { id: 5, start: 0.65, end: 0.82, note: 'A#3', midi: 58, length: '0.82 s', tint: '#9dd5c7' },
  { id: 6, start: 0.82, end: 1, note: 'C4', midi: 60, length: '0.87 s', tint: '#aaa1e9' },
];

const palette = ['#91d9ce', '#b5a0f2', '#86c7e8', '#cf9bd7', '#9dd5c7', '#aaa1e9'];
const makeSlices = (weights: number[], notes: string[], names: string[] = [], duration = 8): Slice[] => {
  const total = weights.reduce((sum, value) => sum + value, 0);
  let position = 0;
  return weights.map((weight, index) => {
    const start = position / total;
    position += weight;
    const end = position / total;
    return { id: index + 1, start, end, note: notes[index % notes.length], label: names[index], midi: 48 + (index * 3) % 24,
      length: `${((end - start) * duration).toFixed(2)} s`, tint: palette[index % palette.length] };
  });
};
const distribute = (count: number, offset = 0) => Array.from({ length: count }, (_, index) => ((index * 37 + 13 + offset * 19) % 997) / 997);
const pulsePeaks = (positions: number[], width = .018) => positions.map((position, index) => ({ position, width, amplitude: .32 + ((index * 7) % 5) * .1 }));
const densePositions = Array.from({ length: 29 }, (_, index) => .025 + index * .033 + ((index * 17) % 5) * .004).filter(value => value < .98);
const transientPositions = [...Array.from({ length: 25 }, (_, index) => .035 + index * .037), .48, .487, .494, .501, .508, .515, .72, .728, .736, .744, .9, .907, .914];

const scenarios: Record<ScenarioId, Scenario> = {
  minimal: { id: 'minimal', label: 'A · SHORT', title: 'Soft', secondTitle: 'Signal', descriptor: 'ONE-SHOT TEXTURE', duration: '0.84 SEC', sampleRate: '48 kHz', channels: 'MONO', bpm: '—', key: '—',
    slices: makeSlices([.44, .56], ['C4', 'G4'], ['First hit', 'Tail'], .84), transients: [.18, .68, .83], bars: 220, peaks: [.18, .68] },
  dense: { id: 'dense', label: 'B · DENSE', title: 'Nightdrive', secondTitle: 'Fragments', descriptor: 'LIVE VOCAL COLLAGE', duration: '38.40 SEC', sampleRate: '96 kHz', channels: 'STEREO', bpm: '126', key: 'F♯ MIN',
    slices: makeSlices([5, 3, 8, 4, 9, 2, 7, 5, 11, 3, 6, 8, 4, 10, 3, 12], ['C3', 'D3', 'E3', 'F♯3', 'G3', 'A3', 'B3'], ['Air cut', 'Vocal rise', 'Glass tail', 'Reverse bloom', 'Low phrase', 'Breath', 'Bright attack', 'Room tone', 'Choir fragment', 'Micro cut', 'Soft consonant', 'Long sustain', 'Noise edge', 'Held vowel', 'Echo tap', 'Final wash'], 38.4),
    transients: densePositions, bars: 280, peaks: pulsePeaks(densePositions, .012).map(peak => peak.position) },
  transient: { id: 'transient', label: 'C · TRANSIENT', title: 'Silver', secondTitle: 'Rain', descriptor: 'PERCUSSION STUDY', duration: '12.00 SEC', sampleRate: '48 kHz', channels: 'STEREO', bpm: '—', key: '—',
    slices: makeSlices([.24, .03, .025, .26, .035, .02, .03, .22, .04, .025, .07], ['C2', 'D2', 'F2', 'G2', 'A2', 'C3'], ['Opening', 'Ghost', 'Flam A', 'Impact', 'Ghost', 'Flam B', 'Rattle', 'Body', 'Tail', 'Tick', 'Air'], 12),
    transients: transientPositions, bars: 250, peaks: pulsePeaks(transientPositions, .009).map(peak => peak.position) },
  metadata: { id: 'metadata', label: 'D · METADATA', title: 'Afterimage Choir', secondTitle: 'Final Vocal Print 08', descriptor: 'ALT TAKE 04 · CONSOLIDATED STEM · 24 BIT', duration: '02:46.320', sampleRate: '96 kHz', channels: 'STEREO', bpm: '168', key: 'D♯ MIN',
    slices: makeSlices([7, 12, 5, 14, 8, 4, 11, 6, 16, 7, 10, 13], ['D♯3', 'F♯3', 'G♯3', 'A♯3', 'C♯4', 'D♯4'], ['Lead breath / onset', 'Upper harmony phrase', 'Consonant pickup', 'Long choir sustain', 'Doubled vowel', 'Whisper layer', 'Wide vocal bloom', 'Soft phrase ending', 'Reverberant tail', 'Central harmony', 'Reverse breath', 'Final sustained chord'], 166.32),
    transients: distribute(23, 3), bars: 270, peaks: pulsePeaks(distribute(10, 3), .025).map(peak => peak.position) },
  performance: { id: 'performance', label: 'E · PERFORMANCE', title: 'Garden', secondTitle: 'Glass', descriptor: 'CHOPPED TEXTURE', duration: '4.82 SEC', sampleRate: '48 kHz', channels: 'STEREO', bpm: '148', key: 'G♯ MIN',
    slices, transients: [.072, .141, .247, .318, .431, .501, .624, .702, .793, .865, .936], bars: 280, peaks: [.08, .24, .42, .62, .79, .94] },
};

function StudySelector({ direction, onChange, scenario, onScenarioChange }: { direction: Direction; onChange: (value: Direction) => void; scenario: ScenarioId; onScenarioChange: (value: ScenarioId) => void }) {
  const options: [Direction, string][] = [
    ['precision', 'A  PRECISION'], ['expressive', 'B  EXPRESSIVE'], ['experimental', 'C  EXPERIMENTAL'],
  ];
  return <div className="studyBar"><div className="studyTitle"><span className="studyMark">CS</span><span>CREATIVE SAMPLER <i>/ VISUAL STUDIES</i></span></div><div className="studyTabs">{options.map(([key, label]) => <button key={key} className={direction === key ? 'active' : ''} onClick={() => onChange(key)}>{label}</button>)}</div><div className="studyTools">{direction === 'expressive' && <label className="scenarioControl"><span>STRESS</span><select aria-label="Stress test scenario" value={scenario} onChange={event => onScenarioChange(event.target.value as ScenarioId)}>{Object.values(scenarios).map(option => <option key={option.id} value={option.id}>{option.label}</option>)}</select></label>}<span className="studyTag">{direction === 'expressive' ? `${window.innerWidth} × ${window.innerHeight}` : `MOCK DATA · ${window.innerWidth} × ${window.innerHeight}`}</span></div></div>;
}

function WaveBars({ className = '', bars = 260, peaks = [.08, .24, .42, .62, .79, .94] }: { className?: string; bars?: number; peaks?: number[] }) {
  const values = Array.from({ length: bars }, (_, i) => {
    const t = i / bars;
    const envelope = 0.045 + peaks.reduce((sum, center, index) => sum + (.34 + (index * 7 % 5) * .11) * Math.exp(-Math.pow((t - center) / (peaks.length > 14 ? .012 : .032), 2)), 0);
    const texture = 0.24 + Math.abs(Math.sin(i * 2.399)) * 0.55 + Math.abs(Math.sin(i * 0.371)) * 0.25;
    return Math.min(0.93, envelope * texture);
  });
  return <svg className={`waveBars ${className}`} viewBox={`0 0 ${bars} 100`} preserveAspectRatio="none" aria-hidden="true">
    {values.map((value, i) => <line key={i} x1={i + 0.5} x2={i + 0.5} y1={50 - value * 47} y2={50 + value * 47} />)}
  </svg>;
}

function Envelope({ className = '' }: { className?: string }) {
  return <svg className={`envelope ${className}`} viewBox="0 0 300 100" preserveAspectRatio="none" aria-label="ADSR envelope">
    <path className="envGuide" d="M0 88H300M0 12V88" />
    <path className="envArea" d="M0 88L35 18L100 53L218 53L292 87L300 87V88H0Z" />
    <path className="envCurve" d="M0 88L35 18L100 53L218 53L292 87" />
    {[['0','88'], ['35','18'], ['100','53'], ['218','53'], ['292','87']].map(([x, y], i) => <circle key={i} cx={x} cy={y} r="3.2" />)}
  </svg>;
}

function Transport({ expressive = false, light = false, duration = '4.82', playing: controlledPlaying, onPlayingChange }: { expressive?: boolean; light?: boolean; duration?: string; playing?: boolean; onPlayingChange?: (playing: boolean) => void }) {
  const [internalPlaying, setInternalPlaying] = useState(false);
  const playing = controlledPlaying ?? internalPlaying;
  const setPlaying = (next: boolean) => onPlayingChange ? onPlayingChange(next) : setInternalPlaying(next);
  return <div className={`transport ${expressive ? 'transportExpressive' : ''} ${light ? 'transportLight' : ''}`}>
    <button className="playButton" onClick={() => setPlaying(!playing)} aria-label={playing ? 'Pause' : 'Play'}>{playing ? 'Ⅱ' : '▶'}</button>
    <button className="stopButton" onClick={() => setPlaying(false)}>STOP</button>
    <div className="transportTime"><b>{playing ? '00:01.84' : '00:00.00'}</b><span>/ {duration}</span></div>
    <div className="progressTrack"><i className={playing ? 'moving' : ''} /></div>
    <div className="masterValue"><small>MASTER</small><strong>0.0 <em>dB</em></strong></div>
  </div>;
}

function SliceIntervals({ selected, onSelect, accent = false, data = slices, compactLabels = false }: { selected: number; onSelect: (id: number) => void; accent?: boolean; data?: Slice[]; compactLabels?: boolean }) {
  return <div className={`intervals ${accent ? 'intervalsAccent' : ''}`}>
    {data.map((slice, i) => <button aria-label={`Select slice ${i + 1}`} key={slice.id}
      className={`interval ${selected === slice.id ? 'selected' : ''}`}
      style={{ left: `${slice.start * 100}%`, width: `${(slice.end - slice.start) * 100}%`, '--slice': slice.tint } as React.CSSProperties}
      onClick={() => onSelect(slice.id)}>
      {(slice.end - slice.start) > (compactLabels ? .038 : .055) && <span className="intervalNo">{String(i + 1).padStart(2, '0')}</span>}{!compactLabels && (slice.end - slice.start) > .055 && <span className="intervalNote">{slice.note}</span>}
    </button>)}
  </div>;
}

function Transients({ className = '', positions = [.072, .141, .247, .318, .431, .501, .624, .702, .793, .865, .936] }: { className?: string; positions?: number[] }) {
  return <div className={`transients ${className}`}>{positions.map((p, i) => <i key={i} style={{ left: `${p * 100}%`, height: `${14 + (i * 13) % 23}px` }} />)}</div>;
}

function APrecision({ selected, onSelect }: { selected: number; onSelect: (id: number) => void }) {
  const slice = slices[selected - 1];
  return <main className="precision concept">
    <header className="precisionHeader">
      <div className="brandMark"><b>CS</b><span>CREATIVE<br />SAMPLER</span></div>
      <div className="headerDivider" />
      <div className="sampleHeading"><small>SOURCE / 48 kHz · STEREO</small><strong>Garden Glass — chopped texture</strong></div>
      <div className="headerMeta"><span>4.82 <i>SEC</i></span><span>148 <i>BPM</i></span><span>G♯ <i>MIN</i></span></div>
      <button className="importButton">IMPORT <span>↗</span></button>
    </header>
    <div className="precisionMain">
      <div className="precisionCaption"><span><i className="statusDot" /> SOURCE TIMELINE</span><span>BAR 01—03 <b>·</b> 48,000 SAMPLES / S</span><div className="editActions"><button>＋ SPLIT</button><button>RESET</button></div></div>
      <div className="precisionCanvas">
        <div className="ruler"><span>00:00.000</span><span>00:01.000</span><span>00:02.000</span><span>00:03.000</span><span>00:04.820</span></div>
        <div className="precisionWave">
          <div className="gridLines"><i /><i /><i /><i /><i /><i /><i /><i /><i /></div>
          <SliceIntervals selected={selected} onSelect={onSelect} />
          <WaveBars className="preciseBars" />
          <Transients className="preciseTransient" />
          <div className="precisionPlayhead" />
          <div className="waveAxis"><span>L</span><span>R</span></div>
        </div>
        <div className="precisionFoot"><span>TRANSIENTS <b>11</b></span><span>GRID <b>1/16</b></span><span>ZOOM <b>1.0×</b></span><span>SELECTED <b>{String(selected).padStart(2, '0')} / {slice.note}</b></span><div className="zoomMarks">− <i /><i /><i /> +</div></div>
      </div>
      <div className="precisionInspector">
        <div className="selectedReadout"><small>ACTIVE SLICE</small><strong>{String(selected).padStart(2, '0')}</strong><span>{slice.note} <i>·</i> {slice.length}</span><span className="voiceBadge">MIDI {slice.midi}</span></div>
        <div className="precisionEnvelope"><div className="controlLabel">AMPLITUDE / ENVELOPE <span>ADSR</span></div><Envelope /></div>
        <div className="precisionParams">
          <div className="preciseParam volume"><small>LEVEL</small><strong>−3.7 <i>dB</i></strong><div><i /></div></div>
          <div className="preciseParam pan"><small>PAN</small><strong>R 24</strong><div><i /></div></div>
          <div className="preciseParam pitch"><small>PITCH</small><strong>0 <i>st</i></strong><div>−12 <b>0</b> +12</div></div>
        </div>
      </div>
    </div>
    <Transport />
  </main>;
}

function BExpressive({ selected, onSelect, scenario, playing, onPlayingChange }: { selected: number; onSelect: (id: number) => void; scenario: Scenario; playing: boolean; onPlayingChange: (playing: boolean) => void }) {
  const slice = scenario.slices.find(item => item.id === selected) ?? scenario.slices[0];
  const transientHeavy = scenario.transients.length > 35;
  return <main className={`expressive concept ${scenario.id} ${playing ? 'isPlaying' : ''}`}>
    <header className="expressiveHeader">
      <div className="expressiveBrand"><span className="orbitLogo"><i /></span><b>CREATIVE<br />SAMPLER</b></div>
      <div className="expressiveSample"><small>ONE SOURCE, MANY VOICES</small><h1>{scenario.title}<br /><em>{scenario.secondTitle}</em></h1><span>{scenario.descriptor} <i>·</i> {scenario.duration} <i>·</i> {scenario.sampleRate} {scenario.channels}</span></div>
      <div className="expressiveTools"><button>IMPORT SAMPLE <span>↗</span></button><div className="sampleFacts"><span><small>TEMPO</small><b>{scenario.bpm}{scenario.bpm !== '—' && <i> BPM</i>}</b></span><span><small>KEY</small><b>{scenario.key}</b></span><span><small>ATTACKS</small><b>{scenario.transients.length}</b></span></div></div>
    </header>
    <section className="expressiveStage">
      <div className="stageKicker"><span>01 — SOURCE</span><span title={slice.label ?? slice.note}>SLICE {String(selected).padStart(2, '0')} · {slice.label ?? slice.note} · {slice.length}</span></div>
      <div className="ribbonHalo" />
      <div className="expressiveWave">
        <div className="expressiveSliceTint" style={{ left: `${slice.start * 100}%`, width: `${(slice.end - slice.start) * 100}%` }} />
        <WaveBars className="expressiveBars" bars={scenario.bars} peaks={scenario.peaks} />
        <SliceIntervals selected={selected} onSelect={onSelect} accent data={scenario.slices} compactLabels={scenario.slices.length > 10} />
        <Transients className={`expressiveTransient ${transientHeavy ? 'transientHeavy' : ''}`} positions={scenario.transients} />
        <div className={`expressivePlayhead ${playing ? 'moving' : ''}`} />
      </div>
      <div className="ribbonRuler"><span>0:00</span><div><i /><i /><i /><i /><i /><i /><i /><i /><i /><i /></div><span>{scenario.duration}</span></div>
    </section>
    <aside className="expressiveInspector">
      <div className="inspectorTop"><span>SLICE {String(selected).padStart(2, '0')}</span><span>{slice.label ?? slice.note}</span></div>
      <div className="expressiveEnvelopeBlock"><Envelope className="expressiveEnvelope" /><div className="expressiveEnvValues"><span>A <b>5 ms</b></span><span>D <b>100 ms</b></span><span>S <b>100%</b></span><span>R <b>100 ms</b></span></div></div>
      <div className="expressionControls">
        <div className="expressionLevel"><small>VOLUME</small><div className="levelArc"><i /></div><b>−3.7 <em>dB</em></b></div>
        <div className="expressionPan"><small>STEREO FIELD</small><div><i /><b>R</b></div><strong>24</strong></div>
        <div className="expressionPitch"><small>PITCH</small><strong>0 <em>st</em></strong><div>−24 <i /><b>+24</b></div></div>
      </div>
      <div className="sliceSequence"><small>SLICE SEQUENCE <i>{scenario.slices.length} FRAGMENTS</i></small><div>{scenario.slices.map((s, i) => <button key={s.id} title={s.label ?? s.note} className={selected === s.id ? 'chosen' : ''} onClick={() => onSelect(s.id)}><span>{scenario.slices.length > 10 ? String(i + 1).padStart(2, '0') : s.label ?? s.note}</span><i>{String(i + 1).padStart(2, '0')}</i></button>)}</div></div>
    </aside>
    <Transport expressive duration={scenario.duration} playing={playing} onPlayingChange={onPlayingChange} />
  </main>;
}

function CExperimental({ selected, onSelect }: { selected: number; onSelect: (id: number) => void }) {
  const slice = slices[selected - 1];
  return <main className="experimental concept">
    <header className="experimentalHeader">
      <div className="cutLogo"><span>CS</span><i> / </i> CUT RECORD NO. 014</div>
      <div className="expSample"><small>RAW MATERIAL</small><b>GARDEN GLASS</b><span>4.82s <i>·</i> 48 kHz <i>·</i> STEREO</span></div>
      <div className="expAnalysis"><span>148 <i>BPM</i></span><span>G♯ <i>MIN</i></span><span>11 <i>TRANSIENTS</i></span></div>
      <button className="expImport">＋ LOAD</button>
    </header>
    <div className="specimenOverview"><span>SOURCE / 01</span><div><WaveBars bars={240} /><SliceIntervals selected={selected} onSelect={onSelect} /></div><span>4.82 s</span></div>
    <div className="specimenHeading"><div><small>FRAGMENT ATLAS</small><h2>Six cuts <i>from one source.</i></h2></div><div className="specimenLegend"><span><i className="legendSelected" /> SELECTED</span><span><i className="legendTransient" /> TRANSIENT</span></div></div>
    <section className="atlas">
      {slices.map((s, index) => <button key={s.id} onClick={() => onSelect(s.id)} className={`specimen ${selected === s.id ? 'specimenSelected' : ''}`} style={{ flex: `${s.end - s.start} 1 0` }}>
        <div className="specimenTop"><span>{String(index + 1).padStart(2, '0')}</span><small>{s.length}</small></div>
        <div className="specimenBars" style={{ '--specimen': s.tint } as React.CSSProperties}>{Array.from({ length: 34 }, (_, n) => {
          const env = .18 + Math.exp(-Math.pow((n / 34 - (index % 2 ? .65 : .23)) / .24, 2)) * .72;
          const h = (8 + ((n * 19 + index * 31) % 34) * env);
          return <i key={n} style={{ height: `${Math.min(90, h)}%` }} />;
        })}</div>
        <div className="specimenBase"><b>{s.note}</b><span>KEY {s.midi}</span></div>
        {selected === s.id && <div className="specimenFocus">IN FOCUS <i>↗</i></div>}
      </button>)}
    </section>
    <div className="experimentalLower">
      <div className="expSelected"><small>SELECTED CUT</small><strong>{String(selected).padStart(2, '0')} <i>/</i> {slice.note}</strong><span>{slice.length} <i>·</i> MIDI {slice.midi}</span></div>
      <div className="expEnvelope"><div><small>AMPLITUDE SHAPE</small><span>A 5ms　 D 100ms　 S 100%　 R 100ms</span></div><Envelope /></div>
      <div className="expParams"><div><small>GAIN</small><b>−3.7 <i>dB</i></b><span className="gainLine"><i /></span></div><div><small>STEREO</small><b>R 24</b><span className="panLine"><i /></span></div><div><small>PITCH</small><b>0 <i>st</i></b><span className="pitchSteps">−24　 ──●──　 +24</span></div></div>
    </div>
    <div className="expFooter"><span>SLICE AS INSTRUMENT</span><div className="keybed">{slices.map((s, i) => <button key={s.id} onClick={() => onSelect(s.id)} className={selected === s.id ? 'keyActive' : ''}><small>{s.note}</small><i>{i + 1}</i></button>)}</div><Transport light /></div>
  </main>;
}

function App() {
  const [direction, setDirection] = useState<Direction>(() => {
    const initial = window.location.hash.slice(1);
    return initial === 'expressive' || initial === 'experimental' ? initial : 'precision';
  });
  const [scenarioId, setScenarioId] = useState<ScenarioId>('performance');
  const [selected, setSelected] = useState(2);
  const [playing, setPlaying] = useState(true);
  const chooseDirection = (next: Direction) => {
    setDirection(next);
    window.history.replaceState(null, '', `#${next}`);
  };
  const chooseScenario = (next: ScenarioId) => {
    setScenarioId(next);
    setSelected(scenarios[next].slices[0].id);
    setPlaying(next === 'performance');
  };
  return <div className="exploration">
    <StudySelector direction={direction} onChange={chooseDirection} scenario={scenarioId} onScenarioChange={chooseScenario} />
    {direction === 'precision' && <APrecision selected={selected} onSelect={setSelected} />}
    {direction === 'expressive' && <BExpressive selected={selected} onSelect={setSelected} scenario={scenarios[scenarioId]} playing={playing} onPlayingChange={setPlaying} />}
    {direction === 'experimental' && <CExperimental selected={selected} onSelect={setSelected} />}
  </div>;
}

const container = document.getElementById('root')!;
const appWindow = window as Window & { __creativeSamplerExplorationRoot?: ReturnType<typeof createRoot> };
const appRoot = appWindow.__creativeSamplerExplorationRoot ?? (appWindow.__creativeSamplerExplorationRoot = createRoot(container));
appRoot.render(<App />);
