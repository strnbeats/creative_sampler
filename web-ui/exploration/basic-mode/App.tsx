import { useCallback, useEffect, useRef, useState, type CSSProperties } from 'react';
import {
  ArrowDownToLine, ArrowRight, AudioLines, ChevronDown, ChevronLeft, ChevronRight,
  CircleHelp, Search, X,
} from 'lucide-react';
import { demoName, initialValues, pads, presets } from './data';
import { MacroDial } from './MacroDial';
import { MaterialSwatches } from './MacroMaterial';

type Preset = (typeof presets)[number];
const categories = [...new Set(presets.map(preset => preset.category))];

function presetTint(preset: Preset): CSSProperties {
  return { '--preset-tint': preset.tint } as CSSProperties;
}
const preview = window.location.hash.slice(1);

function makeSignal(index: number) {
  return Array.from({ length: 44 }, (_, i) => {
    const x = i / 44;
    const attack = Math.min(1, x * (index % 3 === 0 ? 12 : 34));
    const decay = Math.exp(-x * (index % 3 + 1.1));
    const texture = Math.abs(Math.sin(i * 1.83 + index) * Math.cos(i * .21)) * .72 + .18;
    return attack * decay * texture + .033;
  });
}

const signals = pads.map((_, index) => makeSignal(index));

function Kit({ active, onTrigger, expanded = false }: {
  active: number | null; onTrigger: (index: number) => void; expanded?: boolean;
}) {
  return <section className={`kit ${expanded ? 'kit-expanded' : ''}`} aria-label="Playable kit">
    <div className="kit-head flex items-baseline justify-between">
      <h2><span className="status-light" />{expanded ? 'Source waveform' : 'Playable kit'}</h2>
      <p className="mono kit-count">AUTO SLICED <span>·</span> 8 SLICES</p>
    </div>
    <div className="kit-signal" aria-hidden="true">
      {pads.map((pad, index) => <div key={pad.note} className={active === index ? 'sounding' : ''}
        style={{ flexBasis: `${pad.length}%` }}>
        <svg viewBox="0 0 88 48" preserveAspectRatio="none">
          <path d="M0 24H88" className="wave-baseline" />
          {signals[index].map((height, i) =>
            <line key={i} x1={i * 2 + 1} x2={i * 2 + 1} y1={24 - height * 29} y2={24 + height * 23} />)}
        </svg>
      </div>)}
    </div>
    <div className="pads" role="group" aria-label="Slice pads, shortcuts 1 through 8">
      {pads.map((pad, index) => <button key={pad.note} type="button" className={active === index ? 'pad sounding' : 'pad'}
        aria-label={`Trigger ${pad.name}, ${pad.note}`} aria-keyshortcuts={String(index + 1)}
        onClick={() => onTrigger(index)}>
        <span className="pad-number mono">{String(index + 1).padStart(2, '0')}</span>
        <span className="pad-name">{pad.name}</span>
        <span className="pad-note mono">{pad.note}</span>
      </button>)}
    </div>
    <div className="kit-foot flex items-center justify-between">
      <span>Press <kbd>1</kbd>–<kbd>8</kbd> to trigger</span>
      <span role="status">{active === null ? 'Kit ready' : `${pads[active].name} · ${pads[active].note} triggered`}</span>
    </div>
  </section>;
}

