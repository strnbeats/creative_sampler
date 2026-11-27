import assert from 'node:assert/strict';
import { test } from 'node:test';
import { BridgeCommandError, protocolVersion } from '../bridge/protocol.ts';
import { nativeBridge } from '../bridge/nativeBridge.ts';
import { clampSliceParameter, sliceParameterRanges, SliceParameterQueue } from '../app/sliceParameterQueue.ts';

const fields = ['volume', 'pan', 'pitch', 'attack', 'decay', 'sustain', 'release'];
const metadata = id => ({ id, revision: '1', fileName: 'test.wav', durationSeconds: 1, sampleRate: 48000,
  sampleCount: 48000, channelCount: 2, bitDepth: 24 });
const snapshot = (sampleId = '10', revision = '1', sequence = revision) => ({
  protocolVersion: 2, type: 'snapshot', sequence, app: { status: 'ready', message: null }, sample: metadata(sampleId),
  sliceRevision: revision, slices: [{ id: '20' }, { id: '21' }], analysis: null, sampleTransport: null, activeVoices: null,
});
const waitFor = async predicate => {
  for (let i = 0; i < 600; i += 1) {
    if (predicate()) return;
    await new Promise(resolve => setTimeout(resolve, 0));
  }
  assert.fail('Timed out waiting for queued parameter commands.');
};
const deferred = () => {
  let resolve;
  let reject;
  const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
};

test('native bridge sends typed versioned commands for all seven slice parameters', async () => {
  const calls = [];
  const listeners = new Map();
  const backend = {
    emitEvent(event, payload) {
      if (event !== '__juce__invoke') return;
      calls.push(payload);
      const id = payload.resultId;
      const command = payload.params[0];
      if (command.parameter === 'pan' && command.sliceId === 'missing') {
        queueMicrotask(() => listeners.get('__juce__complete')?.({ promiseId: id, result: {
          protocolVersion: 2, type: 'ack', requestId: command.requestId, ok: false,
          error: { code: 'NOT_FOUND', message: 'The slice no longer exists.', retryable: false },
        } }));
        return;
      }
      const ack = { protocolVersion: 2, type: 'ack', requestId: command.requestId, ok: true,
        result: snapshot(command.sampleId, String(Number(command.revision) + 1), String(Number(command.revision) + 1)) };
      queueMicrotask(() => listeners.get('__juce__complete')?.({ promiseId: id, result: ack }));
    },
    addEventListener(event, callback) { listeners.set(event, callback); return [event, 1]; },
    removeEventListener() {},
  };
  globalThis.window = { __JUCE__: { initialisationData: { __juce__functions: ['setUiV2SliceParameter'] }, backend },
    setTimeout, clearTimeout };

  for (const [index, parameter] of fields.entries()) {
    const ack = await nativeBridge.setSliceParameter({ sampleId: '10', revision: String(index + 1), sliceId: '20',
      parameter, value: parameter === 'sustain' ? 1 : index / 10 });
    assert.equal(ack.type, 'snapshot');
  }
  assert.equal(calls.length, 7);
  assert.deepEqual(calls.map(call => call.name), Array(7).fill('setUiV2SliceParameter'));
  calls.forEach((call, index) => {
    const command = call.params[0];
    assert.equal(command.protocolVersion, protocolVersion);
    assert.equal(command.sampleId, '10');
    assert.equal(command.revision, String(index + 1));
    assert.equal(command.sliceId, '20');
    assert.equal(command.parameter, fields[index]);
    assert.equal(command.value, command.parameter === 'sustain' ? 1 : index / 10);
    assert.match(command.requestId, /^ui2-/);
  });
  await assert.rejects(nativeBridge.setSliceParameter({ sampleId: '10', revision: '8', sliceId: 'missing',
    parameter: 'pan', value: 0 }), error => error instanceof BridgeCommandError && error.code === 'NOT_FOUND');
  delete globalThis.window;
});

test('queue serializes parameter edits and sends each command with the revision returned by the previous ACK', async () => {
  const commands = [];
  let active = 0;
  let maximumActive = 0;
  let revision = 5;
  const queue = new SliceParameterQueue({
    send: async command => {
      active += 1;
      maximumActive = Math.max(maximumActive, active);
      commands.push(command);
      await new Promise(resolve => setTimeout(resolve, 0));
      revision += 1;
      active -= 1;
      return snapshot(command.sampleId, String(revision), String(revision));
    },
    acceptSnapshot: value => value, refresh() {}, onPending() {}, onError() {},
  });
  queue.setContext({ sampleId: '10', sliceId: '20', revision: '5' });
  fields.forEach((parameter, index) => queue.enqueue(parameter, index / 10));
  await waitFor(() => commands.length === 7 && active === 0);
  assert.equal(maximumActive, 1);
  assert.deepEqual(commands.map(command => command.parameter), fields);
  assert.deepEqual(commands.map(command => command.revision), ['5', '6', '7', '8', '9', '10', '11']);
  assert.ok(commands.every(command => command.sampleId === '10' && command.sliceId === '20'));
});

