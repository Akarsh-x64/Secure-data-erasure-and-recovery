import React, { useState } from 'react';
import { ChevronDown, FileSignature, Settings2 } from 'lucide-react';

export interface FileSignatureDef {
  id: string;
  label: string;
  extension: string;
  headerHex: string;
  footerHex?: string;
  enabled: boolean;
}

export const DEFAULT_SIGNATURES: FileSignatureDef[] = [
  { id: 'jpeg', label: 'JPEG image', extension: '.jpg', headerHex: 'FF D8 FF', footerHex: 'FF D9', enabled: true },
  { id: 'png', label: 'PNG image', extension: '.png', headerHex: '89 50 4E 47', footerHex: '49 45 4E 44', enabled: true },
  { id: 'pdf', label: 'PDF document', extension: '.pdf', headerHex: '25 50 44 46', footerHex: '25 25 45 4F 46', enabled: true },
  { id: 'zip', label: 'ZIP archive', extension: '.zip', headerHex: '50 4B 03 04', enabled: true },
  { id: 'elf', label: 'ELF binary', extension: '.elf', headerHex: '7F 45 4C 46', enabled: false },
  { id: 'sqlite', label: 'SQLite database', extension: '.sqlite', headerHex: '53 51 4C 69 74 65', enabled: false },
  { id: 'docx', label: 'Word document', extension: '.docx', headerHex: '50 4B 03 04', enabled: false },
  { id: 'mp4', label: 'MP4 video', extension: '.mp4', headerHex: '66 74 79 70', enabled: false },
];

interface RecoveryOptionsProps {
  signatures: FileSignatureDef[];
  onChange: (signatures: FileSignatureDef[]) => void;
  disabled?: boolean;
}

export const RecoveryOptions: React.FC<RecoveryOptionsProps> = ({
  signatures,
  onChange,
  disabled = false,
}) => {
  const [expandedId, setExpandedId] = useState<string | null>(null);
  const enabledCount = signatures.filter((s) => s.enabled).length;
  const allEnabled = enabledCount === signatures.length;

  const toggleSignature = (id: string): void => {
    onChange(signatures.map((s) => (s.id === id ? { ...s, enabled: !s.enabled } : s)));
  };

  const toggleAll = (): void => {
    onChange(signatures.map((s) => ({ ...s, enabled: !allEnabled })));
  };

  const updateHeader = (id: string, value: string): void => {
    onChange(signatures.map((s) => (s.id === id ? { ...s, headerHex: value } : s)));
  };

  return (
    <section className="flex min-h-0 flex-1 flex-col rounded-lg border border-ui-outline bg-background-sidebar">
      <div className="flex items-center justify-between border-b border-ui-outline px-4 py-3">
        <div>
          <h2 className="text-sm font-medium text-text-pure">File signatures</h2>
          <p className="mt-0.5 text-xs text-text-muted">{enabledCount} of {signatures.length} enabled</p>
        </div>
        <button
          type="button"
          disabled={disabled}
          onClick={toggleAll}
          className="rounded-md border border-ui-outline px-2.5 py-1 text-xs text-text-muted transition-colors hover:bg-ui-selection hover:text-text-pure disabled:cursor-not-allowed"
        >
          {allEnabled ? 'Clear all' : 'Select all'}
        </button>
      </div>

      <div className="scrollbar-hidden min-h-0 flex-1 overflow-auto p-2">
        {signatures.map((sig) => {
          const isExpanded = expandedId === sig.id;
          return (
            <div
              key={sig.id}
              className={`mb-1 rounded-md border transition-colors ${
                sig.enabled ? 'border-ui-outline bg-background-main' : 'border-transparent'
              }`}
            >
              <label className="flex cursor-pointer items-center gap-3 px-2.5 py-2.5">
                <input
                  type="checkbox"
                  checked={sig.enabled}
                  disabled={disabled}
                  onChange={() => toggleSignature(sig.id)}
                  className="h-4 w-4 shrink-0 accent-status-valid"
                />
                <FileSignature
                  className={`h-4 w-4 shrink-0 ${sig.enabled ? 'text-status-valid' : 'text-text-muted'}`}
                />
                <div className="min-w-0 flex-1">
                  <span className="text-sm text-text-pure">{sig.label}</span>
                  <span className="ml-2 text-xs text-text-muted">{sig.extension}</span>
                </div>
                <button
                  type="button"
                  onClick={(e) => {
                    e.preventDefault();
                    setExpandedId(isExpanded ? null : sig.id);
                  }}
                  title="Custom offset parameters"
                  className="rounded p-1 text-text-muted transition-colors hover:bg-ui-selection hover:text-text-pure"
                >
                  <Settings2 className="h-3.5 w-3.5" />
                  <span className="sr-only">Toggle offset settings</span>
                </button>
                <ChevronDown
                  className={`h-3.5 w-3.5 shrink-0 text-text-muted transition-transform ${isExpanded ? 'rotate-180' : ''}`}
                />
              </label>

              {isExpanded && (
                <div className="space-y-2 border-t border-ui-outline px-3 py-2.5">
                  <label className="block text-xs text-text-muted">
                    Header offset (hex)
                    <input
                      type="text"
                      value={sig.headerHex}
                      disabled={disabled}
                      onChange={(e) => updateHeader(sig.id, e.target.value)}
                      className="mt-1 w-full rounded-md border border-ui-outline bg-background-icon px-2.5 py-1.5 text-xs text-text-pure outline-none focus:border-status-valid"
                    />
                  </label>
                  {sig.footerHex !== undefined && (
                    <label className="block text-xs text-text-muted">
                      Footer offset (hex)
                      <input
                        type="text"
                        defaultValue={sig.footerHex}
                        disabled={disabled}
                        className="mt-1 w-full rounded-md border border-ui-outline bg-background-icon px-2.5 py-1.5 text-xs text-text-pure outline-none focus:border-status-valid"
                      />
                    </label>
                  )}
                </div>
              )}
            </div>
          );
        })}
      </div>
    </section>
  );
};