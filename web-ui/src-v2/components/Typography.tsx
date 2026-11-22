import type { HTMLAttributes, ReactNode } from 'react';

type TextProps = HTMLAttributes<HTMLElement> & { children: ReactNode };

export function Label({ children, className = '', ...props }: TextProps) {
  return <span className={`cs-label ${className}`} {...props}>{children}</span>;
}

export function Value({ children, className = '', ...props }: TextProps) {
  return <span className={`cs-value ${className}`} {...props}>{children}</span>;
}

export function Metadata({ children, className = '', ...props }: TextProps) {
  return <span className={`cs-metadata ${className}`} {...props}>{children}</span>;
}

export function SectionTitle({ children, className = '', ...props }: TextProps) {
  return <h2 className={`cs-section-title ${className}`} {...props}>{children}</h2>;
}