test('queue coalesces rapid changes per parameter and preserves distinct parameter edits', async () => {
  const first = deferred();
  const calls = [];
  const pendingStates = [];
  const queue = new SliceParameterQueue({
    send: command => { calls.push(command); return calls.length === 1 ? first.promise : Promise.resolve(snapshot('10', '3', '3')); },
    acceptSnapshot: value => value, refresh() {}, onPending: value => pendingStates.push(value), onError() {},
  });
  queue.setContext({ sampleId: '10', sliceId: '20', revision: '1' });
  queue.enqueue('volume', .2);
  queue.enqueue('volume', .4);
  queue.enqueue('volume', .8);
  queue.enqueue('pan', -.5);
  queue.enqueue('pan', .25);
  assert.equal(calls.length, 1);
  first.resolve(snapshot('10', '2', '2'));
  await waitFor(() => calls.length === 3 && pendingStates.at(-1)?.volume === undefined
    && pendingStates.at(-1)?.pan === undefined);
  assert.deepEqual(calls.map(call => [call.parameter, call.value]), [['volume', .2], ['volume', .8], ['pan', .25]]);
  assert.deepEqual(calls.map(call => call.revision), ['1', '2', '3']);
});

test('context switch drops unsent edits and ignores old-context UI completion while accepting authoritative ACK', async () => {
  const first = deferred();
  const commands = [];
  const accepted = [];
  const pending = [];
  const queue = new SliceParameterQueue({
    send: command => { commands.push(command); return commands.length === 1 ? first.promise : Promise.resolve(snapshot('11', '9', '9')); },
    acceptSnapshot: value => { accepted.push(value); return value; }, refresh() {},
    onPending: value => pending.push(value), onError() {},
  });
  queue.setContext({ sampleId: '10', sliceId: '20', revision: '1' });
  queue.enqueue('volume', .3);
  queue.enqueue('pan', .4);
  queue.setContext({ sampleId: '11', sliceId: '21', revision: '8' });
  queue.enqueue('pitch', 3);
  first.resolve(snapshot('10', '2', '2'));
  await waitFor(() => commands.length === 2 && accepted.length >= 1);
  assert.deepEqual(commands.map(({ sampleId, sliceId, parameter }) => [sampleId, sliceId, parameter]), [
    ['10', '20', 'volume'], ['11', '21', 'pitch'],
  ]);
  assert.equal(commands[1].revision, '8');
  assert.equal(accepted[0].sample.id, '10');
  assert.deepEqual(pending.at(-1), {});
});

test('same-sample slice switch advances shared revision after old ACK without replaying old pending values', async () => {
  const first = deferred();
  const commands = [];
  const queue = new SliceParameterQueue({
    send: command => { commands.push(command); return commands.length === 1 ? first.promise : Promise.resolve(snapshot('10', '3', '3')); },
    acceptSnapshot: value => value, refresh() {}, onPending() {}, onError() {},
  });
  queue.setContext({ sampleId: '10', sliceId: '20', revision: '1' });
  queue.enqueue('volume', .3);
  queue.enqueue('pan', .4);
  queue.setContext({ sampleId: '10', sliceId: '21', revision: '1' });
  queue.enqueue('pitch', 3);
  first.resolve(snapshot('10', '2', '2'));
  await waitFor(() => commands.length === 2);
  assert.deepEqual(commands.map(({ sliceId, parameter, revision }) => [sliceId, parameter, revision]), [
    ['20', 'volume', '1'], ['21', 'pitch', '2'],
  ]);
});

test('stale command is cleared and refreshed once without retry loop', async () => {
  const commands = [];
  const errors = [];
  let refreshes = 0;
  const pending = [];
  const queue = new SliceParameterQueue({
    send: async command => { commands.push(command); throw new BridgeCommandError('STALE_SAMPLE', 'stale revision', false); },
    acceptSnapshot: value => value, refresh: () => { refreshes += 1; },
    onPending: value => pending.push(value), onError: value => errors.push(value),
  });
  queue.setContext({ sampleId: '10', sliceId: '20', revision: '1' });
  queue.enqueue('volume', .5);
  queue.enqueue('pan', .5);
  await waitFor(() => refreshes === 1 && pending.at(-1)?.volume === undefined);
  await new Promise(resolve => setTimeout(resolve, 10));
  assert.equal(commands.length, 1);
  assert.equal(refreshes, 1);
  assert.match(errors.at(-1), /^STALE_SAMPLE:/);
});

