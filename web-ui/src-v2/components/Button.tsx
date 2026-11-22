import type { ButtonHTMLAttributes, ReactNode } from 'react';

type ButtonProps = ButtonHTMLAttributes<HTMLButtonElement> & {
  children: ReactNode;
  tone?: 'quiet' | 'accent' | 'transport';
};

export function Button({ children, tone = 'quiet', className = '', type = 'button', ...props }: ButtonProps) {
  return <button type={type} className={`cs-button cs-button--${tone} ${className}`} {...props}>{children}</button>;
}

export function IconButton({ label, children, className = '', ...props }: ButtonHTMLAttributes<HTMLButtonElement> & {
  label: string;
  children: ReactNode;
}) {
  return <button type="button" className={`cs-icon-button ${className}`} aria-label={label} title={label} {...props}>{children}</button>;
}
