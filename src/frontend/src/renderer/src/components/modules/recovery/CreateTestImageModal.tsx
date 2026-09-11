import React, { useState } from 'react';
import {
  HardDrive,
  Loader2,
  X,
  Check,
  Disc
} from 'lucide-react';

export type FilesystemChoice = 'NTFS' | 'FAT32' | 'exFAT' | 'ext4';

interface CreateTestImageModalProps {
  open: boolean;
  onClose: () => void;
  onImageCreated: (imagePath: string, filesystem: FilesystemChoice) => void;
}

interface FilesystemOption {
  id: FilesystemChoice;
  name: string;
  badge: string;
  description: string;
}

const FILESYSTEM_OPTIONS: FilesystemOption[] = [
  {
    id: 'FAT32',
    name: 'FAT32 (Recommended)',
    badge: 'Flash Format',
    description: 'Standard USB drive format. Ideal for testing file deletion and recovery.'
  },
  {
    id: 'exFAT',
    name: 'exFAT',
    badge: 'High Capacity',
    description: 'Modern flash drive format supporting large files.'
  },
  {
    id: 'NTFS',
    name: 'NTFS',
    badge: 'Windows Default',
    description: 'Standard Windows system volume format.'
  },
  {
    id: 'ext4',
    name: 'ext4',
    badge: 'Linux Format',
    description: 'Standard Linux volume format.'
  }
];