function PresetBrowser({ preset, onPick, onClose }: {
  preset: Preset; onPick: (preset: Preset) => void; onClose: () => void;
}) {
  const dialog = useRef<HTMLDialogElement>(null);
  const [category, setCategory] = useState('All');
  const [query, setQuery] = useState('');

  useEffect(() => {
    const element = dialog.current;
    element?.showModal();
    return () => { if (element?.open) element.close(); };
  }, []);

  const filtered = presets.filter(candidate => (category === 'All' || candidate.category === category) &&
    `${candidate.name} ${candidate.category}`.toLowerCase().includes(query.trim().toLowerCase()));

  return <dialog className="browser" ref={dialog} aria-labelledby="browser-title"
    onCancel={event => { event.preventDefault(); onClose(); }}
    onClick={event => {
      if (event.target === event.currentTarget && event.clientX < event.currentTarget.getBoundingClientRect().left) onClose();
    }}>
    <header className="browser-head flex items-start justify-between">
      <div><p className="eyebrow">Factory collection</p><h2 id="browser-title">FX Presets</h2></div>
      <button className="key-button" aria-label="Close FX presets" onClick={onClose}><X size={18} /></button>
    </header>
    <label className="search flex items-center">
      <Search size={17} aria-hidden="true" />
      <input type="search" value={query} onChange={event => setQuery(event.target.value)} placeholder="Find a preset…" aria-label="Find a preset" />
    </label>
    <div className="filters flex flex-wrap" role="group" aria-label="Filter by category">
      {['All', ...categories].map(label => <button key={label} aria-pressed={label === category}
        onClick={() => setCategory(label)}>{label}</button>)}
    </div>
    <div className="browse-results">
      {categories.map(label => {
        const group = filtered.filter(candidate => candidate.category === label);
        if (group.length === 0) return null;
        return <section className="preset-group" key={label}>
          <h3 className="mono">{label}</h3>
          <ul className="preset-list">
            {group.map(candidate => <li key={candidate.id}>
              <button className="preset-option" data-preset-id={candidate.id} style={presetTint(candidate)}
                aria-current={candidate.id === preset.id ? 'true' : undefined} onClick={() => onPick(candidate)}>
                <MaterialSwatches materials={candidate.macros.slice(0, 3).map(macro => macro.material)} tint={candidate.tint} />
                <span className="preset-copy">
                  <strong>{candidate.name}</strong>
                  <span className="preset-description">{candidate.description}</span>
                </span>
                {candidate.id === preset.id ? <span className="preset-loaded mono">Loaded</span> : <ArrowRight size={16} />}
              </button>
            </li>)}
          </ul>
        </section>;
      })}
      {filtered.length === 0 && <p className="no-results" role="status">No presets found.</p>}
    </div>
  </dialog>;
}

