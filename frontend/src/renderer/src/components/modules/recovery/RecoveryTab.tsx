import React, { useEffect, useRef, useState } from 'react';
import { Loader2, Search } from 'lucide-react';
import { DiskImageSelector, type ImageSource } from './DiskImageSelector';
import { RecoveryOptions, DEFAULT_SIGNATURES, type FileSignatureDef } from './RecoveryOptions';
import { ResultsTable, type RecoveredArtifact } from './ResultsTable';

const SAMPLE_RESULTS: RecoveredArtifact[] = [
  {
    id: 'r1',
    name: 'IMG_0921.raw',
    type: 'jpeg',
    size: '14.2 MB',
    fragments: 1,
    confidence: 98,
    confidenceNote: 'intact header',
    sectorOffset: '0x0A3F1000',
  },
  {
    id: 'r2',
    name: 'invoice_q3.pdf',
    type: 'pdf',
    size: '842 KB',
    fragments: 3,
    confidence: 76,
    confidenceNote: 'reassembled',
    sectorOffset: '0x0B120400',
  },
  {
    id: 'r3',
    name: 'archive_backup.zip',
    type: 'zip',
    size: '3.4 MB',
    fragments: 6,
    confidence: 41,
    confidenceNote: 'partial footer',
    sectorOffset: '0x0C880A00',
  },
];

export function RecoveryTab(): React.ReactElement {
  const [source, setSource] = useState<ImageSource | null>(null);
  const [signatures, setSignatures] = useState<FileSignatureDef[]>(DEFAULT_SIGNATURES);
  const [scanning, setScanning] = useState(false);
  const [scanPercent, setScanPercent] = useState(0);
  const [results, setResults] = useState<RecoveredArtifact[]>([]);
  const intervalRef = useRef<ReturnType<typeof setInterval> | null>(null);

  const enabledCount = signatures.filter((s) => s.enabled).length;
  const canScan =
    (source?.mode === 'file' ? Boolean(source?.fileName) : Boolean(source?.devicePath)) &&
    enabledCount > 0 &&
    !scanning;

  useEffect(() => {
    return () => {
      if (intervalRef.current) clearInterval(intervalRef.current);
    };
  }, []);

  const runScan = (): void => {
    setScanning(true);
    setScanPercent(0);
    setResults([]);

    intervalRef.current = setInterval(() => {
      setScanPercent((prev) => {
        const next = prev + Math.random() * 9 + 4;
        if (next >= 100) {
          if (intervalRef.current) clearInterval(intervalRef.current);
          setScanning(false);
          setResults(SAMPLE_RESULTS);
          return 100;
        }
        return next;
      });
    }, 300);
  };

  return (
    <div className="flex min-h-full min-w-0 flex-col gap-4 overflow-hidden font-sans text-text-pure">
      <header className="flex flex-wrap items-end justify-between gap-3 border-b border-ui-outline pb-4">
        <div>
          <h1 className="text-xl font-semibold text-text-pure">File carving and recovery</h1>
          <p className="mt-1 text-sm text-text-muted">
            Signature and structure based carving that scans raw or corrupted images without
            relying on file system metadata.
          </p>
        </div>
        <div className="flex items-center gap-2 text-xs text-text-muted">
          {scanning ? (
            <span className="flex items-center gap-1.5 text-status-warning">
              <Loader2 className="h-3.5 w-3.5 animate-spin" />
              Scanning, {Math.min(100, Math.round(scanPercent))}%
            </span>
          ) : results.length > 0 ? (
            <span className="text-status-valid">Scan complete</span>
          ) : (
            <span>Ready</span>
          )}
        </div>
      </header>

      <DiskImageSelector source={source} onSourceChange={setSource} disabled={scanning} />

      <main className="grid min-h-0 flex-1 grid-cols-1 gap-4 xl:grid-cols-[minmax(16rem,1fr)_minmax(0,2.2fr)]">
        <div className="flex flex-col gap-4">
          <RecoveryOptions signatures={signatures} onChange={setSignatures} disabled={scanning} />
          <button
            type="button"
            disabled={!canScan}
            onClick={runScan}
            className="flex items-center justify-center gap-2 rounded-md bg-status-valid px-3 py-3 text-sm font-medium text-background-main transition-colors hover:bg-status-valid/85 disabled:cursor-not-allowed disabled:bg-ui-selection disabled:text-text-muted"
          >
            <Search className="h-4 w-4" />
            {scanning ? 'Scanning image...' : `Scan with ${enabledCount} signature${enabledCount === 1 ? '' : 's'}`}
          </button>
        </div>

        <ResultsTable artifacts={results} />
      </main>
    </div>
  );
}