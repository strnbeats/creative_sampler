import { BridgeCommandError, protocolVersion, type CommandAck, type NativeSnapshot, type RequestId, type SampleId, type SetSliceParameterRequest, type TransportAction, type TransportCommandResult, type TransportDto, type WaveformDto } from './protocol.ts';

type JuceBackend = {
  emitEvent: (eventId: string, payload: unknown) => void;
  addEventListener: (eventId: string, callback: (payload: unknown) => void) => [string, number];
  removeEventListener: (token: [string, number]) => void;
};

declare global {
  interface Window {
    __JUCE__?: {
      initialisationData?: { __juce__functions: string[] };
      backend?: JuceBackend;
    };
  }
}

type Completion = { promiseId: number; result: unknown };
const pending = new Map<number, { resolve: (value: unknown) => void; reject: (error: Error) => void; timer: number }>();
let nextPromiseId = 0;
let nextRequestId = 0;
let listenerInstalled = false;

function installCompletionListener() {
  if (listenerInstalled) return;
  const backend = window.__JUCE__?.backend;
  if (!backend) return;
  listenerInstalled = true;
  backend.addEventListener('__juce__complete', payload => {
    const completion = payload as Completion;
    const entry = pending.get(completion.promiseId);
    if (!entry) return;
    window.clearTimeout(entry.timer);
    pending.delete(completion.promiseId);
    entry.resolve(completion.result);
  });
}

function requestId(): RequestId {
  nextRequestId += 1;
  return `ui2-${Date.now()}-${nextRequestId}`;
}

function invoke<T>(name: string, payload: Record<string, unknown>): Promise<T> {
  const juce = window.__JUCE__;
  const backend = juce?.backend;
  if (!backend) return Promise.reject(new Error('Creative Sampler bridge is unavailable.'));
  if (!juce.initialisationData?.__juce__functions.includes(name))
    return Promise.reject(new Error(`Native function '${name}' is not registered.`));
  installCompletionListener();
  const promiseId = nextPromiseId++;
  return new Promise<T>((resolve, reject) => {
    const timer = window.setTimeout(() => {
      pending.delete(promiseId);
      reject(new Error(`Native function '${name}' timed out.`));
    }, 5000);
    pending.set(promiseId, { resolve: resolve as (value: unknown) => void, reject, timer });
    backend.emitEvent('__juce__invoke', { name, params: [payload], resultId: promiseId });
  });
}

async function unwrap<T>(name: string, id: RequestId, response: CommandAck<T>): Promise<T> {
  if (response?.protocolVersion !== protocolVersion || response.type !== 'ack' || response.requestId !== id)
    throw new Error(`Invalid response from '${name}'.`);
  if (!response.ok) throw new BridgeCommandError(response.error.code, response.error.message, response.error.retryable);
  return response.result;
}

export const nativeBridge = {
  subscribe(eventId: string, callback: (payload: unknown) => void) {
    const backend = window.__JUCE__?.backend;
    if (!backend) return () => undefined;
    const token = backend.addEventListener(eventId, callback);
    return () => backend.removeEventListener(token);
  },
  async getSnapshot(): Promise<NativeSnapshot> {
    const id = requestId();
    const ack = await invoke<CommandAck<NativeSnapshot>>('getUiV2Snapshot', { protocolVersion, requestId: id });
    return unwrap('getUiV2Snapshot', id, ack);
  },
  async getWaveform(sampleId: SampleId, maxBuckets: number): Promise<WaveformDto> {
    const id = requestId();
    const ack = await invoke<CommandAck<WaveformDto>>('getUiV2Waveform', {
      protocolVersion, requestId: id, sampleId, maxBuckets,
    });
    return unwrap('getUiV2Waveform', id, ack);
  },
  async importSample(): Promise<void> {
    const id = requestId();
    const ack = await invoke<CommandAck<{ accepted: boolean }>>('importSampleV2', { protocolVersion, requestId: id });
    await unwrap('importSampleV2', id, ack);
  },
  async getTransport(): Promise<TransportDto> {
    const id = requestId();
    const ack = await invoke<CommandAck<TransportDto>>('getUiV2Transport', { protocolVersion, requestId: id });
    return unwrap('getUiV2Transport', id, ack);
  },
  async controlTransport(action: TransportAction, sampleId: SampleId): Promise<TransportCommandResult> {
    const id = requestId();
    const ack = await invoke<CommandAck<TransportCommandResult>>('controlUiV2Transport', {
      protocolVersion, requestId: id, action, sampleId,
    });
    return unwrap('controlUiV2Transport', id, ack);
  },
  async setSliceParameter(command: Omit<SetSliceParameterRequest, 'protocolVersion' | 'requestId'>): Promise<NativeSnapshot> {
    const id = requestId();
    const payload: SetSliceParameterRequest = { protocolVersion, requestId: id, ...command };
    const ack = await invoke<CommandAck<NativeSnapshot>>('setUiV2SliceParameter', payload);
    return unwrap('setUiV2SliceParameter', id, ack);
  },
};
