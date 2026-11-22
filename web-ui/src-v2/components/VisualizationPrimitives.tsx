import type { CSSProperties } from 'react';

export type SliceIndicatorProps = {
  index: number;
  note: string;
  selected?: boolean;
  onSelect?: () => void;
};

export function SliceIndicator({ index, note, selected = false, onSelect }: SliceIndicatorProps) {
  const content = <><span className="cs-slice__index">{String(index).padStart(2, '0')}</span>
    <span className="cs-slice__note">{note}</span></>;
  return <button type="button" className={`cs-slice${selected ? ' is-selected' : ''}`} aria-pressed={selected}
    aria-label={`Slice ${index}, ${note}${selected ? ', selected' : ''}`} onClick={onSelect}>
    {content}
  </button>;
}

export function TransientMarker({ position, label, strength }: { position: string; label?: string; strength?: number }) {
  const normalizedStrength = Math.min(1, Math.max(0, strength ?? 0.65));
  const style: CSSProperties & { '--transient-opacity'?: string; '--transient-dense-opacity'?: string } = { left: position };
  if (strength !== undefined) {
    style['--transient-opacity'] = `${0.3 + normalizedStrength * 0.38}`;
    style['--transient-dense-opacity'] = `${0.2 + normalizedStrength * 0.24}`;
    style.height = `${12 + normalizedStrength * 14}%`;
  }
  return <span className="cs-transient" style={style} aria-label={label ?? 'Transient'} role="img" />;
}
