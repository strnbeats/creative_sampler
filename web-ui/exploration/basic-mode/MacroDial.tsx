import { useId, useRef, useState, type KeyboardEvent, type PointerEvent } from 'react';

import type { MacroMaterialId } from './data';
import { MacroGraphic } from './MacroMaterial';

type Props = {
  material: MacroMaterialId;
  tint: string;
  label: string;
  value: number;
  defaultValue: number;
  mapping: readonly [string, string];
  disabled: boolean;
  onChange: (value: number) => void;
};

const clamp = (value: number) => Math.round(Math.min(100, Math.max(0, value)));

export function MacroDial({ material, tint, label, value, defaultValue, mapping, disabled, onChange }: Props) {
  const [dragging, setDragging] = useState(false);
  const drag = useRef<{ id: number; y: number; value: number } | null>(null);
  const hintId = useId();

  function finish(event: PointerEvent<HTMLDivElement>) {
    if (drag.current?.id !== event.pointerId) return;
    drag.current = null;
    setDragging(false);
  }

  function handleKey(event: KeyboardEvent<HTMLDivElement>) {
    let next: number;
    const step = event.shiftKey ? 5 : 1;
    switch (event.key) {
      case 'ArrowUp': case 'ArrowRight': next = value + step; break;
      case 'ArrowDown': case 'ArrowLeft': next = value - step; break;
      case 'PageUp': next = value + 10; break;
      case 'PageDown': next = value - 10; break;
      case 'Home': next = 0; break;
      case 'End': next = 100; break;
      case 'Enter': next = defaultValue; break;
      default: return;
    }
    event.preventDefault();
    onChange(clamp(next));
  }

  return <section className={`macro ${dragging ? 'is-dragging' : ''}`}>
    <div className="macro-hit" role="slider" tabIndex={disabled ? -1 : 0}
      aria-label={label} aria-describedby={hintId} aria-disabled={disabled}
      aria-orientation="vertical" aria-valuemin={0} aria-valuemax={100}
      aria-valuenow={value} aria-valuetext={`${value} percent`}
      onPointerDown={event => {
        if (disabled || event.button !== 0) return;
        event.currentTarget.focus({ preventScroll: true });
        event.currentTarget.setPointerCapture(event.pointerId);
        drag.current = { id: event.pointerId, y: event.clientY, value };
        setDragging(true);
      }}
      onPointerMove={event => {
        const start = drag.current;
        if (start?.id === event.pointerId)
          onChange(clamp(start.value + (start.y - event.clientY) * (event.shiftKey ? .2 : .65)));
      }}
      onPointerUp={finish} onPointerCancel={finish} onLostPointerCapture={finish}
      onDoubleClick={() => { if (!disabled) onChange(defaultValue); }}
      onKeyDown={event => { if (!disabled) handleKey(event); }}>
      <MacroGraphic material={material} tint={tint} value={value} />
    </div>
    <div className="macro-reading">
      <h3 title={label}>{label}</h3>
      <span className="macro-value">{value.toString().padStart(2, '0')}<small>%</small></span>
    </div>
    <p id={hintId} className="macro-mapping" title={mapping.join(' · ')}
      aria-label={`Maps to ${mapping.join(' and ')}. Enter resets to ${defaultValue} percent.`}>
      {mapping[0]} <span aria-hidden="true">·</span> {mapping[1]}
    </p>
  </section>;
}
