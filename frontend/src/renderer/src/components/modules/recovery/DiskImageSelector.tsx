import React, { useRef, useState } from 'react';
import { FileArchive, HardDrive, UploadCloud, X } from 'lucide-react';

export type ImageSourceMode = 'file' | 'sectors';

export interface ImageSource {
  mode: ImageSourceMode;
  fileName?: string;
  fileSize?: string;
  devicePath?: string;
  startSector?: number;
  endSector?: number;
}

interface DiskImageSelectorProps {
  source: ImageSource | null;
  onSourceChange: (source: ImageSource | null) => void;
  disabled?: boolean;
}

export const DiskImageSelector: React.FC<DiskImageSelectorProps> = ({
  source,
  onSourceChange,
  disabled = false,
}) => {
  const [mode, setMode] = useState<ImageSourceMode>(source?.mode ?? 'file');
  const [isDragging, setIsDragging] = useState(false);
  const inputRef = useRef<HTMLInputElement>(null);

  const handleFiles = (files: FileList | null): void => {
    if (!files || files.length === 0) return;
    const file = files[0];
    const sizeMB = file.size / (1024 * 1024);
    const sizeLabel = sizeMB > 1024 ? `${(sizeMB / 1024).toFixed(2)} GB` : `${sizeMB.toFixed(1)} MB`;
    onSourceChange({ mode: 'file', fileName: file.name, fileSize: sizeLabel });
  };

  return (
    <section className="rounded-lg border border-ui-outline bg-background-sidebar">
      <div className="flex items-center justify-between border-b border-ui-outline px-4 py-3">
        <div>
          <h2 className="text-sm font-medium text-text-pure">Image source</h2>
          <p className="mt-0.5 text-xs text-text-muted">Raw disk dump or unallocated sector range</p>
        </div>
        <div className="flex items-center rounded-md border border-ui-outline bg-background-icon p-0.5 text-xs">
          <button
            type="button"
            disabled={disabled}
            onClick={() => setMode('file')}
            className={`rounded px-2.5 py-1 transition-colors disabled:cursor-not-allowed ${
              mode === 'file' ? 'bg-ui-selection text-text-pure' : 'text-text-muted hover:text-text-pure'
            }`}
          >
            Image file
          </button>
          <button
            type="button"
            disabled={disabled}
            onClick={() => setMode('sectors')}
            className={`rounded px-2.5 py-1 transition-colors disabled:cursor-not-allowed ${
              mode === 'sectors' ? 'bg-ui-selection text-text-pure' : 'text-text-muted hover:text-text-pure'
            }`}
          >
            Sector range
          </button>
        </div>
      </div>

      <div className="p-4">
        {mode === 'file' ? (
          source?.fileName ? (
            <div className="flex items-center justify-between rounded-md border border-ui-outline bg-background-main px-3 py-3">
              <div className="flex items-center gap-2.5">
                <FileArchive className="h-4 w-4 text-status-valid" />
                <div>
                  <p className="text-sm text-text-pure">{source.fileName}</p>
                  <p className="text-xs text-text-muted">{source.fileSize}</p>
                </div>
              </div>
              <button
                type="button"
                disabled={disabled}
                onClick={() => onSourceChange(null)}
                title="Remove image"
                className="rounded-md p-1.5 text-text-muted transition-colors hover:bg-status-error/15 hover:text-status-error disabled:cursor-not-allowed"
              >
                <X className="h-4 w-4" />
              </button>
            </div>
          ) : (
            <div
              onDragOver={(e) => {
                e.preventDefault();
                if (!disabled) setIsDragging(true);
              }}
              onDragLeave={() => setIsDragging(false)}
              onDrop={(e) => {
                e.preventDefault();
                setIsDragging(false);
                if (!disabled) handleFiles(e.dataTransfer.files);
              }}
              className={`flex flex-col items-center justify-center gap-2 rounded-md border border-dashed px-4 py-10 text-center transition-colors ${
                isDragging
                  ? 'border-status-valid bg-status-valid/5'
                  : 'border-ui-outline bg-background-main'
              }`}
            >
              <UploadCloud className="h-7 w-7 text-text-muted" />
              <p className="text-sm text-text-pure">
                Drag a disk image here, or{' '}
                <button
                  type="button"
                  disabled={disabled}
                  onClick={() => inputRef.current?.click()}
                  className="text-status-valid underline-offset-2 hover:underline disabled:cursor-not-allowed"
                >
                  browse files
                </button>
              </p>
              <p className="text-xs text-text-muted">Supports .dd, .img, .raw, and .bin images</p>
              <input
                ref={inputRef}
                type="file"
                accept=".dd,.img,.raw,.bin"
                className="hidden"
                disabled={disabled}
                onChange={(e) => handleFiles(e.target.files)}
              />
            </div>
          )
        ) : (
          <div className="space-y-3">
            <label className="block text-xs text-text-muted">
              Device path
              <input
                type="text"
                disabled={disabled}
                value={source?.devicePath ?? ''}
                onChange={(e) =>
                  onSourceChange({
                    mode: 'sectors',
                    devicePath: e.target.value,
                    startSector: source?.startSector ?? 0,
                    endSector: source?.endSector ?? 0,
                  })
                }
                placeholder="/dev/sdb1"
                className="mt-1.5 w-full rounded-md border border-ui-outline bg-background-main px-3 py-2 text-sm text-text-pure outline-none placeholder:text-text-muted/60 focus:border-status-valid disabled:cursor-not-allowed"
              />
            </label>
            <div className="grid grid-cols-2 gap-3">
              <label className="block text-xs text-text-muted">
                Start sector
                <input
                  type="number"
                  disabled={disabled}
                  value={source?.startSector ?? 0}
                  onChange={(e) =>
                    onSourceChange({
                      mode: 'sectors',
                      devicePath: source?.devicePath ?? '',
                      startSector: Number(e.target.value),
                      endSector: source?.endSector ?? 0,
                    })
                  }
                  className="mt-1.5 w-full rounded-md border border-ui-outline bg-background-main px-3 py-2 text-sm text-text-pure outline-none focus:border-status-valid disabled:cursor-not-allowed"
                />
              </label>
              <label className="block text-xs text-text-muted">
                End sector
                <input
                  type="number"
                  disabled={disabled}
                  value={source?.endSector ?? 0}
                  onChange={(e) =>
                    onSourceChange({
                      mode: 'sectors',
                      devicePath: source?.devicePath ?? '',
                      startSector: source?.startSector ?? 0,
                      endSector: Number(e.target.value),
                    })
                  }
                  className="mt-1.5 w-full rounded-md border border-ui-outline bg-background-main px-3 py-2 text-sm text-text-pure outline-none focus:border-status-valid disabled:cursor-not-allowed"
                />
              </label>
            </div>
            <div className="flex items-center gap-1.5 text-xs text-text-muted">
              <HardDrive className="h-3.5 w-3.5" />
              Scans the physical device directly without mounting a file system
            </div>
          </div>
        )}
      </div>
    </section>
  );
};