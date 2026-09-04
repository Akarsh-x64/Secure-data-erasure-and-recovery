import React, { useEffect, useRef, useState } from 'react';
import { AlertTriangle, HardDriveDownload, Terminal, X } from 'lucide-react';
import { DeviceSelector, type DriveDevice } from './DeviceSelector';
import { EraseMethodSelector, ERASE_STANDARDS, type EraseStandard } from './EraseMethodSelector';
import { EraseProgress, type EraseProgressData } from './EraseMethodProgress';

const INITIAL_DEVICES: DriveDevice[] = [
  {
    id: 'nvme0n1',
    path: '/dev/nvme0n1',
    model: 'Samsung 980 Pro NVMe SSD',
    serial: 'S6B2NJ0R500123',
    busType: 'NVMe',
    capacity: '512 GB',
    sectorSize: '512 B',
    health: 'healthy',
    mounted: false,
  },
  {
    id: 'sda',
    path: '/dev/sda',
    model: 'WD Blue 2.5" HDD',
    serial: 'WD-WXH2A93K4821',
    busType: 'SATA',
    capacity: '1 TB',
    sectorSize: '4 KB',
    health: 'warning',
    mounted: false,
  },
  {
    id: 'sdb',
    path: '/dev/sdb',
    model: 'SanDisk Ultra Flair',
    serial: '4C531001471109115627',
    busType: 'USB',
    capacity: '64 GB',
    sectorSize: '512 B',
    health: 'healthy',
    mounted: true,
  },
];

const CONFIRM_TOKEN = 'CONFIRM_WIPE';

function makeIdleProgress(passTotal: number): EraseProgressData {
  return {
    state: 'idle',
    passIndex: 0,
    passTotal,
    percent: 0,
    throughputMBps: 0,
    currentSector: 0,
    totalSectors: 1_000_215_216,
    etaSeconds: 0,
  };
}

