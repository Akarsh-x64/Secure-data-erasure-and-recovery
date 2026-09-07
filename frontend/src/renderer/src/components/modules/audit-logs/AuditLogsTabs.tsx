import React, { useMemo, useState } from 'react';
import { FileOutput } from 'lucide-react';
import { LogFilter } from './LogFilter';
import { LogsTable, type AuditLogEntry } from './LogsTable';
import { ReportGeneratorModal } from '../../shared/ReportGeneratorModal';

const SAMPLE_LOGS: AuditLogEntry[] = [
  {
    id: 'log-1',
    timestamp: '2026-09-07T09:12:44Z',
    operatorId: 'operator.rhodes',
    action: 'drive-erase',
    level: 'info',
    verified: true,
    sha256: '3b1c9e0f7a2d5e8c1f4a6b9d0e3c7f2a5b8d1e4c7f0a3b6d9e2c5f8a1b4d7e0c',
    signature: '30450221009f2c...b1e4022100c7a3d0f6',
    payload: {
      device: '/dev/nvme0n1',
      standard: 'NIST SP 800-88 Rev. 1, Purge',
      passes: 1,
      durationSeconds: 812,
      result: 'success',
    },
  },
  {
    id: 'log-2',
    timestamp: '2026-09-07T08:47:02Z',
    operatorId: 'operator.chen',
    action: 'file-erase',
    level: 'info',
    verified: true,
    sha256: '9e4a2c7f1b8d5e0a3c6f9b2d5e8a1c4f7b0d3e6a9c2f5b8d1e4a7c0f3b6d9e2a',
    signature: '3044022064f1...a92e02201b3c8f4d',
    payload: {
      targets: ['/var/log/audit/system_audit.log'],
      overwriteMethod: 'zero',
      passCount: 1,
      result: 'success',
    },
  },
  {
    id: 'log-3',
    timestamp: '2026-09-07T08:15:19Z',
    operatorId: 'operator.chen',
    action: 'recovery-scan',
    level: 'warning',
    verified: true,
    sha256: '1a2b3c4d5e6f7a8b9c0d1e2f3a4b5c6d7e8f9a0b1c2d3e4f5a6b7c8d9e0f1a2b',
    signature: '30440220a1b2...c3d40220e5f67890',
    payload: {
      source: 'evidence_export.dd',
      signaturesEnabled: 5,
      artifactsRecovered: 3,
      lowConfidenceArtifacts: 1,
    },
  },
  {
    id: 'log-4',
    timestamp: '2026-09-07T07:58:33Z',
    operatorId: 'operator.reyes',
    action: 'drive-erase',
    level: 'error',
    verified: false,
    sha256: '7f8e9d0c1b2a3f4e5d6c7b8a9f0e1d2c3b4a5f6e7d8c9b0a1f2e3d4c5b6a7f8e',
    signature: '3045022100d4e5...f60022043a1b2c',
    payload: {
      device: '/dev/sdb',
      standard: 'DoD 5220.22-M',
      passes: 3,
      result: 'aborted',
      reason: 'device unmounted unexpectedly',
    },
  },
  {
    id: 'log-5',
    timestamp: '2026-09-07T07:30:11Z',
    operatorId: 'operator.rhodes',
    action: 'report-export',
    level: 'info',
    verified: true,
    sha256: '5c6d7e8f9a0b1c2d3e4f5a6b7c8d9e0f1a2b3c4d5e6f7a8b9c0d1e2f3a4b5c6d',
    signature: '3046022100b2c3...d4e5022100f60789ab',
    payload: {
      format: 'pdf',
      sections: ['metadata', 'hashes', 'signatures'],
      entriesIncluded: 42,
    },
  },
];

function parseQuery(query: string): { text: string; filters: Record<string, string> } {
  const tokens = query.trim().split(/\s+/).filter(Boolean);
  const filters: Record<string, string> = {};
  const textParts: string[] = [];

  for (const token of tokens) {
    const match = token.match(/^([a-zA-Z]+):(.+)$/);
    if (match) {
      filters[match[1].toLowerCase()] = match[2].toLowerCase();
    } else {
      textParts.push(token);
    }
  }

  return { text: textParts.join(' ').toLowerCase(), filters };
}

export function AuditLogsTab(): React.ReactElement {
  const [query, setQuery] = useState('');
  const [reportOpen, setReportOpen] = useState(false);

  const filteredLogs = useMemo(() => {
    const { text, filters } = parseQuery(query);

    return SAMPLE_LOGS.filter((log) => {
      if (filters.action && !log.action.toLowerCase().includes(filters.action)) return false;
      if (filters.level && log.level !== filters.level) return false;
      if (filters.operator && !log.operatorId.toLowerCase().includes(filters.operator)) return false;
      if (filters.verified) {
        const wantsVerified = filters.verified === 'true';
        if (log.verified !== wantsVerified) return false;
      }
      if (text) {
        const haystack = `${log.action} ${log.operatorId} ${log.sha256}`.toLowerCase();
        if (!haystack.includes(text)) return false;
      }
      return true;
    });
  }, [query]);

  return (
    <div className="flex min-h-full flex-col gap-4 font-sans text-text-pure">
      <header className="flex flex-wrap items-end justify-between gap-3 border-b border-ui-outline pb-4">
        <div>
          <h1 className="text-xl font-semibold text-text-pure">Activity trail</h1>
          <p className="mt-1 max-w-2xl text-sm text-text-muted">
            Every action is hashed and signed at the point of execution for tamper-evident,
            exportable forensic records.
          </p>
        </div>
        <button
          type="button"
          onClick={() => setReportOpen(true)}
          className="flex items-center gap-2 rounded-md border border-button-primary bg-button-primary px-3 py-2 text-sm font-medium text-button-primary-text transition-colors hover:bg-button-primary/85"
        >
          <FileOutput className="h-4 w-4" />
          Generate report
        </button>
      </header>

      <LogFilter value={query} onChange={setQuery} resultCount={filteredLogs.length} />

      <main className="min-h-0 flex-1">
        <LogsTable entries={filteredLogs} />
      </main>

      <ReportGeneratorModal
        open={reportOpen}
        onClose={() => setReportOpen(false)}
        entryCount={filteredLogs.length}
      />
    </div>
  );
}