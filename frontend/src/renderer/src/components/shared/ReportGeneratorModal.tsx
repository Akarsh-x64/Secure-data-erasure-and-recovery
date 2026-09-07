import React, { useState } from 'react';
import { CheckCircle2, FileJson, FileText, Loader2, ShieldCheck, X } from 'lucide-react';

export type ReportFormat = 'pdf' | 'json';

interface ReportGeneratorModalProps {
  open: boolean;
  onClose: () => void;
  entryCount: number;
}

interface ReportSection {
  id: string;
  label: string;
  description: string;
  enabled: boolean;
}

const DEFAULT_SECTIONS: ReportSection[] = [
  {
    id: 'metadata',
    label: 'Execution metadata',
    description: 'Operator id, timestamps, device and target identifiers.',
    enabled: true,
  },
  {
    id: 'hashes',
    label: 'Pre and post wipe sector hashes',
    description: 'SHA-256 digests captured before and after sanitization.',
    enabled: true,
  },
  {
    id: 'signatures',
    label: 'Operator signatures',
    description: 'Cryptographic signatures attached to each logged action.',
    enabled: true,
  },
  {
    id: 'log-trail',
    label: 'Full audit trail',
    description: 'Complete chronological log of matched entries.',
    enabled: false,
  },
];

export const ReportGeneratorModal: React.FC<ReportGeneratorModalProps> = ({
  open,
  onClose,
  entryCount,
}) => {
  const [format, setFormat] = useState<ReportFormat>('pdf');
  const [sections, setSections] = useState<ReportSection[]>(DEFAULT_SECTIONS);
  const [status, setStatus] = useState<'idle' | 'generating' | 'ready'>('idle');

  if (!open) return null;

  const toggleSection = (id: string): void => {
    setSections((prev) => prev.map((s) => (s.id === id ? { ...s, enabled: !s.enabled } : s)));
  };

  const enabledCount = sections.filter((s) => s.enabled).length;

  const generate = (): void => {
    setStatus('generating');
    setTimeout(() => setStatus('ready'), 1400);
  };

  const handleClose = (): void => {
    setStatus('idle');
    onClose();
  };

  return (
    <div
      className="fixed inset-0 z-50 flex items-center justify-center bg-black/60 p-4 backdrop-blur-sm"
      role="dialog"
      aria-modal="true"
      aria-labelledby="report-modal-title"
    >
      <div className="w-full max-w-lg rounded-lg border border-ui-outline bg-background-sidebar shadow-2xl">
        <div className="flex items-center justify-between border-b border-ui-outline px-4 py-3">
          <div className="flex items-center gap-2 text-text-pure">
            <ShieldCheck className="h-4 w-4 text-status-valid" />
            <h2 id="report-modal-title" className="text-sm font-medium">
              Generate forensic report
            </h2>
          </div>
          <button
            type="button"
            onClick={handleClose}
            title="Close"
            className="rounded-md p-1 text-text-muted transition-colors hover:bg-ui-selection hover:text-text-pure"
          >
            <X className="h-4 w-4" />
          </button>
        </div>

        <div className="space-y-4 p-4">
          <p className="text-sm text-text-muted">
            Exports a signed report covering {entryCount} matched log{' '}
            {entryCount === 1 ? 'entry' : 'entries'}.
          </p>

          <div className="space-y-2">
            <p className="text-xs font-medium text-text-muted">Export format</p>
            <div className="grid grid-cols-2 gap-2">
              <button
                type="button"
                onClick={() => setFormat('pdf')}
                className={`flex items-center gap-2.5 rounded-md border px-3 py-2.5 text-left text-sm transition-colors ${
                  format === 'pdf'
                    ? 'border-status-valid/50 bg-status-valid/10 text-text-pure'
                    : 'border-ui-outline text-text-muted hover:bg-ui-selection/40'
                }`}
              >
                <FileText className="h-4 w-4" />
                PDF certificate
              </button>
              <button
                type="button"
                onClick={() => setFormat('json')}
                className={`flex items-center gap-2.5 rounded-md border px-3 py-2.5 text-left text-sm transition-colors ${
                  format === 'json'
                    ? 'border-status-valid/50 bg-status-valid/10 text-text-pure'
                    : 'border-ui-outline text-text-muted hover:bg-ui-selection/40'
                }`}
              >
                <FileJson className="h-4 w-4" />
                JSON payload
              </button>
            </div>
          </div>

          <div className="space-y-2">
            <p className="text-xs font-medium text-text-muted">
              Included sections, {enabledCount} of {sections.length}
            </p>
            <div className="space-y-1.5">
              {sections.map((section) => (
                <label
                  key={section.id}
                  className="flex cursor-pointer items-start gap-2.5 rounded-md border border-ui-outline bg-background-main px-3 py-2.5 transition-colors hover:bg-ui-selection/30"
                >
                  <input
                    type="checkbox"
                    checked={section.enabled}
                    onChange={() => toggleSection(section.id)}
                    className="mt-0.5 h-4 w-4 shrink-0 accent-status-valid"
                  />
                  <div className="min-w-0">
                    <p className="text-sm text-text-pure">{section.label}</p>
                    <p className="mt-0.5 text-xs text-text-muted">{section.description}</p>
                  </div>
                </label>
              ))}
            </div>
          </div>

          {status === 'ready' ? (
            <div className="flex items-center justify-between rounded-md border border-status-valid/40 bg-status-valid/10 px-3 py-3 text-sm">
              <span className="flex items-center gap-2 text-status-valid">
                <CheckCircle2 className="h-4 w-4" />
                Report signed and ready
              </span>
              <button
                type="button"
                className="rounded-md bg-status-valid px-3 py-1.5 text-xs font-medium text-background-main transition-colors hover:bg-status-valid/85"
              >
                Download {format === 'pdf' ? '.pdf' : '.json'}
              </button>
            </div>
          ) : (
            <div className="flex justify-end gap-2 pt-1">
              <button
                type="button"
                onClick={handleClose}
                className="rounded-md border border-ui-outline px-3 py-2 text-sm text-text-muted transition-colors hover:bg-ui-selection hover:text-text-pure"
              >
                Cancel
              </button>
              <button
                type="button"
                disabled={enabledCount === 0 || status === 'generating'}
                onClick={generate}
                className="flex items-center gap-2 rounded-md bg-status-valid px-3 py-2 text-sm font-medium text-background-main transition-colors hover:bg-status-valid/85 disabled:cursor-not-allowed disabled:bg-ui-selection disabled:text-text-muted"
              >
                {status === 'generating' && <Loader2 className="h-4 w-4 animate-spin" />}
                {status === 'generating' ? 'Signing report...' : 'Generate report'}
              </button>
            </div>
          )}
        </div>
      </div>
    </div>
  );
};