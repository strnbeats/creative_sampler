import { useEffect, useMemo, useState } from 'react';
import { Button } from '../components/Button';
import { Label, Metadata, Value } from '../components/Typography';
import { nativeBridge } from '../bridge/nativeBridge';
import type { AnalysisDto, ViewSlice, ViewTransient } from '../bridge/protocol';
import { Inspector } from './Inspector';
import { Transport } from './Transport';
import { Waveform } from './Waveform';
import { useSamplerState } from './useSamplerState';
import './app.css';

const pitchNames = ['C', 'C♯', 'D', 'D♯', 'E', 'F', 'F♯', 'G', 'G♯', 'A', 'A♯', 'B'];
function formatMidiNote(note: number) {
  return `${pitchNames[note % 12]}${Math.floor(note / 12) - 1}`;
}
function analysisText(analysis: AnalysisDto | null) {
  if (!analysis || analysis.status === 'notAnalyzed') return 'NOT ANALYZED';
  if (analysis.status === 'running') return 'ANALYZING';
  if (analysis.status === 'failed') return 'ANALYSIS FAILED';
  if (analysis.status === 'cancelled') return 'ANALYSIS CANCELLED';
  return 'ANALYSIS COMPLETE';
}

export function App() {
  const integration = useSamplerState();
  const snapshot = integration.snapshot;
  const loading = snapshot?.app.status === 'loading';
  const sample = !loading ? snapshot?.sample ?? null : null;
  const analysis = snapshot && sample && snapshot.analysis?.sampleId === sample.id ? snapshot.analysis : null;
  const [selectedSliceId, setSelectedSliceId] = useState<string | null>(null);
  const [importError, setImportError] = useState<string | null>(null);

  const slices = useMemo<ViewSlice[]>(() => snapshot && sample && snapshot.sample?.id === sample.id
    ? snapshot.slices.map((slice, index) => ({
      id: slice.id,
      index: index + 1,
      name: `Slice ${String(index + 1).padStart(2, '0')}`,
      note: formatMidiNote(slice.midiNote),
      midiNote: slice.midiNote,
      startSample: slice.startSample,
      endSample: slice.endSample,
      volume: slice.parameters.volume,
      pan: slice.parameters.pan,
      pitch: slice.parameters.pitchSemitones,
      attackSeconds: slice.parameters.envelope.attackSeconds,
      decaySeconds: slice.parameters.envelope.decaySeconds,
      sustain: slice.parameters.envelope.sustainLevel,
      releaseSeconds: slice.parameters.envelope.releaseSeconds,
    })) : [], [sample?.id, snapshot]);

  useEffect(() => {
    setSelectedSliceId(slices[0]?.id ?? null);
  }, [sample?.id]);
  useEffect(() => {
    if (selectedSliceId && !slices.some(slice => slice.id === selectedSliceId))
      setSelectedSliceId(slices[0]?.id ?? null);
  }, [selectedSliceId, slices]);

  const selectedSlice = slices.find(slice => slice.id === selectedSliceId) ?? null;
  const bpm = analysis?.status === 'completed' ? analysis.bpm?.value ?? null : null;
  const key = analysis?.status === 'completed' && analysis.key?.tonic
    ? `${analysis.key.tonic}${analysis.key.mode === 'minor' ? ' MIN' : ' MAJ'}` : null;
  const transientData = analysis?.status === 'completed' ? analysis.transients : null;
  const transients: ViewTransient[] = (transientData?.positions ?? []).map((event, index) => ({
    id: `${analysis?.generation}-${index}`,
    samplePosition: event.samplePosition,
    strength: event.strength,
  }));
  const transportPosition = integration.transport && sample && integration.transport.sampleId === sample.id
    ? integration.transport.positionSamples : snapshot?.sampleTransport?.positionSamples ?? 0;
  const transportPlaying = integration.transport && sample && integration.transport.sampleId === sample.id
    ? integration.transport.status === 'playing' : snapshot?.sampleTransport?.status === 'playing';
  const positionSeconds = sample && sample.sampleRate > 0 ? transportPosition / sample.sampleRate : 0;

  const importSample = async () => {
    setImportError(null);
    try { await nativeBridge.importSample(); }
    catch (error) { setImportError(error instanceof Error ? error.message : String(error)); }
  };

  const bridgeUnavailable = integration.connection !== 'connected';
  const nativeImportError = snapshot?.app.status === 'error' ? snapshot.app.message : null;
  const topMessage = importError ?? nativeImportError
    ?? (bridgeUnavailable ? integration.error ?? 'Connecting to Creative Sampler…' : null);

  return <div className="cs-ui-v2">
    {topMessage && <div className={`cs-connection-status${bridgeUnavailable || importError || nativeImportError ? ' is-error' : ''}`} role="status">
      <span>{topMessage}</span>
      {bridgeUnavailable && <Button onClick={integration.retry}>RETRY</Button>}
    </div>}

    <main className="cs-app-shell">
      <header className="cs-sample-header">
        <div className="cs-product-mark" aria-label="Creative Sampler"><span>◉</span><b>CREATIVE<br />SAMPLER</b></div>
        <div className="cs-sample-identity">
          <div className="cs-sample-source-row">
            <Label>{loading ? 'IMPORTING SAMPLE' : sample ? 'ONE SOURCE, MANY VOICES' : 'SOURCE SAMPLE'}</Label>
            <Button tone="accent" onClick={() => void importSample()} disabled={bridgeUnavailable || loading}>＋ IMPORT</Button>
          </div>
          <h1>{loading ? 'Loading sample…' : sample?.fileName ?? 'No sample loaded'}</h1>
          <Metadata>{sample
            ? `${sample.durationSeconds.toFixed(2)} SEC · ${(sample.sampleRate / 1000).toFixed(1)} kHz · ${sample.channelCount === 2 ? 'STEREO' : 'MONO'}${sample.bitDepth ? ` · ${sample.bitDepth}-BIT` : ''}${analysis ? ` · ${analysisText(analysis)}` : ''}`
            : loading ? 'PREPARING AUDIO AND ANALYSIS' : snapshot?.app.message ?? 'IMPORT A WAV OR AIFF TO BEGIN'}</Metadata>
        </div>
        <div className={`cs-sample-analysis${sample ? '' : ' is-empty'}`} aria-label="Analysis results">
          <div><Label>TEMPO</Label><Value>{bpm !== null ? <>{bpm.toFixed(1)} <small>BPM</small></> : analysis?.status === 'running' ? '…' : '—'}</Value></div>
          <div><Label>KEY</Label><Value>{key ?? (analysis?.status === 'running' ? '…' : '—')}</Value></div>
          <div><Label>ATTACKS</Label><Value>{transientData ? transientData.totalEventCount : analysis?.status === 'running' ? '…' : '—'}</Value></div>
        </div>
      </header>

      <Waveform metadata={sample ? {
        id: sample.id, name: sample.fileName, durationSeconds: sample.durationSeconds,
        sampleRate: sample.sampleRate, sampleCount: sample.sampleCount, channelCount: sample.channelCount,
      } : null}
        peaks={integration.waveform && sample && integration.waveform.sampleId === sample.id ? integration.waveform.channels : []}
        sampleCount={sample?.sampleCount ?? 0}
        slices={slices} transients={transients} selectedSliceId={selectedSliceId}
        playheadPositionSamples={positionSeconds * (sample?.sampleRate ?? 0)} playing={transportPlaying}
        onSelectSlice={setSelectedSliceId} />

      <Inspector slice={selectedSlice} sliceCount={slices.length} sampleRate={sample?.sampleRate ?? 0} />

      <Transport durationSeconds={sample?.durationSeconds ?? 0} positionSeconds={positionSeconds}
        isPlaying={transportPlaying} disabled={!sample} />
    </main>
  </div>;
}
