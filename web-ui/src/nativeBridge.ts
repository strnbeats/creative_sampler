type JuceBackend = {
  emitEvent: (eventId: string, payload: unknown) => void;
  addEventListener: (eventId: string, callback: (payload: unknown) => void) => [string, number];
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
const pending = new Map<number, (value: unknown) => void>();
let nextPromiseId = 0;

window.__JUCE__?.backend?.addEventListener('__juce__complete', payload => {
  const { promiseId, result } = payload as Completion;
  const resolve = pending.get(promiseId);
  if (resolve) {
    pending.delete(promiseId);
    resolve(result);
  }
});

/** Mirrors JUCE's getNativeFunction protocol for WebBrowserComponent native bindings. */
export function callNative<T>(name: string, ...params: unknown[]): Promise<T> | undefined {
  const juce = window.__JUCE__;
  const backend = juce?.backend;
  if (!backend) return undefined;

  if (!juce.initialisationData?.__juce__functions.includes(name))
    console.warn(`JUCE native function '${name}' is not listed in initialisation data`);

  const promiseId = nextPromiseId++;
  return new Promise<T>(resolve => {
    pending.set(promiseId, resolve as (value: unknown) => void);
    backend.emitEvent('__juce__invoke', { name, params, resultId: promiseId });
  });
}
