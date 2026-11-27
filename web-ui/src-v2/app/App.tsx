import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { nativeBridge } from '../bridge/nativeBridge';
import type { SliceId, TransportAction } from '../bridge/protocol';
import { Inspector } from './Inspector';
import { SourceHeader } from './SourceHeader';
import { Transport } from './Transport';
import { Waveform, type AmplitudeMode } from './Waveform';
import { useSamplerState } from './useSamplerState';
import { resolveSlice } from './sliceInteraction';
import { SliceParameterQueue, type SliceParameterValues } from './sliceParameterQueue';
import './app.css';

export function App() {
  const integration = useSamplerState();
  const snapshot = integration.snapshot;
  const loading = snapshot?.app.status === 'loading';
  const sample = loading ? null : snapshot?.sample ?? null;
  const sampleId = sample?.id;
  const sampleCount = sample?.sampleCount ?? 0;
  const analysis = sample && snapshot?.analysis?.sampleId === sampleId ? snapshot?.analysis ?? null : null;
  const [mode, setMode] = useState<AmplitudeMode>('linear');
  const [importError, setImportError] = useState<string | null>(null);
  const [transportBusy, setTransportBusy] = useState(false);
  const [transportError, setTransportError] = useState<string | null>(null);
  const activeSampleId = useRef(sampleId ?? null);
  activeSampleId.current = sampleId ?? null;
  useEffect(() => {
    setTransportError(null);
    setTransportBusy(false);
  }, [sampleId]);

  const slices = useMemo(() => {
    if (!sampleId || !snapshot?.slices) return [];
    return snapshot.slices.filter(slice => slice.endSample <= sampleCount)
      .sort((left, right) => left.startSample - right.startSample);
  }, [sampleId, sampleCount, snapshot?.slices]);

  const transientData = analysis?.status === 'completed' ? analysis.transients : null;
  const waveform = sample && integration.waveform?.sampleId === sampleId ? integration.waveform : null;
  const transport = sample
    ? integration.transport?.sampleId === sampleId ? integration.transport : snapshot?.sampleTransport ?? null
    : null;
  const bridgeUnavailable = integration.connection !== 'connected';
  const importDisabled = bridgeUnavailable || loading;
  const nativeImportError = snapshot?.app.status === 'error' ? snapshot.app.message : null;
  const message = importError ?? nativeImportError ?? (bridgeUnavailable ? integration.error : null);

  const importSample = async () => {
    setImportError(null);
    try {
      await nativeBridge.importSample();
    } catch (error) {
      setImportError(error instanceof Error ? error.message : String(error));
    }
  };
  const onImport = () => void importSample();
  const onTransportAction = async (action: TransportAction) => {
    if (!sample || transportBusy) return;
    const requestedSampleId = sample.id;
    setTransportBusy(true);
    setTransportError(null);
    try {
      await nativeBridge.controlTransport(action, requestedSampleId);
      integration.refreshSnapshot();
    } catch (error) {
      if (activeSampleId.current === requestedSampleId)
        setTransportError(error instanceof Error ? error.message : String(error));
      integration.refreshSnapshot();
    } finally {
      if (activeSampleId.current === requestedSampleId) setTransportBusy(false);
    }
  };

  return <main className={sample ? 'lithic' : 'lithic is-empty'}>
    <SourceHeader sample={sample} analysis={analysis} loading={loading} onImport={onImport}
      importDisabled={importDisabled} message={message} onRetry={bridgeUnavailable && message ? integration.retry : undefined} />

    {sample ? <SampleWorkspace key={sample.id} sample={sample} waveform={waveform} slices={slices}
      transients={transientData} transport={transport} mode={mode} setMode={setMode}
      waveformError={!waveform && integration.connection === 'connected' ? integration.error : null}
      snapshot={snapshot!} acceptSnapshot={integration.acceptSnapshot} refreshSnapshot={integration.refreshSnapshot} />
      : <section className="plate empty-plate" aria-label="Source waveform">
        <div className="plate-head"><span className="plate-label"><i className="copper-notch" />Source waveform</span></div>
        <div className="empty-source">
          <span className="empty-marker" aria-hidden="true" />
          <strong>{loading ? 'Preparing sample' : 'No sample loaded'}</strong>
          <p>{loading ? 'Reading audio and building the waveform…'
            : bridgeUnavailable ? 'Waiting for the sampler engine.' : 'Import an audio file to begin.'}</p>
          {!importDisabled && <button className="import-action" onClick={onImport}>Import sample</button>}
        </div>
      </section>}

    <Transport sample={sample} transport={transport} busy={transportBusy} error={transportError}
      onAction={onTransportAction} />
  </main>;
}

// Both the waveform and the inspector use the same resolved ID. A new sample mounts a new workspace.
type WorkspaceProps = Omit<React.ComponentProps<typeof Waveform>, 'selected' | 'select'> & {
  snapshot: NonNullable<ReturnType<typeof useSamplerState>['snapshot']>;
  acceptSnapshot: ReturnType<typeof useSamplerState>['acceptSnapshot'];
  refreshSnapshot: ReturnType<typeof useSamplerState>['refreshSnapshot'];
};

function SampleWorkspace({ snapshot, acceptSnapshot, refreshSnapshot, ...props }: WorkspaceProps) {
  const [selection, setSelection] = useState<SliceId | null>(null);
  const [pendingValues, setPendingValues] = useState<SliceParameterValues>({});
  const [pendingContext, setPendingContext] = useState<string | null>(null);
  const [parameterError, setParameterError] = useState<string | null>(null);
  const selected = resolveSlice(props.slices, selection);
  const contextKey = `${snapshot.sample?.id ?? ''}:${selected?.id ?? ''}`;
  const visiblePending = pendingContext === contextKey ? pendingValues : {};
  if (selection !== null && selection !== selected?.id) setSelection(null);

  const queue = useMemo(() => new SliceParameterQueue({
    send: command => nativeBridge.setSliceParameter(command),
    acceptSnapshot,
    refresh: refreshSnapshot,
    onPending: setPendingValues,
    onError: setParameterError,
  }), [acceptSnapshot, refreshSnapshot]);

  useEffect(() => {
    setParameterError(null);
    queue.setContext(selected && snapshot.sliceRevision !== null
      ? { sampleId: snapshot.sample!.id, sliceId: selected.id, revision: snapshot.sliceRevision }
      : null);
    return () => queue.dispose();
  }, [queue, snapshot.sample?.id, selected?.id]);
  useEffect(() => {
    queue.syncSnapshot(snapshot);
  }, [queue, snapshot]);

  const select = useCallback((id: SliceId) => setSelection(id), []);
  return <>
    <Waveform {...props} selected={selected} select={select} />
    <Inspector key={selected?.id} slice={selected} pendingValues={visiblePending}
      onParameterChange={(parameter, value) => {
        setPendingContext(contextKey);
        queue.enqueue(parameter, value);
      }} error={parameterError} />
  </>;
}
