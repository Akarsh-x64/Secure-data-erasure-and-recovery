import React from 'react';
import {
  ShieldCheck,
  ShieldX,
  X,
  FileCheck,
  CheckCircle2,
  Calendar,
  User,
  KeyRound,
  Activity
} from 'lucide-react';
import type { AuditLogEntry } from '../modules/audit-logs/LogsTable';

interface ReportGeneratorModalProps {
  open: boolean;
  onClose: () => void;
  entryCount: number;
  logs?: AuditLogEntry[];
}

export const ReportGeneratorModal: React.FC<ReportGeneratorModalProps> = ({
  open,
  onClose,
  entryCount,
  logs = []
}) => {
  if (!open) return null;

  const verifiedCount = logs.filter((l) => l.verified).length;
  const verifiedRate = logs.length > 0 ? Math.round((verifiedCount / logs.length) * 100) : 100;

  return (
    <div
      className="fixed inset-0 z-50 flex items-center justify-center bg-black/75 p-4 backdrop-blur-md animate-in fade-in duration-200"
      role="dialog"
      aria-modal="true"
      aria-labelledby="report-modal-title"
    >
      <div className="flex flex-col w-full max-w-3xl max-h-[90vh] rounded-xl border border-ui-outline bg-background-sidebar shadow-2xl overflow-hidden font-sans text-text-pure">
        {/* Modal Header */}
        <div className="flex items-center justify-between border-b border-ui-outline px-5 py-4 bg-background-main/80">
          <div className="flex items-center gap-3">
            <div className="flex h-10 w-10 items-center justify-center rounded-lg border border-status-valid/40 bg-status-valid/15 text-status-valid">
              <ShieldCheck className="h-6 w-6" />
            </div>
            <div>
              <div className="flex items-center gap-2">
                <h2 id="report-modal-title" className="text-base font-semibold text-text-pure">
                  Forensic Audit Trail & Activity Report
                </h2>
                <span className="rounded-full border border-status-valid/40 bg-status-valid/10 px-2 py-0.5 text-[11px] font-medium text-status-valid">
                  NIST SP 800-88 REV. 1
                </span>
              </div>
              <p className="text-xs text-text-muted">
                In-app tamper-evident execution log verification & cryptographic activity summary
              </p>
            </div>
          </div>
          <button
            type="button"
            onClick={onClose}
            title="Close report viewer"
            className="rounded-lg p-1.5 text-text-muted transition-colors hover:bg-ui-selection hover:text-text-pure"
          >
            <X className="h-5 w-5" />
          </button>
        </div>

        {/* Summary Metric Cards */}
        <div className="grid grid-cols-3 gap-3 p-5 border-b border-ui-outline bg-background-main/40">
          <div className="flex items-center gap-3 rounded-lg border border-ui-outline bg-background-main p-3">
            <Activity className="h-5 w-5 text-status-valid shrink-0" />
            <div>
              <span className="text-[10px] uppercase text-text-muted font-medium block">Total Activity Logs</span>
              <span className="text-sm font-semibold text-text-pure">{entryCount} Entries</span>
            </div>
          </div>

          <div className="flex items-center gap-3 rounded-lg border border-ui-outline bg-background-main p-3">
            <CheckCircle2 className="h-5 w-5 text-status-valid shrink-0" />
            <div>
              <span className="text-[10px] uppercase text-text-muted font-medium block">Verified Signatures</span>
              <span className="text-sm font-semibold text-status-valid">
                {verifiedCount} / {logs.length} ({verifiedRate}%)
              </span>
            </div>
          </div>

          <div className="flex items-center gap-3 rounded-lg border border-ui-outline bg-background-main p-3">
            <FileCheck className="h-5 w-5 text-status-valid shrink-0" />
            <div>
              <span className="text-[10px] uppercase text-text-muted font-medium block">Audit Standard</span>
              <span className="text-sm font-semibold text-text-pure">Tamper-Evident SHA-256</span>
            </div>
          </div>
        </div>

        {/* Scrollable Audit Report Content */}
        <div className="flex-1 overflow-y-auto p-5 space-y-4 scrollbar-thin">
          <div className="flex items-center justify-between pb-1">
            <span className="text-xs font-semibold uppercase tracking-wider text-text-muted">
              Chronological Execution Logs ({logs.length})
            </span>
            <span className="text-xs text-text-muted">
              Generated at {new Date().toLocaleTimeString()}
            </span>
          </div>

          {logs.length === 0 ? (
            <div className="rounded-lg border border-ui-outline p-8 text-center text-sm text-text-muted">
              No audit logs recorded for the selected criteria.
            </div>
          ) : (
            logs.map((log) => (
              <div
                key={log.id}
                className="rounded-lg border border-ui-outline bg-background-main p-4 space-y-3 transition-colors hover:border-ui-outline/80"
              >
                {/* Log Item Header */}
                <div className="flex flex-wrap items-center justify-between gap-2 border-b border-ui-outline/40 pb-2.5">
                  <div className="flex items-center gap-2.5">
                    <span className="font-mono text-xs font-semibold text-text-pure uppercase tracking-wide bg-ui-selection px-2 py-0.5 rounded">
                      {log.action}
                    </span>
                    <span
                      className={`inline-flex items-center gap-1 text-[11px] font-medium px-2 py-0.5 rounded-full ${
                        log.level === 'info'
                          ? 'bg-status-valid/10 text-status-valid'
                          : log.level === 'warning'
                          ? 'bg-status-warning/10 text-status-warning'
                          : 'bg-status-error/10 text-status-error'
                      }`}
                    >
                      {log.level.toUpperCase()}
                    </span>
                  </div>

                  <div className="flex items-center gap-4 text-xs text-text-muted">
                    <span className="flex items-center gap-1.5">
                      <User className="h-3.5 w-3.5 text-text-muted" />
                      {log.operatorId}
                    </span>
                    <span className="flex items-center gap-1.5">
                      <Calendar className="h-3.5 w-3.5 text-text-muted" />
                      {new Date(log.timestamp).toLocaleString()}
                    </span>
                  </div>
                </div>

                {/* Verification Status */}
                <div className="flex items-center justify-between text-xs">
                  <span className="text-text-muted">Cryptographic Integrity:</span>
                  {log.verified ? (
                    <span className="flex items-center gap-1.5 text-status-valid font-medium">
                      <ShieldCheck className="h-4 w-4" />
                      Signature Verified (ECDSA-SHA256)
                    </span>
                  ) : (
                    <span className="flex items-center gap-1.5 text-status-error font-medium">
                      <ShieldX className="h-4 w-4" />
                      Verification Failed
                    </span>
                  )}
                </div>

                {/* Hashes & Signatures */}
                <div className="grid grid-cols-1 md:grid-cols-2 gap-2 text-xs">
                  <div className="rounded border border-ui-outline/50 bg-background-sidebar p-2 space-y-1">
                    <div className="flex items-center gap-1 text-[10px] uppercase font-semibold text-text-muted">
                      <KeyRound className="h-3 w-3" /> SHA-256 Digest
                    </div>
                    <p className="font-mono text-[11px] text-text-pure break-all">
                      {log.sha256}
                    </p>
                  </div>

                  <div className="rounded border border-ui-outline/50 bg-background-sidebar p-2 space-y-1">
                    <div className="flex items-center gap-1 text-[10px] uppercase font-semibold text-text-muted">
                      <KeyRound className="h-3 w-3" /> Digital Signature
                    </div>
                    <p className="font-mono text-[11px] text-text-pure break-all">
                      {log.signature}
                    </p>
                  </div>
                </div>

                {/* Formatted Signed Payload */}
                {log.payload && Object.keys(log.payload).length > 0 && (
                  <div className="space-y-1.5 pt-1">
                    <span className="text-[10px] uppercase font-semibold tracking-wider text-text-muted block">
                      Execution Payload Parameters
                    </span>
                    <div className="grid grid-cols-2 sm:grid-cols-4 gap-2">
                      {Object.entries(log.payload).map(([k, v]) => (
                        <div
                          key={k}
                          className="rounded border border-ui-outline/40 bg-background-sidebar px-2 py-1 text-xs"
                        >
                          <span className="text-[10px] text-text-muted block capitalize truncate">
                            {k.replace(/([A-Z])/g, ' $1')}
                          </span>
                          <span className="font-mono text-xs text-text-pure font-medium truncate block">
                            {Array.isArray(v) ? v.join(', ') : String(v)}
                          </span>
                        </div>
                      ))}
                    </div>
                  </div>
                )}
              </div>
            ))
          )}
        </div>

        {/* Modal Footer */}
        <div className="flex justify-end border-t border-ui-outline px-5 py-3 bg-background-main/80">
          <button
            type="button"
            onClick={onClose}
            className="rounded-lg border border-ui-outline bg-background-main px-4 py-2 text-sm font-medium text-text-pure transition-colors hover:bg-ui-selection hover:text-text-pure"
          >
            Close Report
          </button>
        </div>
      </div>
    </div>
  );
};