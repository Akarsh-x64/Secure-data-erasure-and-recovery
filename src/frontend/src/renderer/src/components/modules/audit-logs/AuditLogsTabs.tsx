import React, { useEffect, useMemo, useState } from 'react';
import { Eye } from 'lucide-react';
import { LogFilter } from './LogFilter';
import { LogsTable, type AuditLogEntry } from './LogsTable';
import { ReportGeneratorModal } from '../../shared/ReportGeneratorModal';

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
  const [logs, setLogs] = useState<AuditLogEntry[]>([]);
  const [query, setQuery] = useState('');
  const [reportOpen, setReportOpen] = useState(false);
  const [modalLogs, setModalLogs] = useState<AuditLogEntry[] | null>(null);

  useEffect(() => {
    const fetchLogs = async (): Promise<void> => {
      try {
        if (window.api?.getAuditLogs) {
          const fetched = await window.api.getAuditLogs();
          setLogs(fetched || []);
        }
      } catch (err) {
        console.error('Failed to fetch audit logs:', err);
      }
    };

    fetchLogs();
    const interval = setInterval(fetchLogs, 3000);
    return () => clearInterval(interval);
  }, []);

  const filteredLogs = useMemo(() => {
    const { text, filters } = parseQuery(query);

    return logs.filter((log) => {
      if (filters.action && !log.action.toLowerCase().includes(filters.action)) return false;
      if (filters.level && log.level !== filters.level) return false;
      if (filters.operator && !log.operatorId.toLowerCase().includes(filters.operator)) return false;
      if (filters.verified) {
        const wantsVerified = filters.verified === 'true';
        if (log.verified !== wantsVerified) return false;
      }
      if (text) {
        const haystack = `${log.action} ${log.operatorId} ${log.sha256} ${JSON.stringify(log.payload || {})}`.toLowerCase();
        if (!haystack.includes(text)) return false;
      }
      return true;
    });
  }, [logs, query]);

  const activeModalLogs = modalLogs ?? filteredLogs;

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
          onClick={() => {
            setModalLogs(filteredLogs);
            setReportOpen(true);
          }}
          className="flex items-center gap-2 rounded-md border border-button-primary bg-button-primary px-3 py-2 text-sm font-medium text-button-primary-text transition-colors hover:bg-button-primary/85"
        >
          <Eye className="h-4 w-4" />
          View Full Audit Report
        </button>
      </header>

      <LogFilter value={query} onChange={setQuery} resultCount={filteredLogs.length} />

      <main className="min-h-0 flex-1">
        <LogsTable
          entries={filteredLogs}
          onViewReport={(entry) => {
            setModalLogs([entry]);
            setReportOpen(true);
          }}
        />
      </main>

      <ReportGeneratorModal
        open={reportOpen}
        onClose={() => setReportOpen(false)}
        entryCount={activeModalLogs.length}
        logs={activeModalLogs}
      />
    </div>
  );
}