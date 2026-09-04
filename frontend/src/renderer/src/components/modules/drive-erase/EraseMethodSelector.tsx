import React, { useRef, useState } from 'react';
import { Check, ChevronDown, ShieldCheck } from 'lucide-react';

export type EraseStandard = 'nist-clear' | 'nist-purge' | 'dod-522022m' | 'afssi-5020';

interface StandardInfo {
  label: string;
  passes: number;
  description: string;
}

export const ERASE_STANDARDS: Record<EraseStandard, StandardInfo> = {
  'nist-clear': {
    label: 'NIST SP 800-88 Rev. 1, Clear',
    passes: 1,
    description: 'Single logical overwrite. Fast, suitable for drives being reused internally.',
  },
  'nist-purge': {
    label: 'NIST SP 800-88 Rev. 1, Purge',
    passes: 1,
    description: 'Crypto scramble or block erase at the controller level. Recommended for SSD and NVMe.',
  },
  'dod-522022m': {
    label: 'DoD 5220.22-M',
    passes: 3,
    description: 'Three-pass overwrite with a final verification read. Legacy standard for spinning disks.',
  },
  'afssi-5020': {
    label: 'AFSSI-5020',
    passes: 3,
    description: 'Three-pass overwrite with random data on the final pass, used for classified media.',
  },
};

interface EraseMethodSelectorProps {
  value: EraseStandard;
  onChange: (value: EraseStandard) => void;
  disabled?: boolean;
}

export const EraseMethodSelector: React.FC<EraseMethodSelectorProps> = ({
  value,
  onChange,
  disabled = false,
}) => {
  const [open, setOpen] = useState(false);
  const containerRef = useRef<HTMLDivElement>(null);
  const selected = ERASE_STANDARDS[value];

  return (
    <div className="space-y-2">
      <p className="text-xs font-medium text-text-muted">Sanitization standard</p>
      <div ref={containerRef} className="relative">
        <button
          type="button"
          disabled={disabled}
          onClick={() => setOpen((prev) => !prev)}
          aria-haspopup="listbox"
          aria-expanded={open}
          className="flex w-full items-center justify-between gap-3 rounded-md border border-ui-outline bg-background-main px-3 py-2.5 text-left transition-colors disabled:cursor-not-allowed disabled:opacity-50 hover:enabled:bg-ui-selection/40"
        >
          <div className="flex min-w-0 items-center gap-2.5">
            <ShieldCheck className="h-4 w-4 shrink-0 text-status-valid" />
            <div className="min-w-0">
              <p className="truncate text-sm text-text-pure">{selected.label}</p>
              <p className="truncate text-xs text-text-muted">
                {selected.passes} pass{selected.passes === 1 ? '' : 'es'}
              </p>
            </div>
          </div>
          <ChevronDown
            className={`h-4 w-4 shrink-0 text-text-muted transition-transform ${open ? 'rotate-180' : ''}`}
          />
        </button>

        {open && (
          <>
            <div className="fixed inset-0 z-40" onClick={() => setOpen(false)} />
            <div
              role="listbox"
              className="absolute z-50 mt-1.5 w-full rounded-md border border-ui-outline bg-background-sidebar p-1 shadow-xl"
            >
              {(Object.keys(ERASE_STANDARDS) as EraseStandard[]).map((key) => {
                const option = ERASE_STANDARDS[key];
                const isSelected = key === value;
                return (
                  <button
                    key={key}
                    type="button"
                    role="option"
                    aria-selected={isSelected}
                    onClick={() => {
                      onChange(key);
                      setOpen(false);
                    }}
                    className={`flex w-full items-start gap-2.5 rounded-md px-3 py-2.5 text-left transition-colors ${
                      isSelected ? 'bg-ui-selection' : 'hover:bg-ui-selection/50'
                    }`}
                  >
                    <Check
                      className={`mt-0.5 h-3.5 w-3.5 shrink-0 text-status-valid ${isSelected ? 'opacity-100' : 'opacity-0'}`}
                    />
                    <div className="min-w-0">
                      <p className="text-sm text-text-pure">{option.label}</p>
                      <p className="mt-0.5 text-xs leading-relaxed text-text-muted">
                        {option.description}
                      </p>
                    </div>
                  </button>
                );
              })}
            </div>
          </>
        )}
      </div>
    </div>
  );
};