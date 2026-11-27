import assert from 'node:assert/strict';
import { test } from 'node:test';
import { hitSlice, resolveSlice, sliceKey, sliceTime } from '../app/sliceInteraction.ts';

const bounds = (id, startSample, endSample) => ({ id, startSample, endSample });
const slices = [
  bounds('9007199254740993', 0, 400),
  bounds('9007199254740994', 400, 423),
  bounds('9007199254740995', 423, 452),
  bounds('9007199254740996', 452, 800),
  bounds('9007199254740997', 820, 1000),
];

test('selects by exact string ID without relying on slice position', () => {
  const selected = resolveSlice(slices, '9007199254740995');
  assert.equal(selected, slices[2]);
  assert.equal(resolveSlice([slices[2], slices[3]], selected.id), selected);
});

test('clears deleted IDs and defaults new workspaces to their first slice', () => {
  const replacement = [bounds('9007199254740998', 0, 1000)];
  assert.equal(resolveSlice(replacement, slices[2].id), replacement[0]);
  assert.equal(resolveSlice(replacement, null), replacement[0]);
  assert.equal(resolveSlice([], slices[2].id), null);
});

test('uses half-open boundaries, guards both sides of a cut and ignores gaps', () => {
  assert.deepEqual(hitSlice(slices, 1000, 200, 1000), { id: slices[0].id, action: 'select' });
  assert.deepEqual(hitSlice(slices, 1000, 399.9, 1000), { id: slices[0].id, action: 'focus' });
  assert.deepEqual(hitSlice(slices, 1000, 400, 1000), { id: slices[1].id, action: 'focus' });
  assert.deepEqual(hitSlice(slices, 1000, 451.9, 1000), { id: slices[2].id, action: 'focus' });
  assert.deepEqual(hitSlice(slices, 1000, 452.1, 1000), { id: slices[3].id, action: 'focus' });
  assert.equal(hitSlice(slices, 1000, 810, 1000), null);
  assert.equal(hitSlice(slices, 1000, 1000, 1000), null);
});

test('uses current plot width after resize and never expands a narrow interval', () => {
  for (const width of [1440, 900, 680]) {
    assert.deepEqual(hitSlice(slices, 1000, .438 * width, width),
      { id: slices[2].id, action: width === 680 ? 'focus' : 'select' });
    assert.deepEqual(hitSlice(slices, 1000, .6 * width, width), { id: slices[3].id, action: 'select' });
  }
  assert.deepEqual(hitSlice(slices, 1000, 411, 1000), { id: slices[1].id, action: 'focus' });
});

test('dense or overlapping ranges do not select an ambiguous neighbor', () => {
  const dense = Array.from({ length: 48 }, (_, i) => bounds(String(i + 1), i * 10, (i + 1) * 10));
  assert.deepEqual(hitSlice(dense, 480, 335, 680), { id: '24', action: 'focus' });
  const overlapping = [bounds('a', 0, 250), bounds('b', 100, 300)];
  assert.deepEqual(hitSlice(overlapping, 300, 150, 300), { id: 'a', action: 'focus' });
});

test('arrow keys move focus while Enter and Space confirm the focused index', () => {
  assert.deepEqual(sliceKey('ArrowRight', 1, 5), { focus: 2 });
  assert.deepEqual(sliceKey('ArrowRight', 4, 5), { focus: 4 });
  assert.deepEqual(sliceKey('Home', 3, 5), { focus: 0 });
  assert.deepEqual(sliceKey('End', 1, 5), { focus: 4 });
  assert.deepEqual(sliceKey('Enter', 2, 5), { select: 2 });
  assert.deepEqual(sliceKey(' ', 2, 5), { select: 2 });
  assert.equal(sliceKey('Tab', 2, 5), null);
});

test('readout distinguishes adjacent frames even at high sample rates', () => {
  assert.notEqual(sliceTime(12001, 192000), sliceTime(12002, 192000));
});
