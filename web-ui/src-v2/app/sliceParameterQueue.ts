import { protocolVersion, type NativeSnapshot, type SampleId, type SliceId, type SliceParameterId, type SetSliceParameterRequest, type Revision } from '../bridge/protocol.ts';

export type SliceParameterValues = Partial<Record<SliceParameterId, number>>;
export type SliceParameterContext = { sampleId: SampleId; sliceId: SliceId; revision: Revision };
export type SliceParameterCommand = Omit<SetSliceParameterRequest, 'requestId'>;
const minDispatchIntervalMs = 40;

export const sliceParameterRanges: Record<SliceParameterId, { min: number; max: number; step: number }> = {
  volume: { min: 0, max: 2, step: 0.01 },
  pan: { min: -1, max: 1, step: 0.01 },
  pitch: { min: -24, max: 24, step: 0.1 },
  attack: { min: 0, max: 10, step: 0.01 },
  decay: { min: 0, max: 10, step: 0.01 },
  sustain: { min: 0, max: 1, step: 0.01 },
  release: { min: 0, max: 10, step: 0.01 },
};

export function clampSliceParameter(parameter: SliceParameterId, value: number) {
  const { min, max } = sliceParameterRanges[parameter];
  return Math.max(min, Math.min(max, Number.isFinite(value) ? value : min));
}

type QueueOptions = {
  send: (command: SliceParameterCommand) => Promise<NativeSnapshot>;
  acceptSnapshot: (snapshot: NativeSnapshot) => NativeSnapshot;
  refresh: () => void;
  onPending: (values: SliceParameterValues) => void;
  onError: (message: string | null) => void;
};

/** Serializes commands because each accepted change advances the shared slice revision. */
export class SliceParameterQueue {
  private readonly options: QueueOptions;
  private context: SliceParameterContext | null = null;
  private generation = 0;
  private busy = false;
  private inFlight: { parameter: SliceParameterId; value: number } | null = null;
  private lastDispatchAt = 0;
  private dispatchTimer: ReturnType<typeof setTimeout> | null = null;
  private disposed = false;
  private readonly queued = new Map<SliceParameterId, number>();
  private readonly pending: SliceParameterValues = {};

  constructor(options: QueueOptions) { this.options = options; }

  setContext(context: SliceParameterContext | null) {
    const previous = this.context;
    if (previous?.sampleId !== context?.sampleId || previous?.sliceId !== context?.sliceId) {
      this.generation += 1;
      this.queued.clear();
      for (const key of Object.keys(this.pending) as SliceParameterId[]) delete this.pending[key];
      this.options.onPending({});
    }
    this.context = context ? { ...context } : null;
    this.disposed = false;
    this.pump();
  }

  /** Update the shared revision without clearing edits for the active context. */
  syncSnapshot(snapshot: NativeSnapshot) {
    if (!this.context || !snapshot.sample || this.context.sampleId !== snapshot.sample.id) return;
    if (!snapshot.slices.some(slice => slice.id === this.context?.sliceId)) {
      this.setContext(null);
      return;
    }
    if (snapshot.sliceRevision !== null)
      this.context = { ...this.context, revision: snapshot.sliceRevision };
  }

  enqueue(parameter: SliceParameterId, value: number) {
    if (!this.context || !Number.isFinite(value) || this.disposed) return false;
    const next = clampSliceParameter(parameter, value);
    this.pending[parameter] = next;
    if (this.inFlight?.parameter === parameter && this.inFlight.value === next) this.queued.delete(parameter);
    else this.queued.set(parameter, next);
    this.options.onPending({ ...this.pending });
    this.options.onError(null);
    this.pump();
    return true;
  }

  dispose() {
    this.disposed = true;
    this.generation += 1;
    this.context = null;
    this.queued.clear();
    if (this.dispatchTimer !== null) clearTimeout(this.dispatchTimer);
    this.dispatchTimer = null;
    for (const key of Object.keys(this.pending) as SliceParameterId[]) delete this.pending[key];
  }

  private pump() {
    if (this.busy || this.disposed || !this.context || this.queued.size === 0) return;
    const wait = minDispatchIntervalMs - (Date.now() - this.lastDispatchAt);
    if (this.lastDispatchAt > 0 && wait > 0) {
      if (this.dispatchTimer === null) this.dispatchTimer = setTimeout(() => {
        this.dispatchTimer = null;
        this.pump();
      }, wait);
      return;
    }
    if (this.dispatchTimer !== null) clearTimeout(this.dispatchTimer);
    this.dispatchTimer = null;
    const [parameter, value] = this.queued.entries().next().value as [SliceParameterId, number];
    this.queued.delete(parameter);
    const context = { ...this.context };
    const generation = this.generation;
    this.busy = true;
    this.lastDispatchAt = Date.now();
    this.inFlight = { parameter, value };
    void this.options.send({ protocolVersion, ...context, parameter, value })
      .then(snapshot => {
        // The ACK is authoritative even if the user has since changed selection.
        const latest = this.options.acceptSnapshot(snapshot);
        this.syncSnapshot(latest);
        if (generation !== this.generation) return;
        if (this.pending[parameter] === value) {
          delete this.pending[parameter];
          this.options.onPending({ ...this.pending });
        }
      })
      .catch(error => {
        const message = error instanceof Error ? error.message : String(error);
        if (generation === this.generation) {
          this.queued.clear();
          for (const key of Object.keys(this.pending) as SliceParameterId[]) delete this.pending[key];
          this.options.onPending({});
          this.options.onError(message);
        }
        // Failed commands are never retried. Refresh once to restore authoritative state.
        this.options.refresh();
      })
      .finally(() => {
        this.busy = false;
        this.inFlight = null;
        this.pump();
      });
  }
}