export function DriveEraseTab(): React.ReactElement {
  const [devices] = useState<DriveDevice[]>(INITIAL_DEVICES);
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [standard, setStandard] = useState<EraseStandard>('nist-purge');
  const [dialogOpen, setDialogOpen] = useState(false);
  const [confirmation, setConfirmation] = useState('');
  const [progress, setProgress] = useState<EraseProgressData>(
    makeIdleProgress(ERASE_STANDARDS['nist-purge'].passes)
  );
  const intervalRef = useRef<ReturnType<typeof setInterval> | null>(null);

  const selectedDevice = devices.find((d) => d.id === selectedId) ?? null;
  const passTotal = ERASE_STANDARDS[standard].passes;
  const isRunning = progress.state === 'running' || progress.state === 'verifying';

  useEffect(() => {
    if (!isRunning) setProgress((p) => ({ ...makeIdleProgress(passTotal), state: p.state }));
  }, [standard]); // eslint-disable-line react-hooks/exhaustive-deps

  useEffect(() => {
    return () => {
      if (intervalRef.current) clearInterval(intervalRef.current);
    };
  }, []);

  const startErase = (): void => {
    setDialogOpen(false);
    setConfirmation('');
    const totalSectors = 1_000_215_216;
    let percent = 0;
    let passIndex = 1;

    setProgress({
      state: 'running',
      passIndex,
      passTotal,
      percent: 0,
      throughputMBps: 210,
      currentSector: 0,
      totalSectors,
      etaSeconds: 492,
    });

    intervalRef.current = setInterval(() => {
      percent += Math.random() * 4 + 2;
      if (percent >= 100) {
        if (passIndex < passTotal) {
          passIndex += 1;
          percent = 0;
        } else {
          if (intervalRef.current) clearInterval(intervalRef.current);
          setProgress((p) => ({
            ...p,
            state: 'complete',
            passIndex: passTotal,
            percent: 100,
            currentSector: totalSectors,
            etaSeconds: 0,
          }));
          return;
        }
      }
      const currentSector = Math.round((percent / 100) * totalSectors);
      const throughput = 180 + Math.round(Math.random() * 60);
      const remaining = totalSectors - currentSector;
      const eta = Math.max(0, Math.round(remaining / (throughput * 2000)));

      setProgress((p) => ({
        ...p,
        state: 'running',
        passIndex,
        percent,
        currentSector,
        throughputMBps: throughput,
        etaSeconds: eta,
      }));
    }, 450);
  };

  const resetRun = (): void => {
    if (intervalRef.current) clearInterval(intervalRef.current);
    setProgress(makeIdleProgress(passTotal));
  };

  const canConfirm = confirmation === CONFIRM_TOKEN;
  const canRequestErase = Boolean(selectedDevice) && !selectedDevice?.mounted && !isRunning;

  return (
    <div className="flex min-h-full flex-col gap-4 font-sans text-text-pure">
      <header className="flex flex-wrap items-end justify-between gap-3 border-b border-ui-outline pb-4">
        <div>
          <h1 className="text-xl font-semibold text-text-pure">Secure drive eraser</h1>
          <p className="mt-1 max-w-2xl text-sm text-text-muted">
            Sanitize raw storage media with verification passes and NIST 800-88 protocol
            validation.
          </p>
        </div>
        {progress.state === 'complete' && (
          <button
            type="button"
            onClick={resetRun}
            className="rounded-md border border-ui-outline px-3 py-2 text-sm text-text-muted transition-colors hover:bg-ui-selection hover:text-text-pure"
          >
            Start another run
          </button>
        )}
      </header>

      <main className="grid min-h-0 flex-1 grid-cols-1 gap-4 xl:grid-cols-[minmax(0,1.65fr)_minmax(20rem,1fr)]">
        <DeviceSelector
          devices={devices}
          selectedId={selectedId}
          onSelect={(device) => !isRunning && setSelectedId(device.id)}
          disabled={isRunning}
        />

        <div className="flex flex-col gap-4">
          <section className="rounded-lg border border-ui-outline bg-background-sidebar p-4">
            <EraseMethodSelector value={standard} onChange={setStandard} disabled={isRunning} />

            <button
              type="button"
              disabled={!canRequestErase}
              onClick={() => setDialogOpen(true)}
              className="mt-4 flex w-full items-center justify-center gap-2 rounded-md bg-status-error px-3 py-3 text-sm font-medium text-white transition-colors hover:bg-status-error/85 disabled:cursor-not-allowed disabled:bg-ui-selection disabled:text-text-muted"
            >
              <HardDriveDownload className="h-4 w-4" />
              {selectedDevice ? `Sanitize ${selectedDevice.path}` : 'Select a device to continue'}
            </button>
          </section>

          <EraseProgress progress={progress} />
        </div>
      </main>

      {dialogOpen && selectedDevice && (
        <div
          className="fixed inset-0 z-50 flex items-center justify-center bg-black/60 p-4 backdrop-blur-sm"
          role="dialog"
          aria-modal="true"
          aria-labelledby="drive-erase-dialog-title"
        >
          <div className="w-full max-w-lg rounded-lg border border-status-error/40 bg-background-sidebar shadow-2xl">
            <div className="flex items-center justify-between border-b border-ui-outline px-4 py-3">
              <div className="flex items-center gap-2 text-status-error">
                <Terminal className="h-4 w-4" />
                <h2 id="drive-erase-dialog-title" className="text-sm font-medium">
                  Destructive operation
                </h2>
              </div>
              <button
                type="button"
                onClick={() => setDialogOpen(false)}
                title="Close confirmation"
                className="rounded-md p-1 text-text-muted transition-colors hover:bg-ui-selection hover:text-text-pure"
              >
                <X className="h-4 w-4" />
              </button>
            </div>
            <div className="space-y-4 p-4">
              <div className="flex gap-3 rounded-md border border-status-warning/40 bg-status-warning/10 p-3 text-sm text-status-warning">
                <AlertTriangle className="h-4 w-4 shrink-0" />
                <p>
                  This permanently destroys all data on {selectedDevice.path} ({selectedDevice.capacity}
                  ). This cannot be undone.
                </p>
              </div>
              <div className="grid grid-cols-2 gap-y-2 rounded-md border border-ui-outline bg-background-main p-3 text-sm">
                <span className="text-text-muted">Device</span>
                <span className="text-right text-text-pure">{selectedDevice.model}</span>
                <span className="text-text-muted">Standard</span>
                <span className="text-right text-text-pure">{ERASE_STANDARDS[standard].label}</span>
                <span className="text-text-muted">Passes</span>
                <span className="text-right text-text-pure">{passTotal}</span>
              </div>
              <label className="block text-sm text-text-muted">
                Type <span className="font-medium text-status-error">{CONFIRM_TOKEN}</span> to
                continue
                <input
                  autoFocus
                  value={confirmation}
                  onChange={(event) => setConfirmation(event.target.value)}
                  placeholder="Awaiting confirmation..."
                  className="mt-2 w-full rounded-md border border-ui-outline bg-background-main px-3 py-2 text-sm text-text-pure outline-none placeholder:text-text-muted/50 focus:border-status-error"
                />
              </label>
              <div className="flex justify-end gap-2 pt-1">
                <button
                  type="button"
                  onClick={() => setDialogOpen(false)}
                  className="rounded-md border border-ui-outline px-3 py-2 text-sm text-text-muted transition-colors hover:bg-ui-selection hover:text-text-pure"
                >
                  Cancel
                </button>
                <button
                  type="button"
                  disabled={!canConfirm}
                  onClick={startErase}
                  className="rounded-md bg-status-error px-3 py-2 text-sm font-medium text-white transition-colors hover:bg-status-error/85 disabled:cursor-not-allowed disabled:bg-ui-selection disabled:text-text-muted"
                >
                  Execute erase
                </button>
              </div>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}