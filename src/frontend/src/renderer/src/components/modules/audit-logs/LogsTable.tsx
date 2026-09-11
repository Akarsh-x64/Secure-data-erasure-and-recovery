import React, { useState } from 'react';
import { ChevronDown, ChevronRight, FileText, KeyRound, ShieldCheck, ShieldX } from 'lucide-react';

export type LogLevel = 'info' | 'warning' | 'error';

export interface AuditLogEntry {
  id: string;
  timestamp: string;
  operatorId: string;
  action: string;
  level: LogLevel;
  verified: boolean;
  sha256: string;
  signature: string;
  payload: Record<string, unknown>;
}

interface LogsTableProps {
  entries: AuditLogEntry[];
  onViewReport?: (entry: AuditLogEntry) => void;
}

const levelConfig: Record<LogLevel, { label: string; color: string; dot: string }> = {
  info: { label: 'Info', color: 'text-text-muted', dot: 'bg-text-muted' },
  warning: { label: 'Warning', color: 'text-status-warning', dot: 'bg-status-warning' },
  error: { label: 'Error', color: 'text-status-error', dot: 'bg-status-error' },
};

function formatTimestamp(iso: string): string {
  const date = new Date(iso);
  return date.toLocaleString('en-US', {
    month: 'short',
    day: '2-digit',
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
    hour12: false,
  });
}

export const LogsTable: React.FC<LogsTableProps> = ({ entries, onViewReport }) => {
  const [expandedId, setExpandedId] = useState<string | null>(null);

  return (
    <section className="flex min-h-0 flex-1 flex-col rounded-lg border border-ui-outline bg-background-sidebar">
      <div className="min-h-0 flex-1 overflow-auto">
        <table className="w-full border-collapse text-left text-sm">
          <thead className="sticky top-0 z-10 bg-background-sidebar text-xs text-text-muted">
            <tr>
              <th className="w-8 border-b border-ui-outline px-2 py-2" aria-hidden />
              <th className="border-b border-ui-outline px-2 py-2 font-normal">Timestamp</th>
              <th className="border-b border-ui-outline px-2 py-2 font-normal">Operator</th>
              <th className="border-b border-ui-outline px-2 py-2 font-normal">Action</th>
              <th className="border-b border-ui-outline px-2 py-2 font-normal">Level</th>
              <th className="border-b border-ui-outline px-4 py-2 font-normal">Verification</th>
              <th className="border-b border-ui-outline px-2 py-2 font-normal text-right">Audit Report</th>
            </tr>
          </thead>
          <tbody>
            {entries.map((entry) => {
              const isExpanded = expandedId === entry.id;
              const level = levelConfig[entry.level];
              return (
                <React.Fragment key={entry.id}>
                  <tr
                    onClick={() => setExpandedId(isExpanded ? null : entry.id)}
                    className="cursor-pointer border-b border-ui-outline/60 transition-colors hover:bg-ui-selection/40"
                  >
                    <td className="px-2 py-2.5 text-text-muted">
                      {isExpanded ? (
                        <ChevronDown className="h-3.5 w-3.5" />
                      ) : (
                        <ChevronRight className="h-3.5 w-3.5" />
                      )}
                    </td>
                    <td className="whitespace-nowrap px-2 py-2.5 text-text-muted">
                      {formatTimestamp(entry.timestamp)}
                    </td>
                    <td className="whitespace-nowrap px-2 py-2.5 text-text-pure">
                      {entry.operatorId}
                    </td>
                    <td className="whitespace-nowrap px-2 py-2.5 text-text-pure">{entry.action}</td>
                    <td className="whitespace-nowrap px-2 py-2.5">
                      <span className={`flex items-center gap-1.5 text-xs ${level.color}`}>
                        <span className={`h-1.5 w-1.5 rounded-full ${level.dot}`} />
                        {level.label}
                      </span>
                    </td>
                    <td className="px-4 py-2.5">
                      {entry.verified ? (
                        <span className="flex items-center gap-1.5 text-xs text-status-valid">
                          <ShieldCheck className="h-3.5 w-3.5" />
                          Signature verified
                        </span>
                      ) : (
                        <span className="flex items-center gap-1.5 text-xs text-status-error">
                          <ShieldX className="h-3.5 w-3.5" />
                          Verification failed
                        </span>
                      )}
                    </td>
                    <td className="px-2 py-2.5 text-right">
                      <button
                        type="button"
                        onClick={(e) => {
                          e.stopPropagation();
                          onViewReport?.(entry);
                        }}
                        className="inline-flex items-center gap-1 rounded border border-status-valid/40 bg-status-valid/10 px-2 py-1 text-xs text-status-valid transition-colors hover:bg-status-valid/25"
                      >
                        <FileText className="h-3 w-3" />
                        Action Audit
                      </button>
                    </td>
                  </tr>
                  {isExpanded && (
                    <tr className="border-b border-ui-outline/60 bg-background-main">
                      <td colSpan={7} className="px-4 py-3">
                        <div className="grid grid-cols-1 gap-3 md:grid-cols-2">
                          <div className="space-y-2 text-xs">
                            <div className="flex items-center gap-1.5 text-text-muted">
                              <KeyRound className="h-3.5 w-3.5" />
                              SHA-256 hash
                            </div>
                            <p className="break-all rounded-md border border-ui-outline bg-background-icon px-2.5 py-2 text-text-pure">
                              {entry.sha256}
                            </p>
                            <div className="flex items-center gap-1.5 text-text-muted">
                              <KeyRound className="h-3.5 w-3.5" />
                              Operator signature
                            </div>
                            <p className="break-all rounded-md border border-ui-outline bg-background-icon px-2.5 py-2 text-text-pure">
                              {entry.signature}
                            </p>
                          </div>
                          <div className="text-xs">
                            <div className="mb-2 text-text-muted">Signed payload</div>
                            <div className="max-h-48 overflow-auto rounded-md border border-ui-outline bg-background-icon p-2 text-text-pure">
                              {Object.keys(entry.payload || {}).length > 0 ? (
                                <div className="grid grid-cols-1 gap-1.5 sm:grid-cols-2">
                                  {Object.entries(entry.payload || {}).map(([key, val]) => (
                                    <div
                                      key={key}
                                      className="flex items-center justify-between rounded border border-ui-outline/40 bg-background-main/60 px-2 py-1"
                                    >
                                      <span className="text-[11px] text-text-muted capitalize">
                                        {key.replace(/([A-Z])/g, ' $1')}
                                      </span>
                                      <span className="font-mono text-[11px] font-medium text-text-pure">
                                        {Array.isArray(val) ? val.join(', ') : String(val)}
                                      </span>
                                    </div>
                                  ))}
                                </div>
                              ) : (
                                <span className="italic text-text-muted">No payload parameters</span>
                              )}
                            </div>
                          </div>
                        </div>
                      </td>
                    </tr>
                  )}
                </React.Fragment>
              );
            })}
            {entries.length === 0 && (
              <tr>
                <td colSpan={6} className="px-4 py-14 text-center text-sm text-text-muted">
                  No log entries match the current filter.
                </td>
              </tr>
            )}
          </tbody>
        </table>
      </div>
    </section>
  );
};