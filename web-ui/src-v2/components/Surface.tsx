import type { HTMLAttributes, ReactNode } from 'react';

type InstrumentSurfaceProps = HTMLAttributes<HTMLElement> & {
  children: ReactNode;
  level?: 'base' | 'raised';
};

export function InstrumentSurface({ children, level = 'base', className = '', ...props }: InstrumentSurfaceProps) {
  return <section className={`cs-surface cs-surface--${level} ${className}`} {...props}>{children}</section>;
}

export function SubtleDivider({ vertical = false }: { vertical?: boolean }) {
  return <span className={`cs-divider${vertical ? ' cs-divider--vertical' : ''}`} aria-hidden="true" />;
}