export function App() {
  const [sample, setSample] = useState<string | null>(preview === 'empty' ? null : demoName);
  const [isDemo, setIsDemo] = useState(true);
  const [preparing, setPreparing] = useState(false);
  const [mode, setMode] = useState(preview === 'advanced' ? 'Advanced' : 'Basic');
  const [presetIndex, setPresetIndex] = useState(() =>
    Math.max(0, presets.findIndex(preset => preview === `preset=${preset.id}`)));
  const [browserOpen, setBrowserOpen] = useState(preview === 'browser');
  const [active, setActive] = useState<number | null>(preview === 'performance' ? 2 : null);
  const [storedValues, setStoredValues] = useState<Partial<Record<string, number[]>>>(() =>
    preview === 'performance' ? { meltdown: [83, 27, 42, 76, 36, 18] } : {});
  const file = useRef<HTMLInputElement>(null);
  const browserButton = useRef<HTMLButtonElement>(null);
  const triggerTimer = useRef<number | undefined>(undefined);
  const importTimer = useRef<number | undefined>(undefined);
  const preset = presets[presetIndex];
  const values = storedValues[preset.id] ?? initialValues(preset);
  const kitReady = Boolean(sample) && !preparing;

  const trigger = useCallback((index: number) => {
    if (!kitReady) return;
    window.clearTimeout(triggerTimer.current);
    setActive(index);
    triggerTimer.current = window.setTimeout(() => setActive(null), 1000);
  }, [kitReady]);

  useEffect(() => {
    function onKey(event: globalThis.KeyboardEvent) {
      if (!kitReady || browserOpen || event.repeat || event.defaultPrevented ||
        event.ctrlKey || event.metaKey || event.altKey || event.shiftKey ||
        (event.target instanceof HTMLElement && event.target.closest('input, [role="slider"], [contenteditable]'))) return;
      if (/^[1-8]$/.test(event.key)) {
        event.preventDefault();
        trigger(Number(event.key) - 1);
      }
    }
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [kitReady, browserOpen, trigger]);

  useEffect(() => () => {
    window.clearTimeout(triggerTimer.current);
    window.clearTimeout(importTimer.current);
  }, []);

  function loadSample(name: string, demo: boolean) {
    setPreparing(true);
    window.clearTimeout(triggerTimer.current);
    setActive(null);
    window.clearTimeout(importTimer.current);
    importTimer.current = window.setTimeout(() => {
      setSample(name);
      setIsDemo(demo);
      setPreparing(false);
    }, 550);
  }

  function closeBrowser() {
    setBrowserOpen(false);
    requestAnimationFrame(() => browserButton.current?.focus());
  }

  function changeMacro(index: number, value: number) {
    setStoredValues(previous => {
      const current = previous[preset.id] ?? initialValues(preset);
      if (current[index] === value) return previous;
      return { ...previous, [preset.id]: current.map((old, i) => i === index ? value : old) };
    });
  }

  return <main className="instrument mx-auto">
    <header className="rail">
      <div className="brand flex items-center"><AudioLines size={25} strokeWidth={1.8} aria-hidden="true" />
        <span>Creative<span className="brand-second">Sampler</span></span>
      </div>
      <div className="mode-switch" role="group" aria-label="Workspace">
        {['Basic', 'Advanced'].map(view => <button key={view} aria-pressed={mode === view}
          type="button" onClick={() => setMode(view)}>{view}</button>)}
      </div>
      <button className="import-button flex items-center justify-center" type="button" disabled={preparing}
        onClick={() => file.current?.click()}><ArrowDownToLine size={16} aria-hidden="true" /> Import sample</button>
      <input ref={file} type="file" accept="audio/*,.wav,.aiff,.aif,.caf,.flac" hidden onChange={event => {
        const selected = event.currentTarget.files?.[0];
        if (selected) loadSample(selected.name, false);
        event.currentTarget.value = '';
      }} />
    </header>

    <div className="source-bar flex items-center justify-between">
      <div className="sample-identity">
        <span className="eyebrow">Source</span>
        <span className="sample-name" title={sample ?? undefined}>{preparing ? 'Preparing kit…' : sample ?? 'No sample loaded'}</span>
      </div>
      {kitReady && <div className="source-meta mono">
        {isDemo ? <>4.82 s <i /> 144 BPM <i /> F min</> : 'DEMO KIT'}
      </div>}
    </div>

    {mode === 'Basic' ? <>
      <section className="fx-stage" style={presetTint(preset)} aria-label="Preset and FX macros">
        <div className="fx-head">
          <div className="preset-identity">
            <p className="eyebrow copper-text">FX Preset <span className="tag" title={preset.category}>{preset.category}</span></p>
            <h1 title={preset.name}>{preset.name}</h1>
            <p className="preset-deck" title={preset.description}>{preset.description}</p>
          </div>
          <div className="fx-navigation">
            <div className="preset-switch flex items-center">
              <button className="key-button" aria-label="Previous preset" onClick={() =>
                setPresetIndex(index => (index - 1 + presets.length) % presets.length)}><ChevronLeft size={17} /></button>
              <button className="browser-button" ref={browserButton} aria-haspopup="dialog" aria-expanded={browserOpen}
                onClick={() => setBrowserOpen(true)}>Browse presets <ChevronDown size={14} /></button>
              <button className="key-button" aria-label="Next preset" onClick={() =>
                setPresetIndex(index => (index + 1) % presets.length)}><ChevronRight size={17} /></button>
            </div>
            <div className="chain mono" aria-label={`Demo chain: ${preset.chain.join(', ')}`}>
              {preset.chain.map((node, index) => <span className="chain-step" key={node}>
                {index > 0 && <ArrowRight size={10} aria-hidden="true" />}
                <span className="chain-label" title={node}>{node}</span>
              </span>)}
            </div>
          </div>
        </div>
        <div className="macro-bank" aria-label="FX macros">
          {preset.macros.map((macro, i) => <MacroDial key={`${preset.id}-${i}`} material={macro.material} tint={preset.tint} label={macro.name}
            defaultValue={macro.initial} mapping={macro.mapping} value={values[i]}
            disabled={!kitReady} onChange={value => changeMacro(i, value)} />)}
        </div>
        <div className="gesture-tip"><CircleHelp size={13} aria-hidden="true" /> Drag vertically <span>·</span> Double-click to reset</div>
      </section>

      {kitReady ? <Kit active={active} onTrigger={trigger} /> :
        <section className="kit kit-empty" aria-label="Playable kit">
          <AudioLines size={34} strokeWidth={1.35} aria-hidden="true" />
          <div><h2>{preparing ? 'Preparing kit…' : 'Start with a sample'}</h2>
            <p>{preparing ? 'Building the demo slice layout.' : 'Import a file or load a demo kit.'}</p></div>
          {!preparing && <button type="button" onClick={() => loadSample(demoName, true)}>Load demo kit <ArrowRight size={15} /></button>}
        </section>}
    </> : <>
      <section className="advanced-intro">
        <p className="eyebrow copper-text">Advanced preview</p>
        <h1>{sample ?? 'No sample loaded'}</h1>
        <p>Inspect the source and trigger the same kit. Slice editing is outside this preview.</p>
        <button className="import-button" onClick={() => setMode('Basic')}>Back to Basic</button>
      </section>
      {kitReady && <Kit active={active} onTrigger={trigger} expanded />}
    </>}

    <footer className="footer flex items-center justify-between">
      <span><span className="mini-dot" /> Visual prototype</span><span>Simulated interaction · no audio</span>
    </footer>

    {mode === 'Basic' && browserOpen && <PresetBrowser preset={preset} onClose={closeBrowser}
      onPick={candidate => {
        setPresetIndex(presets.indexOf(candidate));
        closeBrowser();
      }} />}
  </main>;
}
