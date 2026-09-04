import { AlertTriangle, Terminal, X } from 'lucide-react'
import type { EraseConfig } from './EraseConfigPanel'
import type { EraseTarget } from './SelectedFilesPanel'
import { useState } from 'react'
import type { ReactElement } from 'react'

interface EraseConfirmDialogProps {
  open: boolean
  targets: EraseTarget[]
  config: EraseConfig
  onCancel: () => void
  onConfirm: () => void
}

export function EraseConfirmDialog({
  open,
  targets,
  config,
  onCancel,
  onConfirm
}: EraseConfirmDialogProps): ReactElement | null {
  const [confirmation, setConfirmation] = useState('')
  if (!open) return null

  const canConfirm = confirmation === 'CONFIRM_ERASE'
  return (
    <div
      className="fixed inset-0 z-50 flex items-center justify-center bg-black/60 p-4 backdrop-blur-sm"
      role="dialog"
      aria-modal="true"
      aria-labelledby="erase-dialog-title"
    >
      <div className="w-full max-w-lg rounded-lg border border-status-error/40 bg-background-sidebar shadow-2xl">
        <div className="flex items-center justify-between border-b border-ui-outline px-4 py-3">
          <div className="flex items-center gap-2 text-status-error">
            <Terminal className="h-4 w-4" />
            <h2 id="erase-dialog-title" className="text-sm font-medium">
              Destructive operation
            </h2>
          </div>
          <button
            type="button"
            onClick={onCancel}
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
              This permanently removes recoverable content from {targets.length} selected
              target{targets.length === 1 ? '' : 's'}. Review the queue before continuing.
            </p>
          </div>
          <div className="grid grid-cols-2 gap-y-2 rounded-md border border-ui-outline bg-background-main p-3 text-sm">
            <span className="text-text-muted">Method</span>
            <span className="text-right text-text-pure">
              {config.overwriteMethod === 'zero' ? 'Zero fill' : 'Random'}, {config.passCount} pass
              {config.passCount === 1 ? '' : 'es'}
            </span>
            <span className="text-text-muted">Metadata</span>
            <span className="text-right text-text-pure">
              {config.clearMetadata ? 'Clear' : 'Retain'}
            </span>
            <span className="text-text-muted">Slack space</span>
            <span className="text-right text-text-pure">
              {config.wipeSlackSpace ? 'Wipe' : 'Retain'}
            </span>
          </div>
          <label className="block text-sm text-text-muted">
            Type <span className="font-medium text-status-error">CONFIRM_ERASE</span> to continue
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
              onClick={onCancel}
              className="rounded-md border border-ui-outline px-3 py-2 text-sm text-text-muted transition-colors hover:bg-ui-selection hover:text-text-pure"
            >
              Cancel
            </button>
            <button
              type="button"
              disabled={!canConfirm}
              onClick={onConfirm}
              className="rounded-md bg-status-error px-3 py-2 text-sm font-medium text-white transition-colors hover:bg-status-error/85 disabled:cursor-not-allowed disabled:bg-ui-selection disabled:text-text-muted"
            >
              Execute erase
            </button>
          </div>
        </div>
      </div>
    </div>
  )
}