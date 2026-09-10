interface StatusBarProps {
  driveStatus?: string;
  progress?: number;
  ipcConnected?: boolean;
  sessionTime?: string;
}

export default function StatusBar({
  driveStatus = 'READY',
  progress = 0,
  ipcConnected = true,
  sessionTime = '00:14:22',
}: StatusBarProps) {
  return (
    <footer className="h-6 w-full bg-background-icon border-t border-ui-outline px-3 flex items-center justify-between font-mono text-xs text-text-muted select-none">
      {/* Left: Connectivity & Status */}
      <div className="flex items-center gap-3">
        {/* IPC Status */}
        <div className="flex items-center gap-1.5" title="IPC Socket Connection">
          <div className={`status-dot ${ipcConnected ? 'bg-status-valid' : 'bg-status-error'}`} />
          <span className="text-[10px] tracking-wider uppercase">
            {ipcConnected ? 'IPC: OK' : 'IPC: DISCONNECTED'}
          </span>
        </div>

        <div className="w-[1px] h-3 bg-ui-outline" />

        {/* Target Drive Status */}
        <div className="flex items-center gap-1.5">
          <span>DRIVE:</span>
          <span className="text-text-pure font-bold">{driveStatus}</span>
        </div>
      </div>

      {/* Right: Progress & Session Info */}
      <div className="flex items-center gap-4">
        {/* Active Progress Readout */}
        {progress > 0 && (
          <div className="flex items-center gap-2">
            <span>OP:</span>
            <div className="w-16 h-1.5 border border-ui-outline bg-background-main p-[1px]">
              <div className="h-full bg-status-warning" style={{ width: `${progress}%` }} />
            </div>
            <span className="text-text-pure">{progress}%</span>
          </div>
        )}

        <div className="w-[1px] h-3 bg-ui-outline" />

        {/* Session Uptime */}
        <div className="flex items-center gap-1">
          <span>TIME:</span>
          <span className="text-text-pure">{sessionTime}</span>
        </div>
      </div>
    </footer>
  );
}