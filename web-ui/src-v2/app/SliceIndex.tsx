import { memo, useLayoutEffect, useRef, type KeyboardEvent } from 'react';
import type { SliceDto, SliceId } from '../bridge/protocol';
import { sliceKey } from './sliceInteraction';

export const sliceNumber = (index: number) => String(index + 1).padStart(2, '0');

export const SliceIndex = memo(function SliceIndex({ slices, selectedId, select, linked, focused, onHover, onFocusChange }: {
  slices: readonly SliceDto[]; selectedId: SliceId | undefined; select: (id: SliceId) => void;
  linked: SliceId | null; focused: SliceId | null; onHover: (id: SliceId | null) => void;
  onFocusChange: (id: SliceId | null) => void;
}) {
  const items = useRef<Array<HTMLButtonElement | null>>([]);
  const group = useRef<HTMLDivElement>(null);

  useLayoutEffect(() => {
    const container = group.current;
    const item = items.current[slices.findIndex(slice => slice.id === (focused ?? selectedId))];
    if (!container || !item) return;
    const bounds = container.getBoundingClientRect();
    const target = item.getBoundingClientRect();
    if (target.top < bounds.top) container.scrollTop += target.top - bounds.top;
    else if (target.bottom > bounds.bottom) container.scrollTop += target.bottom - bounds.bottom;
  }, [focused, selectedId, slices]);

  function onKey(event: KeyboardEvent<HTMLButtonElement>, index: number) {
    if (event.altKey || event.ctrlKey || event.metaKey || event.shiftKey) return;
    const action = sliceKey(event.key, index, slices.length);
    if (!action) return;
    event.preventDefault();
    if ('focus' in action) items.current[action.focus]?.focus();
    else select(slices[action.select].id);
  }

  return <div className="cut-index" ref={group} role="group" aria-label="Slice index" aria-describedby="slice-index-help">
    {slices.map((slice, index) => <button key={slice.id} data-slice-index={index}
      ref={element => { items.current[index] = element; }}
      type="button" aria-pressed={slice.id === selectedId} tabIndex={slice.id === (focused ?? selectedId) ? 0 : -1}
      aria-label={`Slice ${sliceNumber(index)}`} className={slice.id === linked ? 'is-linked' : undefined}
      onClick={() => select(slice.id)} onPointerEnter={() => onHover(slice.id)} onPointerLeave={() => onHover(null)}
      onFocus={() => onFocusChange(slice.id)} onBlur={() => onFocusChange(null)} onKeyDown={event => onKey(event, index)}>
      {sliceNumber(index)}
    </button>)}
  </div>;
});