test('authoritative snapshot updates revision without overwriting pending UI values', async () => {
  const first = deferred();
  const pending = [];
  const queue = new SliceParameterQueue({
    send: () => first.promise, acceptSnapshot: value => value, refresh() {},
    onPending: value => pending.push(value), onError() {},
  });
  queue.setContext({ sampleId: '10', sliceId: '20', revision: '1' });
  queue.enqueue('volume', .4);
  queue.enqueue('volume', .9);
  queue.syncSnapshot(snapshot('10', '2', '2'));
  assert.equal(pending.at(-1).volume, .9);
  first.resolve(snapshot('10', '3', '3'));
});

test('repeating the in-flight value does not create a redundant second command', async () => {
  const first = deferred();
  const calls = [];
  const queue = new SliceParameterQueue({
    send: command => { calls.push(command); return calls.length === 1 ? first.promise : Promise.resolve(snapshot('10', '3', '3')); },
    acceptSnapshot: value => value, refresh() {}, onPending() {}, onError() {},
  });
  queue.setContext({ sampleId: '10', sliceId: '20', revision: '1' });
  queue.enqueue('volume', .5);
  queue.enqueue('volume', .7);
  queue.enqueue('volume', .5);
  first.resolve(snapshot('10', '2', '2'));
  await new Promise(resolve => setTimeout(resolve, 10));
  assert.equal(calls.length, 1);
});

test('rapid drag updates with fast ACKs are rate-limited and collapse to the final value', async () => {
  const calls = [];
  const queue = new SliceParameterQueue({
    send: async command => {
      calls.push(command);
      return snapshot(command.sampleId, String(Number(command.revision) + 1), String(Number(command.revision) + 1));
    },
    acceptSnapshot: value => value, refresh() {}, onPending() {}, onError() {},
  });
  queue.setContext({ sampleId: '10', sliceId: '20', revision: '1' });
  for (let index = 0; index < 10; index += 1) queue.enqueue('volume', .2 + index * .1);
  await waitFor(() => calls.length === 2);
  assert.deepEqual(calls.map(call => call.value), [.2, 1.1]);
  await new Promise(resolve => setTimeout(resolve, 50));
  assert.equal(calls.length, 2);
});

test('UI ranges clamp all seven parameter endpoints without percent scaling', () => {
  const expected = {
    volume: [0, 2], pan: [-1, 1], pitch: [-24, 24], attack: [0, 10], decay: [0, 10],
    sustain: [0, 1], release: [0, 10],
  };
  for (const parameter of fields) {
    const { min, max } = sliceParameterRanges[parameter];
    assert.deepEqual([min, max], expected[parameter]);
    assert.equal(clampSliceParameter(parameter, min - 1), min);
    assert.equal(clampSliceParameter(parameter, max + 1), max);
  }
  assert.equal(clampSliceParameter('sustain', 1), 1);
  assert.equal(clampSliceParameter('sustain', Number.NaN), 0);
});

test('rapid sustain boundary sequences coalesce and an older ACK never replaces the newest pending value', async () => {
  const first = deferred();
  const second = deferred();
  const calls = [];
  const pending = [];
  const queue = new SliceParameterQueue({
    send: command => {
      calls.push(command);
      return calls.length === 1 ? first.promise : second.promise;
    },
    acceptSnapshot: value => value, refresh() {}, onPending: value => pending.push(value), onError() {},
  });
  queue.setContext({ sampleId: '10', sliceId: '20', revision: '4' });
  for (const value of [0.95, 0.98, 0.99, 1]) queue.enqueue('sustain', value);
  assert.deepEqual(calls.map(command => command.value), [0.95]);
  first.resolve(snapshot('10', '5', '5'));
  await waitFor(() => calls.length === 2);
  assert.equal(pending.at(-1).sustain, 1);
  assert.deepEqual(calls.map(({ parameter, value, revision }) => [parameter, value, revision]), [
    ['sustain', 0.95, '4'], ['sustain', 1, '5'],
  ]);
  second.resolve(snapshot('10', '6', '6'));
  await waitFor(() => pending.at(-1)?.sustain === undefined);

  const secondFirst = deferred();
  const secondCalls = [];
  const secondPending = [];
  const secondQueue = new SliceParameterQueue({
    send: command => {
      secondCalls.push(command);
      return secondCalls.length === 1 ? secondFirst.promise : Promise.resolve(snapshot(command.sampleId,
        String(Number(command.revision) + 1), String(Number(command.revision) + 1)));
    },
    acceptSnapshot: value => value, refresh() {}, onPending: value => secondPending.push(value), onError() {},
  });
  secondQueue.setContext({ sampleId: '10', sliceId: '20', revision: '9' });
  secondQueue.enqueue('sustain', 0.95);
  secondQueue.enqueue('sustain', 0.99);
  secondQueue.enqueue('sustain', 1);
  assert.equal(secondPending.at(-1).sustain, 1);
  secondFirst.resolve(snapshot('10', '10', '10'));
  await waitFor(() => secondCalls.length === 2 && secondPending.at(-1)?.sustain === undefined);
  assert.deepEqual(secondCalls.map(({ value, revision }) => [value, revision]), [[0.95, '9'], [1, '10']]);

  const duplicateCalls = [];
  const duplicateQueue = new SliceParameterQueue({
    send: async command => {
      duplicateCalls.push(command);
      return snapshot(command.sampleId, String(Number(command.revision) + 1), String(Number(command.revision) + 1));
    },
    acceptSnapshot: value => value, refresh() {}, onPending() {}, onError() {},
  });
  duplicateQueue.setContext({ sampleId: '10', sliceId: '20', revision: '20' });
  duplicateQueue.enqueue('sustain', 1);
  duplicateQueue.enqueue('sustain', 0.99);
  duplicateQueue.enqueue('sustain', 1);
  await waitFor(() => duplicateCalls.length === 1);
  await new Promise(resolve => setTimeout(resolve, 10));
  assert.equal(duplicateCalls.length, 1);
});

