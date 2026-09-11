import { useEffect, useState } from 'react';

interface StatusBarProps {
  driveStatus?: string;
  progress?: number;
  ipcConnected?: boolean;
  sessionTime?: string;
}

function formatSeconds(totalSeconds: number): string {
  const hrs = Math.floor(totalSeconds / 3600);
  const mins = Math.floor((totalSeconds % 3600) / 60);
  const secs = totalSeconds % 60;
  return [hrs, mins, secs].map((v) => String(v).padStart(2, '0')).join(':');
}

export default function StatusBar({
  driveStatus = 'READY',
  progress = 0,
  ipcConnected,
  sessionTime,
}: StatusBarProps) {
  const [seconds, setSeconds] = useState(0);
  const [backendOk, setBackendOk] = useState(false);

  useEffect(() => {
    const timer = setInterval(() => {
      setSeconds((prev) => prev + 1);
    }, 1000);
    return () => clearInterval(timer);
  }, []);

  useEffect(() => {
    const checkBackend = async () => {
      try {
        let res = await fetch('http://127.0.0.1:5000/api/v1/health');
        if (!res.ok) {
          res = await fetch('http://127.0.0.1:5000/api/v1/devices');
        }
        setBackendOk(res.ok);
      } catch {
        setBackendOk(false);
      }
    };
    checkBackend();
    const interval = setInterval(checkBackend, 3000);
    return () => clearInterval(interval);
  }, []);

  const liveIpc = ipcConnected ?? (typeof window !== 'undefined' && Boolean(window.api) && backendOk);
  const displayTime = sessionTime ?? formatSeconds(seconds);

  return (
    <footer className="h-6 w-full bg-background-icon border-t border-ui-outline px-3 flex items-center justify-between font-mono text-xs text-text-muted select-none">
      {/* Left: Connectivity & Status */}
      <div className="flex items-center gap-3">
        {/* IPC Status */}
        <div
          className="flex items-center gap-1.5 cursor-help"
          title={
            liveIpc
              ? 'IPC Bridge & SanitizeX Native Backend Engine Connected'
              : 'IPC/Backend Engine Disconnected (Check backend daemon)'
          }
        >
          <div className={`status-dot ${liveIpc ? 'bg-status-valid' : 'bg-status-error'}`} />
          <span className="text-[10px] tracking-wider uppercase">
            {liveIpc ? 'IPC: OK' : 'IPC: DISCONNECTED'}
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
          <span className="text-text-pure">{displayTime}</span>
        </div>
      </div>
    </footer>
  );
}