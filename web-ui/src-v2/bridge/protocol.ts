export type Id64 = string;
export type SampleId = Id64;
export type SliceId = Id64;
export type Revision = Id64;
export type RequestId = string;
export const protocolVersion = 2 as const;

export type BridgeErrorCode = 'UNSUPPORTED_PROTOCOL' | 'INVALID_ARGUMENT' | 'NO_SAMPLE'
  | 'STALE_SAMPLE' | 'NOT_FOUND' | 'INTERNAL';

export type BridgeError = {
  code: BridgeErrorCode;
  message: string;
  retryable: boolean;
};

export type CommandAck<T> =
  | { protocolVersion: typeof protocolVersion; type: 'ack'; requestId: RequestId; ok: true; result: T }
  | { protocolVersion: typeof protocolVersion; type: 'ack'; requestId: RequestId; ok: false; error: BridgeError };

export type SampleMetadataDto = {
  id: SampleId;
  revision: Revision;
  fileName: string;
  durationSeconds: number;
  sampleRate: number;
  sampleCount: number;
  channelCount: number;
  bitDepth: number | null;
};

export type SliceParametersDto = {
  volume: number;
  pan: number;
  pitchSemitones: number;
  envelope: { attackSeconds: number; decaySeconds: number; sustainLevel: number; releaseSeconds: number };
};

export type SliceDto = {
  id: SliceId;
  startSample: number;
  endSample: number;
  midiNote: number;
  parameters: SliceParametersDto;
};

export type AnalysisDto = {
  sampleId: SampleId;
  generation: Revision;
  status: 'notAnalyzed' | 'running' | 'completed' | 'failed' | 'cancelled';
  bpm: null | { value: number | null; confidence: number; score: number; detectionStatus: string };
  key: null | { tonic: string | null; mode: 'major' | 'minor' | null; confidence: number; score: number; detectionStatus: string };
  transients: null | {
    sampleRate: number;
    sampleCount: number;
    totalEventCount: number;
    truncated: boolean;
    positions: Array<{ samplePosition: number; strength: number; confidence: number | null }>;
  };
  error: string | null;
};

export type NativeSnapshot = {
  protocolVersion: typeof protocolVersion;
  type: 'snapshot';
  sequence: Revision;
  app: { status: 'ready' | 'loading' | 'error'; message: string | null };
  sample: SampleMetadataDto | null;
  sliceRevision: Revision | null;
  slices: SliceDto[];
  analysis: AnalysisDto | null;
  sampleTransport: { status: 'stopped' | 'playing'; positionSamples: number } | null;
  activeVoices: null;
};

export type WaveformDto = {
  protocolVersion: typeof protocolVersion;
  type: 'waveform';
  requestId: RequestId;
  sampleId: SampleId;
  framesPerPeak: number;
  channels: Array<Array<{ min: number; max: number }>>;
};

export type TransportDto = {
  protocolVersion: typeof protocolVersion;
  type: 'transport';
  requestId: RequestId;
  sampleId: SampleId | null;
  status: 'stopped' | 'playing';
  positionSamples: number;
};

export type TransportUpdateDto = {
  protocolVersion: typeof protocolVersion;
  type: 'transportUpdate';
  sequence: Revision;
  sampleId: SampleId | null;
  status: 'stopped' | 'playing';
  positionSamples: number;
};

export type ViewSample = {
  id: SampleId;
  name: string;
  durationSeconds: number;
  sampleRate: number;
  sampleCount: number;
  channelCount: number;
};

export type ViewSlice = {
  id: SliceId;
  index: number;
  name: string;
  note: string;
  midiNote: number;
  startSample: number;
  endSample: number;
  volume: number;
  pan: number;
  pitch: number;
  attackSeconds: number;
  decaySeconds: number;
  sustain: number;
  releaseSeconds: number;
};

export type ViewTransient = { id: string; samplePosition: number; strength: number };