test('idempotent native ACK keeps the same revision for the next serialized edit', async () => {
  const commands = [];
  const errors = [];
  const queue = new SliceParameterQueue({
    send: async command => {
      commands.push(command);
      const nextRevision = command.parameter === 'pan' ? String(Number(command.revision) + 1) : command.revision;
      return snapshot(command.sampleId, nextRevision, String(commands.length + 1));
    },
    acceptSnapshot: value => value, refresh() {}, onPending() {}, onError: error => errors.push(error),
  });
  queue.setContext({ sampleId: '10', sliceId: '20', revision: '4' });
  queue.enqueue('volume', 1);
  await waitFor(() => commands.length === 1);
  await new Promise(resolve => setTimeout(resolve, 10));
  queue.enqueue('volume', 1);
  await waitFor(() => commands.length === 2);
  await new Promise(resolve => setTimeout(resolve, 10));
  queue.enqueue('pan', .25);
  await waitFor(() => commands.length === 3);
  assert.deepEqual(commands.map(({ parameter, revision }) => [parameter, revision]), [
    ['volume', '4'], ['volume', '4'], ['pan', '4'],
  ]);
  assert.deepEqual(errors.filter(Boolean), []);
});

test('queue accepts a later user edit after one failed command without retrying the failed command', async () => {
  const commands = [];
  const refreshes = [];
  const errors = [];
  const queue = new SliceParameterQueue({
    send: async command => {
      commands.push(command);
      if (commands.length === 1) throw new BridgeCommandError('INTERNAL', 'test rejection', false);
      return snapshot(command.sampleId, String(Number(command.revision) + 1), '20');
    },
    acceptSnapshot: value => value, refresh: () => refreshes.push(commands.length), onPending() {},
    onError: error => errors.push(error),
  });
  queue.setContext({ sampleId: '10', sliceId: '20', revision: '7' });
  queue.enqueue('volume', .5);
  await waitFor(() => errors.filter(Boolean).length === 1 && refreshes.length === 1);
  await new Promise(resolve => setTimeout(resolve, 10));
  assert.equal(commands.length, 1);
  queue.enqueue('pan', .25);
  await waitFor(() => commands.length === 2);
  assert.deepEqual(commands.map(({ parameter, revision }) => [parameter, revision]), [
    ['volume', '7'], ['pan', '7'],
  ]);
  assert.equal(refreshes.length, 1);
  assert.equal(errors.filter(Boolean).length, 1);
});

test('slice removal in a snapshot invalidates its pending edits', async () => {
  const first = deferred();
  const calls = [];
  const queue = new SliceParameterQueue({
    send: command => { calls.push(command); return first.promise; },
    acceptSnapshot: value => value, refresh() {}, onPending() {}, onError() {},
  });
  queue.setContext({ sampleId: '10', sliceId: '20', revision: '1' });
  queue.enqueue('volume', .5);
  queue.enqueue('pan', .25);
  queue.syncSnapshot({ ...snapshot('10', '2', '2'), slices: [{ id: '21' }] });
  first.resolve({ ...snapshot('10', '3', '3'), slices: [{ id: '21' }] });
  await new Promise(resolve => setTimeout(resolve, 10));
  assert.equal(calls.length, 1);
});
