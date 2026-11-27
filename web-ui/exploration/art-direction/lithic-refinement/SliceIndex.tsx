import { useRef, type KeyboardEvent } from 'react';
import { number, type Slice } from './data';

export function SliceIndex({ slices, selected, select, hovered, onHover, indexRef }: {
  slices: readonly Slice[]; selected: number; select: (index: number) => void;
  hovered: number | null; onHover: (index: number | null) => void;
  indexRef: React.RefObject<HTMLDivElement | null>;
}) {
  const items = useRef<Array<HTMLButtonElement | null>>([]);

  function onKey(event: KeyboardEvent<HTMLButtonElement>, index: number) {
    let next: number;
    switch (event.key) {
      case 'ArrowLeft': case 'ArrowUp': next = Math.max(0, index - 1); break;
      case 'ArrowRight': case 'ArrowDown': next = Math.min(slices.length - 1, index + 1); break;
      case 'Home': next = 0; break;
      case 'End': next = slices.length - 1; break;
      default: return;
    }
    event.preventDefault();
    select(next);
    items.current[next]?.focus();
  }

  return <div className="cut-index" ref={indexRef} role="radiogroup" aria-label="Slice index">
    {slices.map((part, i) => <button key={i} ref={element => { items.current[i] = element; }}
      data-slice-index={i} type="button" role="radio" aria-checked={i === selected} tabIndex={i === selected ? 0 : -1}
      aria-label={`Slice ${number(i)} · ${part.name}`} className={i === hovered ? 'is-linked' : undefined}
      onClick={() => select(i)} onPointerEnter={() => onHover(i)} onPointerLeave={() => onHover(null)}
      onFocus={() => onHover(i)} onBlur={() => onHover(null)} onKeyDown={event => onKey(event, i)}>{number(i)}</button>)}
  </div>;
}