export const CreateTestImageModal: React.FC<CreateTestImageModalProps> = ({
  open,
  onClose,
  onImageCreated
}) => {
  const [selectedFs, setSelectedFs] = useState<FilesystemChoice>('FAT32');
  const [sizeMb, setSizeMb] = useState<number>(500);
  const [creating, setCreating] = useState(false);
  const [errorMsg, setErrorMsg] = useState<string | null>(null);

  if (!open) return null;

  const handleCreate = async (): Promise<void> => {
    setCreating(true);
    setErrorMsg(null);
    try {
      let res: any = null;
      if (window.api?.createTestImage) {
        res = await window.api.createTestImage({
          filesystem: selectedFs,
          sizeMb,
          label: `SanitizeX_${selectedFs}`
        });
      }

      if (!res || !res.success) {
        // Direct HTTP fallback to backend server
        const httpRes = await fetch('http://localhost:5000/api/v1/create-test-image', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({
            filesystem: selectedFs,
            sizeMb,
            label: `SanitizeX_${selectedFs}`
          })
        });
        if (httpRes.ok) {
          res = await httpRes.json();
        }
      }

      if (res && res.success && res.vhdPath) {
        onImageCreated(res.vhdPath, selectedFs);
        onClose();
      } else {
        setErrorMsg(res?.error || 'Failed to create test disk image.');
      }
    } catch (err: any) {
      console.error('[CreateTestImageModal] Error:', err);
      setErrorMsg(err?.message || 'Unexpected error creating test disk image.');
    } finally {
      setCreating(false);
    }
  };

  return (
    <div
      className="fixed inset-0 z-50 flex items-center justify-center bg-black/75 p-4 backdrop-blur-md animate-in fade-in duration-200"
      role="dialog"
      aria-modal="true"
      aria-labelledby="create-image-title"
    >
      <div className="flex flex-col w-full max-w-xl rounded-xl border border-ui-outline bg-background-sidebar shadow-2xl overflow-hidden font-sans text-text-pure">
        {/* Header */}
        <div className="flex items-center justify-between border-b border-ui-outline px-5 py-4 bg-background-main/80">
          <div className="flex items-center gap-3">
            <div className="flex h-10 w-10 items-center justify-center rounded-lg border border-button-primary/40 bg-button-primary/15 text-button-primary">
              <HardDrive className="h-6 w-6" />
            </div>
            <div>
              <div className="flex items-center gap-2">
                <h2 id="create-image-title" className="text-base font-semibold text-text-pure">
                  Create Test Disk Image
                </h2>
                <span className="rounded-full border border-button-primary/40 bg-button-primary/10 px-2 py-0.5 text-[11px] font-medium text-button-primary">
                  VHD / Image Generator
                </span>
              </div>
              <p className="text-xs text-text-muted">
                Create a virtual test disk formatted with a custom filesystem to test deletion & recovery
              </p>
            </div>
          </div>
          <button
            type="button"
            disabled={creating}
            onClick={onClose}
            title="Close"
            className="rounded-lg p-1.5 text-text-muted transition-colors hover:bg-ui-selection hover:text-text-pure disabled:opacity-40"
          >
            <X className="h-5 w-5" />
          </button>
        </div>

        {/* Content */}
        <div className="p-5 space-y-5 overflow-y-auto max-h-[75vh]">
          {/* Filesystem Selection */}
          <div className="space-y-2.5">
            <div className="flex items-center justify-between">
              <label className="text-xs font-semibold uppercase tracking-wider text-text-muted">
                Target Filesystem
              </label>
              <span className="text-xs text-text-muted">Select filesystem format</span>
            </div>

            <div className="grid grid-cols-1 gap-2.5 sm:grid-cols-2">
              {FILESYSTEM_OPTIONS.map((opt) => {
                const isSelected = selectedFs === opt.id;
                return (
                  <button
                    key={opt.id}
                    type="button"
                    disabled={creating}
                    onClick={() => setSelectedFs(opt.id)}
                    className={`flex flex-col justify-between rounded-lg border p-3.5 text-left transition-all ${
                      isSelected
                        ? 'border-button-primary bg-button-primary/10 text-text-pure shadow-[inset_0_0_0_1px_rgba(59,130,246,0.3)]'
                        : 'border-ui-outline bg-background-main hover:border-ui-outline/80 hover:bg-ui-selection/30 text-text-muted'
                    }`}
                  >
                    <div className="flex items-center justify-between w-full mb-1.5">
                      <span className="text-sm font-semibold text-text-pure flex items-center gap-1.5">
                        <Disc className="h-4 w-4 text-button-primary shrink-0" />
                        {opt.id}
                      </span>
                      {isSelected && <Check className="h-4 w-4 text-button-primary shrink-0" />}
                    </div>

                    <p className="text-xs text-text-muted leading-relaxed mb-2">
                      {opt.description}
                    </p>

                    <span className="inline-self-start text-[10px] font-medium px-2 py-0.5 rounded bg-button-primary/15 text-button-primary border border-button-primary/30">
                      {opt.badge}
                    </span>
                  </button>
                );
              })}
            </div>
          </div>

          {/* Size Capacity Selection */}
          <div className="space-y-2">
            <label className="text-xs font-semibold uppercase tracking-wider text-text-muted block">
              Image Storage Capacity
            </label>
            <div className="grid grid-cols-3 gap-2">
              {[250, 500, 1000].map((sz) => (
                <button
                  key={sz}
                  type="button"
                  disabled={creating}
                  onClick={() => setSizeMb(sz)}
                  className={`rounded-lg border py-2.5 px-3 text-center text-xs font-medium transition-colors ${
                    sizeMb === sz
                      ? 'border-button-primary bg-button-primary/15 text-text-pure'
                      : 'border-ui-outline bg-background-main text-text-muted hover:text-text-pure hover:bg-ui-selection/40'
                  }`}
                >
                  {sz >= 1000 ? `${sz / 1000} GB (1000 MB)` : `${sz} MB`}
                </button>
              ))}
            </div>
          </div>

          {/* Error Notice */}
          {errorMsg && (
            <div className="rounded-lg border border-status-error/40 bg-status-error/10 p-3 text-xs text-status-error">
              {errorMsg}
            </div>
          )}
        </div>

        {/* Footer */}
        <div className="flex items-center justify-between border-t border-ui-outline px-5 py-3.5 bg-background-main/80">
          <p className="text-xs text-text-muted">
            Mounted as virtual drive in <code className="text-text-pure">%TEMP%</code>
          </p>
          <div className="flex gap-2">
            <button
              type="button"
              disabled={creating}
              onClick={onClose}
              className="rounded-lg border border-ui-outline px-3.5 py-2 text-xs font-medium text-text-muted transition-colors hover:bg-ui-selection hover:text-text-pure disabled:opacity-40"
            >
              Cancel
            </button>
            <button
              type="button"
              disabled={creating}
              onClick={handleCreate}
              className="flex items-center gap-2 rounded-lg border border-button-primary bg-button-primary px-4 py-2 text-xs font-medium text-button-primary-text transition-colors hover:bg-button-primary/85 disabled:opacity-50"
            >
              {creating && <Loader2 className="h-3.5 w-3.5 animate-spin" />}
              {creating ? 'Creating & Mounting...' : `Create ${selectedFs} Test Disk`}
            </button>
          </div>
        </div>
      </div>
    </div>
  );
};
