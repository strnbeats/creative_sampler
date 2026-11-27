import type { NativeSnapshot, SampleMetadataDto, TransportAction } from '../bridge/protocol';

export function Transport({ sample, transport, busy, error, onAction }: {
  sample: SampleMetadataDto | null; transport: NativeSnapshot['sampleTransport'];
  busy: boolean; error: string | null; onAction: (action: TransportAction) => void;
}) {
  const position = sample && transport && sample.sampleRate > 0 ? transport.positionSamples / sample.sampleRate : null;
  const playing = transport?.status === 'playing';
  return <footer className="status" aria-label="Sample playback state">
    <div className="transport">
      <i className={playing ? 'is-playing' : ''} aria-hidden="true" />
      <span>{!sample ? 'No sample' : !transport ? 'Playback status unavailable' : playing ? 'Playing' : 'Stopped'}</span>
      {position !== null && sample && <span className="timecode">
        <strong>{position.toFixed(2)}</strong> / {sample.durationSeconds.toFixed(2)} s
      </span>}
    </div>
    <div className="transport-actions">
      <button className="transport-control is-play" type="button" onClick={() => onAction('play')}
        disabled={!sample || busy} aria-label="Play sample" title="Play sample">▶ <span>PLAY</span></button>
      <button className="transport-control" type="button" onClick={() => onAction('stop')}
        disabled={!sample || busy || !playing} aria-label="Stop sample" title="Stop sample">■ <span>STOP</span></button>
      {error && <span className="transport-error" role="alert">{error}</span>}
    </div>
  </footer>;
}
