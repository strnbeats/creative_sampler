import { AudioLines, ArrowUpRight } from 'lucide-react';
import type { AnalysisDto, SampleMetadataDto } from '../bridge/protocol';

const analysisStates: Record<AnalysisDto['status'], string> = {
  notAnalyzed: 'Awaiting analysis',
  running: 'Analyzing',
  completed: 'Analysis complete',
  failed: 'Analysis failed',
  cancelled: 'Analysis cancelled',
};

export function SourceHeader({ sample, analysis, loading, onImport, importDisabled, message, onRetry }: {
  sample: SampleMetadataDto | null; analysis: AnalysisDto | null; loading: boolean;
  onImport: () => void; importDisabled: boolean; message: string | null; onRetry?: () => void;
}) {
  const bpm = analysis?.status === 'completed' ? analysis.bpm?.value : null;
  const key = analysis?.status === 'completed' && analysis.key?.tonic && analysis.key.mode
    ? `${analysis.key.tonic} ${analysis.key.mode === 'minor' ? 'min' : 'maj'}` : null;
  const channels = sample?.channelCount === 1 ? 'Mono' : sample?.channelCount === 2 ? 'Stereo' : `${sample?.channelCount} ch`;

  return <header className="source-header">
    <div className="rail">
      <div className="brand"><AudioLines size={20} aria-hidden="true" /> Creative Sampler</div>
      {sample && <button className="import-action" type="button" onClick={onImport} disabled={importDisabled}>
        Import sample <ArrowUpRight size={14} aria-hidden="true" />
      </button>}
    </div>
    <div className="sample-header">
      <div className="sample-ident">
        <p>{sample
          ? <>{sample.durationSeconds.toFixed(2)} s <i /> {sample.sampleRate / 1000} kHz <i /> {channels}{sample.bitDepth ? <> <i /> {sample.bitDepth}-bit</> : null}</>
          : loading ? 'Reading audio' : 'Source sample'}</p>
        <h1 title={sample?.fileName}>{sample?.fileName ?? (loading ? 'Importing…' : 'Sample input')}</h1>
      </div>
      {sample && <div className="source-analysis" aria-label="Sample analysis">
        <dl className="analysis">
          <div><dt>Tempo</dt><dd>{bpm != null ? <>{Number(bpm.toFixed(1))} <small>BPM</small></> : '—'}</dd></div>
          <div><dt>Key</dt><dd>{key ?? '—'}</dd></div>
        </dl>
        <span className="analysis-status" title={analysis?.error ?? undefined} role="status">
          {analysis ? analysisStates[analysis.status] : 'Awaiting analysis'}
        </span>
      </div>}
    </div>
    {message && <div className="connection-status" role="status">
      <span>{message}</span>{onRetry && <button className="import-action" type="button" onClick={onRetry}>Retry</button>}
    </div>}
  </header>;
}
