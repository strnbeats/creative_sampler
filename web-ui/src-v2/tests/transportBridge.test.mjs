import assert from 'node:assert/strict';
import { test } from 'node:test';
import { protocolVersion } from '../bridge/protocol.ts';
import { nativeBridge } from '../bridge/nativeBridge.ts';

test('transport sends versioned play and stop commands and validates their ACKs', async () => {
  const calls = [];
  const listeners = new Map();
  const backend = {
    emitEvent(event, payload) {
      if (event !== '__juce__invoke') return;
      calls.push(payload);
      const command = payload.params[0];
      queueMicrotask(() => listeners.get('__juce__complete')?.({
        promiseId: payload.resultId,
        result: { protocolVersion: 2, type: 'ack', requestId: command.requestId, ok: true,
          result: { protocolVersion: 2, type: 'transportCommand', sampleId: command.sampleId, action: command.action } },
      }));
    },
    addEventListener(event, callback) { listeners.set(event, callback); return [event, 1]; },
    removeEventListener() {},
  };
  globalThis.window = {
    __JUCE__: { initialisationData: { __juce__functions: ['controlUiV2Transport'] }, backend },
    setTimeout, clearTimeout,
  };

  const play = await nativeBridge.controlTransport('play', '18446744073709551615');
  const stop = await nativeBridge.controlTransport('stop', '18446744073709551615');
  assert.equal(play.type, 'transportCommand');
  assert.equal(play.action, 'play');
  assert.equal(stop.action, 'stop');
  assert.equal(calls.length, 2);
  for (const [index, action] of ['play', 'stop'].entries()) {
    const command = calls[index].params[0];
    assert.equal(calls[index].name, 'controlUiV2Transport');
    assert.equal(command.protocolVersion, protocolVersion);
    assert.equal(command.action, action);
    assert.equal(command.sampleId, '18446744073709551615');
    assert.match(command.requestId, /^ui2-/);
  }
  delete globalThis.window;
});
