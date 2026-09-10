import React from 'react';
import { HardDrive, Usb, Cable, AlertCircle, CheckCircle2, Lock, Server, Cpu, AlertTriangle } from 'lucide-react';

export type BusType = 'SATA' | 'NVMe' | 'USB' | 'SCSI' | 'Virtual' | 'unknown';
export type DriveHealth = 'healthy' | 'warning' | 'critical' | 'unknown';

export interface DriveDevice {
  id: string;
  path: string;
  model: string;
  serial: string;
  busType: BusType;
  capacity: string;
  capacityBytes?: number;
  sectorSize: string;
  health: DriveHealth;
  mounted: boolean;
  filesystem?: string;
  isSystem?: boolean;
  volumeLabel?: string;
  identityToken?: string;
}

interface DeviceSelectorProps {
  devices: DriveDevice[];
  selectedId: string | null;
  onSelect: (device: DriveDevice) => void;
  disabled?: boolean;
}

const busIcon: Record<BusType, React.ReactNode> = {
  NVMe: <HardDrive className="h-4 w-4" />,
  SATA: <Cable className="h-4 w-4" />,
  USB: <Usb className="h-4 w-4" />,
  SCSI: <Server className="h-4 w-4" />,
  Virtual: <Cpu className="h-4 w-4" />,
  unknown: <HardDrive className="h-4 w-4" />,
};

const healthConfig: Record<DriveHealth, { label: string; color: string; dot: string }> = {
  healthy: { label: 'Healthy', color: 'text-status-valid', dot: 'bg-status-valid' },
  warning: { label: 'Degraded', color: 'text-status-warning', dot: 'bg-status-warning' },
  critical: { label: 'Critical', color: 'text-status-error', dot: 'bg-status-error' },
  unknown: { label: 'Active', color: 'text-text-muted', dot: 'bg-text-muted' },
};

export const DeviceSelector: React.FC<DeviceSelectorProps> = ({
  devices,
  selectedId,
  onSelect,
  disabled = false,
}) => {
  return (
    <section className="flex min-h-0 flex-1 flex-col rounded-lg border border-ui-outline bg-background-sidebar">
      <div className="border-b border-ui-outline px-4 py-3">
        <h2 className="text-sm font-medium text-text-pure">Target storage volumes & drives</h2>
        <p className="mt-0.5 text-xs text-text-muted">
          {devices.length} {devices.length === 1 ? 'target' : 'targets'} discovered on host system
        </p>
      </div>

      <div className="min-h-0 flex-1 overflow-auto p-3">
        <div className="grid grid-cols-1 gap-2.5 sm:grid-cols-2">
          {devices.map((device) => {
            const isSelected = device.id === selectedId;
            const health = healthConfig[device.health] || healthConfig.healthy;

            return (
              <button
                key={device.id}
                type="button"
                disabled={disabled}
                onClick={() => onSelect(device)}
                className={`flex flex-col gap-3 rounded-md border p-3 text-left transition-colors disabled:cursor-not-allowed disabled:opacity-50 ${
                  isSelected
                    ? 'border-status-valid/50 bg-status-valid/10'
                    : 'border-ui-outline bg-background-main hover:bg-ui-selection/40'
                }`}
              >
                <div className="flex items-start justify-between gap-2">
                  <div className="flex items-center gap-2 text-text-pure">
                    <span className="text-text-muted">{busIcon[device.busType] || busIcon.unknown}</span>
                    <span className="text-sm font-medium">{device.path}</span>
                  </div>
                  <div className="flex items-center gap-1.5">
                    {device.filesystem && (
                      <span className="rounded-full border border-button-primary/40 bg-button-primary/10 px-2 py-0.5 text-xs font-medium text-button-primary">
                        {device.filesystem}
                      </span>
                    )}
                    <span className="rounded-full border border-ui-outline bg-background-icon px-2 py-0.5 text-xs text-text-muted">
                      {device.busType}
                    </span>
                  </div>
                </div>

                <p className="truncate text-xs text-text-muted">{device.model}</p>

                <div className="flex items-center justify-between text-xs text-text-muted">
                  <span>{device.capacity}</span>
                  <span>{device.sectorSize} sectors</span>
                </div>

                <div className="flex items-center justify-between border-t border-ui-outline pt-2.5 text-xs">
                  <span className="text-text-muted">S/N {device.serial}</span>
                  <span className={`flex items-center gap-1.5 ${health.color}`}>
                    <span className={`h-1.5 w-1.5 rounded-full ${health.dot}`} />
                    {health.label}
                  </span>
                </div>

                {device.isSystem ? (
                  <div className="flex items-center gap-1.5 rounded-md bg-status-error/15 px-2 py-1 text-xs text-status-error font-medium">
                    <AlertTriangle className="h-3.5 w-3.5 shrink-0" />
                    Windows System Boot Drive (Critical)
                  </div>
                ) : device.mounted ? (
                  <div className="flex items-center gap-1.5 rounded-md bg-status-valid/10 px-2 py-1 text-xs text-status-valid">
                    <Lock className="h-3 w-3 shrink-0" />
                    Mounted Volume • Ready for Surgical Wipe
                  </div>
                ) : null}
              </button>
            );
          })}

          {devices.length === 0 && (
            <div className="col-span-full flex flex-col items-center gap-2 py-14 text-center text-sm text-text-muted">
              <AlertCircle className="h-6 w-6 opacity-50" />
              No storage devices detected
            </div>
          )}
        </div>
      </div>

      {selectedId && (
        <div className="flex items-center gap-1.5 border-t border-ui-outline px-4 py-2.5 text-xs text-status-valid">
          <CheckCircle2 className="h-3.5 w-3.5" />
          Device selected and ready for a sanitization profile
        </div>
      )}
    </section>
  );
};