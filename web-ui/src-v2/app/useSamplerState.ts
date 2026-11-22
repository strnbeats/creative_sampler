import { useCallback, useEffect, useRef, useState } from 'react';
import { nativeBridge } from '../bridge/nativeBridge';
import type { NativeSnapshot, TransportDto, TransportUpdateDto, WaveformDto } from '../bridge/protocol';

type BridgeViewState = {
  connection: 'connecting' | 'connected' | 'error';
  snapshot: NativeSnapshot | null;
  waveform: WaveformDto | null;
  transport: TransportDto | null;
  error: string | null;
};

const initialState: BridgeViewState = { connection: 'connecting', snapshot: null, waveform: null, transport: null, error: null };

function validateSnapshot(value: NativeSnapshot) {
  return value?.protocolVersion === 2 && value.type === 'snapshot'
    && typeof value.sequence === 'string' && /^\d+$/.test(value.sequence)
    && Array.isArray(value.slices)
    && (value.sample === null || (typeof value.sample.id === 'string'
      && Number.isFinite(value.sample.sampleRate) && Number.isFinite(value.sample.sampleCount)))
    && value.slices.every(slice => typeof slice.id === 'string'
      && Number.isSafeInteger(slice.startSample) && Number.isSafeInteger(slice.endSample)
      && slice.startSample >= 0 && slice.endSample > slice.startSample);
}

function requestedBucketCount() {
  const width = Math.max(320, window.innerWidth - 48);
  return Math.min(2048, Math.max(256, Math.ceil(width * Math.min(window.devicePixelRatio || 1, 2))));
}

export function useSamplerState() {
  const [state, setState] = useState(initialState);
  const latestSnapshotRequest = useRef(0);
  const latestTransportSequence = useRef('0');
  const refreshNow = useRef<() => void>(() => undefined);

  useEffect(() => {
    let alive = true;
    const refresh = async () => {
      const request = ++latestSnapshotRequest.current;
      try {
        const snapshot = await nativeBridge.getSnapshot();
        if (!validateSnapshot(snapshot)) throw new Error('The native snapshot did not match protocol v2.');
        if (!alive || request !== latestSnapshotRequest.current) return;
        setState(previous => {
          if (previous.snapshot && BigInt(snapshot.sequence) <= BigInt(previous.snapshot.sequence)) return previous;
          return {
            ...previous,
            connection: 'connected',
            snapshot,
            waveform: snapshot.sample?.id === previous.waveform?.sampleId ? previous.waveform : null,
            transport: snapshot.sampleTransport && snapshot.sample
              ? { protocolVersion: 2, type: 'transport', requestId: 'snapshot', sampleId: snapshot.sample.id,
                  status: snapshot.sampleTransport.status, positionSamples: snapshot.sampleTransport.positionSamples }
              : null,
            error: null,
          };
        });
      } catch (error) {
        if (!alive || request !== latestSnapshotRequest.current) return;
        setState(previous => ({ ...previous, connection: 'error', error: error instanceof Error ? error.message : String(error) }));
      }
    };

    const handleSnapshotEvent = (value: unknown) => {
      const snapshot = value as NativeSnapshot;
      if (!validateSnapshot(snapshot)) return;
      setState(previous => {
        if (previous.snapshot && BigInt(snapshot.sequence) <= BigInt(previous.snapshot.sequence)) return previous;
        return {
          ...previous, connection: 'connected', snapshot,
          waveform: snapshot.sample?.id === previous.waveform?.sampleId ? previous.waveform : null,
          transport: snapshot.sampleTransport && snapshot.sample
            ? { protocolVersion: 2, type: 'transport', requestId: 'event', sampleId: snapshot.sample.id,
                status: snapshot.sampleTransport.status, positionSamples: snapshot.sampleTransport.positionSamples }
            : null,
          error: null,
        };
      });
    };
    const offSnapshot = nativeBridge.subscribe('samplerStateSnapshot', handleSnapshotEvent);
    refreshNow.current = () => { void refresh(); };
    void refresh();
    const interval = window.setInterval(() => void refresh(), 5000);
    const handleVisibility = () => { if (document.visibilityState === 'visible') void refresh(); };
    window.addEventListener('focus', handleVisibility);
    document.addEventListener('visibilitychange', handleVisibility);
    return () => {
      alive = false;
      window.clearInterval(interval);
      window.removeEventListener('focus', handleVisibility);
      document.removeEventListener('visibilitychange', handleVisibility);
      offSnapshot();
      refreshNow.current = () => undefined;
    };
  }, []);

  const sampleId = state.snapshot?.sample?.id ?? null;
  useEffect(() => {
    let alive = true;
    if (!sampleId) {
      setState(previous => ({ ...previous, waveform: null }));
      return () => { alive = false; };
    }
    void nativeBridge.getWaveform(sampleId, requestedBucketCount()).then(waveform => {
      if (!alive || waveform.sampleId !== sampleId) return;
      setState(previous => previous.snapshot?.sample?.id === sampleId ? { ...previous, waveform } : previous);
    }).catch(error => {
      if (!alive) return;
      setState(previous => previous.snapshot?.sample?.id === sampleId
        ? { ...previous, error: error instanceof Error ? error.message : String(error) } : previous);
    });
    return () => { alive = false; };
  }, [sampleId]);

  useEffect(() => nativeBridge.subscribe('samplerTransportUpdate', value => {
    const transport = value as TransportUpdateDto;
    if (transport.protocolVersion !== 2 || transport.type !== 'transportUpdate'
        || !/^\d+$/.test(transport.sequence) || BigInt(transport.sequence) <= BigInt(latestTransportSequence.current)) return;
    latestTransportSequence.current = transport.sequence;
    setState(previous => previous.snapshot?.sample?.id === transport.sampleId
      ? { ...previous, transport: { ...transport, requestId: 'event', type: 'transport' } }
      : previous);
  }), []);

  const retry = useCallback(() => {
    setState(previous => ({ ...previous, connection: 'connecting', error: null }));
    refreshNow.current();
  }, []);

  return { ...state, retry };
}
