import React, { useMemo } from 'react';
import { Activity, CheckCircle2, Gauge, Loader2, Timer } from 'lucide-react';

export type EraseRunState = 'idle' | 'running' | 'verifying' | 'complete';

export interface EraseProgressData {
  state: EraseRunState;
  passIndex: number;
  passTotal: number;
  percent: number;
  throughputMBps: number;
  currentSector: number;
  totalSectors: number;
  etaSeconds: number;
}

interface EraseProgressProps {
  progress: EraseProgressData;
}

const SEGMENT_COUNT = 48;

function formatEta(seconds: number): string {
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = Math.floor(seconds % 60);
  return [h, m, s].map((n) => n.toString().padStart(2, '0')).join(':');
}

function formatSectors(n: number): string {
  return n.toLocaleString('en-US');
}

export const EraseProgress: React.FC<EraseProgressProps> = ({ progress }) => {
  const { state, passIndex, passTotal, percent, throughputMBps, currentSector, totalSectors, etaSeconds } =
    progress;

  const filledSegments = useMemo(
    () => Math.round((percent / 100) * SEGMENT_COUNT),
    [percent]
  );

  const stateLabel =
    state === 'idle'
      ? 'Idle'
      : state === 'running'
        ? 'Sanitizing'
        : state === 'verifying'
          ? 'Verifying'
          : 'Complete';

  const stateColor =
    state === 'complete'
      ? 'text-status-valid'
      : state === 'idle'
        ? 'text-text-muted'
        : 'text-status-warning';

  return (
    <section className="flex flex-col rounded-lg border border-ui-outline bg-background-sidebar">
      <div className="flex items-center justify-between border-b border-ui-outline px-4 py-3">
        <div>
          <h2 className="text-sm font-medium text-text-pure">Sector progress</h2>
          <p className="mt-0.5 text-xs text-text-muted">Live throughput and pass verification</p>
        </div>
        <span className={`flex items-center gap-1.5 text-xs ${stateColor}`}>
          {state === 'running' || state === 'verifying' ? (
            <Loader2 className="h-3.5 w-3.5 animate-spin" />
          ) : state === 'complete' ? (
            <CheckCircle2 className="h-3.5 w-3.5" />
          ) : (
            <span className="h-1.5 w-1.5 rounded-full bg-text-muted" />
          )}
          {stateLabel}
        </span>
      </div>

      <div className="space-y-5 p-4">
        {/* Pass indicator */}
        <div className="flex items-center gap-2">
          {Array.from({ length: passTotal }).map((_, idx) => {
            const passNum = idx + 1;
            const isDone = passNum < passIndex || state === 'complete';
            const isActive = passNum === passIndex && state !== 'complete';
            return (
              <div key={idx} className="flex flex-1 items-center gap-2">
                <div
                  className={`flex h-6 w-6 shrink-0 items-center justify-center rounded-full border text-xs ${
                    isDone
                      ? 'border-status-valid bg-status-valid/15 text-status-valid'
                      : isActive
                        ? 'border-status-warning bg-status-warning/15 text-status-warning'
                        : 'border-ui-outline text-text-muted'
                  }`}
                >
                  {isDone ? <Check_ /> : passNum}
                </div>
                {idx < passTotal - 1 && (
                  <div
                    className={`h-px flex-1 ${isDone ? 'bg-status-valid/50' : 'bg-ui-outline'}`}
                  />
                )}
              </div>
            );
          })}
        </div>
        <p className="-mt-3 text-xs text-text-muted">
          Pass {Math.min(passIndex, passTotal)} of {passTotal}
        </p>

        {/* Block sector visualizer */}
        <div>
          <div className="mb-2 flex items-center justify-between text-xs text-text-muted">
            <span>Sector map</span>
            <span className="text-text-pure">{percent.toFixed(1)}%</span>
          </div>
          <div className="grid grid-cols-12 gap-1 sm:grid-cols-16">
            {Array.from({ length: SEGMENT_COUNT }).map((_, idx) => (
              <div
                key={idx}
                className={`h-3 rounded-sm transition-colors ${
                  idx < filledSegments
                    ? state === 'complete'
                      ? 'bg-status-valid'
                      : 'bg-status-warning'
                    : 'bg-ui-outline'
                }`}
              />
            ))}
          </div>
          <p className="mt-2 text-xs text-text-muted">
            Sector {formatSectors(currentSector)} of {formatSectors(totalSectors)}
          </p>
        </div>

        {/* Stats grid */}
        <div className="grid grid-cols-3 gap-2.5">
          <div className="rounded-md border border-ui-outline bg-background-main p-3">
            <div className="flex items-center gap-1.5 text-xs text-text-muted">
              <Gauge className="h-3.5 w-3.5" /> Throughput
            </div>
            <p className="mt-1.5 text-sm text-text-pure">{throughputMBps} MB/s</p>
          </div>
          <div className="rounded-md border border-ui-outline bg-background-main p-3">
            <div className="flex items-center gap-1.5 text-xs text-text-muted">
              <Activity className="h-3.5 w-3.5" /> Passes
            </div>
            <p className="mt-1.5 text-sm text-text-pure">
              {passIndex} / {passTotal}
            </p>
          </div>
          <div className="rounded-md border border-ui-outline bg-background-main p-3">
            <div className="flex items-center gap-1.5 text-xs text-text-muted">
              <Timer className="h-3.5 w-3.5" /> Time remaining
            </div>
            <p className="mt-1.5 text-sm text-text-pure">
              {state === 'complete' ? '00:00:00' : formatEta(etaSeconds)}
            </p>
          </div>
        </div>
      </div>
    </section>
  );
};

// Small inline check glyph kept separate so the pass dots stay visually compact
function Check_(): React.ReactElement {
  return (
    <svg viewBox="0 0 24 24" className="h-3 w-3" fill="none" stroke="currentColor" strokeWidth={3}>
      <path d="M20 6 9 17l-5-5" strokeLinecap="round" strokeLinejoin="round" />
    </svg>
  );
}