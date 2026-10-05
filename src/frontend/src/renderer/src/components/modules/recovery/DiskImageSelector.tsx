import React, { useRef, useState } from 'react';
import { FileArchive, FolderSearch, UploadCloud, X } from 'lucide-react';

export type ImageSourceMode = 'file' | 'directory';

export interface ImageSource {
  mode: ImageSourceMode;
  fileName?: string;
  filePath?: string;
  fileSize?: string;
  directoryPath?: string;
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
  const fileInputRef = useRef<HTMLInputElement>(null);
  const directoryInputRef = useRef<HTMLInputElement>(null);

  const handleFiles = (files: FileList | null): void => {
    if (!files || files.length === 0) return;
    const file = files[0] as any;
    const sizeMB = file.size / (1024 * 1024);
    const sizeLabel = sizeMB > 1024 ? `${(sizeMB / 1024).toFixed(2)} GB` : `${sizeMB.toFixed(1)} MB`;
    
    // Retrieve absolute path using Electron webUtils via IPC bridge
    const absolutePath = window.api?.getPathForFile ? window.api.getPathForFile(file) : file.path;
    
    onSourceChange({ mode: 'file', fileName: file.name, filePath: absolutePath, fileSize: sizeLabel });
  };

  const handleDirectory = (files: FileList | null): void => {
    if (!files || files.length === 0) return;
    const file = files[0] as any;
    const relativePath = file.webkitRelativePath;
    const folderName = relativePath ? relativePath.split('/')[0] : `${files.length} files selected`;
    
    let absoluteDirPath = folderName;
    if (file.path && relativePath) {
      const depth = relativePath.split('/').length - 1;
      let currentPath = file.path.replace(/\\/g, '/');
      for (let i = 0; i < depth; i++) {
        currentPath = currentPath.substring(0, currentPath.lastIndexOf('/'));
      }
      absoluteDirPath = currentPath;
    }

    onSourceChange({
      mode: 'directory',
      directoryPath: absoluteDirPath,
    });
  };

  const resetDirectory = (): void => {
    onSourceChange({ mode: 'directory' });
  };

  const switchMode = (next: ImageSourceMode): void => {
    setMode(next);
    onSourceChange({ mode: next });
  };

  return (
    <section className="rounded-lg border border-ui-outline bg-background-sidebar">
      <div className="flex items-center justify-between border-b border-ui-outline px-4 py-3">
        <div>
          <h2 className="text-sm font-medium text-text-pure">Image source</h2>
          <p className="mt-0.5 text-xs text-text-muted">
            Raw disk dump or a directory to recover
          </p>
        </div>
        <div className="flex items-center rounded-md border border-ui-outline bg-background-icon p-0.5 text-xs">
          <button
            type="button"
            disabled={disabled}
            onClick={() => switchMode('file')}
            className={`rounded px-2.5 py-1 transition-colors disabled:cursor-not-allowed ${
              mode === 'file' ? 'bg-ui-selection text-text-pure' : 'text-text-muted hover:text-text-pure'
            }`}
          >
            Image file
          </button>
          <button
            type="button"
            disabled={disabled}
            onClick={() => switchMode('directory')}
            className={`rounded px-2.5 py-1 transition-colors disabled:cursor-not-allowed ${
              mode === 'directory' ? 'bg-ui-selection text-text-pure' : 'text-text-muted hover:text-text-pure'
            }`}
          >
            Directory
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
                  onClick={() => fileInputRef.current?.click()}
                  className="text-status-valid underline-offset-2 hover:underline disabled:cursor-not-allowed"
                >
                  browse files
                </button>
              </p>
              <p className="text-xs text-text-muted">Supports .dd, .img, .raw, and .bin images</p>
              <input
                ref={fileInputRef}
                type="file"
                accept=".dd,.img,.raw,.bin"
                className="hidden"
                disabled={disabled}
                onChange={(e) => handleFiles(e.target.files)}
              />
            </div>
          )
        ) : source?.directoryPath ? (
          <div className="flex items-center justify-between rounded-md border border-ui-outline bg-background-main px-3 py-3">
            <div className="flex min-w-0 items-center gap-2.5">
              <FolderSearch className="h-4 w-4 shrink-0 text-text-muted" />
              <span className="truncate text-sm text-text-pure">{source.directoryPath}</span>
            </div>
            <button
              type="button"
              disabled={disabled}
              onClick={resetDirectory}
              title="Remove directory"
              className="rounded-md p-1.5 text-text-muted transition-colors hover:bg-status-error/15 hover:text-status-error disabled:cursor-not-allowed"
            >
              <X className="h-4 w-4" />
            </button>
          </div>
        ) : (
          <div className="flex flex-col items-center justify-center gap-2 rounded-md border border-dashed border-ui-outline bg-background-main px-4 py-10 text-center">
            <FolderSearch className="h-7 w-7 text-text-muted" />
            <p className="text-sm text-text-pure">
              <button
                type="button"
                disabled={disabled}
                onClick={() => directoryInputRef.current?.click()}
                className="text-status-valid underline-offset-2 hover:underline disabled:cursor-not-allowed"
              >
                Choose a directory
              </button>{' '}
              to recover drive
            </p>
            <p className="text-xs text-text-muted">
              Scans the folder tree without depending on file system metadata
            </p>
            <input
              ref={directoryInputRef}
              type="file"
              className="hidden"
              disabled={disabled}
              // @ts-expect-error non-standard attributes for directory selection
              webkitdirectory=""
              onChange={(e) => handleDirectory(e.target.files)}
            />
          </div>
        )}
      </div>
    </section>
  );
};