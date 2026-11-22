import { useRef, useState, type KeyboardEvent, type PointerEvent } from 'react';
import { Label, Value } from './Typography';

export type RotaryControlProps = {
  label: string;
  value: number;
  min: number;
  max: number;
  step?: number;
  accent?: 'violet' | 'mint' | 'amber';
  formatValue?: (value: number) => string;
  onChange?: (value: number) => void;
  disabled?: boolean;
  readOnly?: boolean;
};

const clamp = (value: number, min: number, max: number) => Math.min(max, Math.max(min, value));

export function RotaryControl({ label, value, min, max, step = 0.01, accent = 'violet', formatValue,
  onChange, disabled = false, readOnly = false }: RotaryControlProps) {
  const [dragging, setDragging] = useState(false);
  const pointerY = useRef(0);
  const pointerValue = useRef(value);
  const fraction = max > min ? clamp((value - min) / (max - min), 0, 1) : 0;
  const angle = -135 + fraction * 270;
  const formatted = formatValue?.(value) ?? value.toFixed(2);

  const setFromPointer = (event: PointerEvent<HTMLButtonElement>) => {
    const delta = pointerY.current - event.clientY;
    pointerY.current = event.clientY;
    pointerValue.current = clamp(pointerValue.current + delta * (max - min) / 150, min, max);
    onChange?.(Math.round(pointerValue.current / step) * step);
  };

  const handleKeyDown = (event: KeyboardEvent<HTMLButtonElement>) => {
    if (!onChange || disabled || readOnly) return;
    const increments: Record<string, number> = { ArrowUp: step, ArrowRight: step, ArrowDown: -step, ArrowLeft: -step,
      PageUp: step * 10, PageDown: -step * 10, Home: min - value, End: max - value };
    const increment = increments[event.key];
    if (increment === undefined) return;
    event.preventDefault();
    onChange(clamp(Math.round((value + increment) / step) * step, min, max));
  };

  return <div className={`cs-rotary cs-rotary--${accent}${disabled ? ' is-disabled' : ''}${readOnly ? ' is-readonly' : ''}`}>
    <button className={`cs-rotary__face${dragging ? ' is-dragging' : ''}`} type="button" role="slider"
      aria-label={label} aria-valuemin={min} aria-valuemax={max} aria-valuenow={value}
      aria-valuetext={formatted} aria-disabled={disabled} aria-readonly={readOnly} disabled={disabled}
      onPointerDown={event => {
        if (disabled || readOnly || !onChange) return;
        event.currentTarget.setPointerCapture(event.pointerId);
        pointerY.current = event.clientY;
        pointerValue.current = value;
        setDragging(true);
      }}
      onPointerMove={event => { if (dragging) setFromPointer(event); }}
      onPointerUp={() => setDragging(false)}
      onPointerCancel={() => setDragging(false)}
      onKeyDown={handleKeyDown}>
      <svg className="cs-rotary__svg" viewBox="0 0 72 72" aria-hidden="true">
        <path className="cs-rotary__track" d="M 11.2 52.3 A 29 29 0 1 1 60.8 52.3" />
        <path className="cs-rotary__progress" d="M 11.2 52.3 A 29 29 0 1 1 60.8 52.3"
          pathLength="100" strokeDasharray={`${fraction * 100} 100`} />
        <circle className="cs-rotary__hub" cx="36" cy="36" r="20" />
        <line className="cs-rotary__indicator" x1="36" y1="34" x2="36" y2="20"
          transform={`rotate(${angle} 36 36)`} />
      </svg>
    </button>
    <Value className="cs-rotary__value">{formatted}</Value>
    <Label className="cs-rotary__label">{label}</Label>
  </div>;
}
