import { useState } from 'react'
import type { ReactElement } from 'react'
import { FolderPlus, PanelLeftClose, PanelLeftOpen, Plus } from 'lucide-react'
import { EraseConfirmDialog } from './EraseConfirmDialog'
import { EraseConfigPanel, type EraseConfig } from './EraseConfigPanel'
import { SelectedFilesPanel, type EraseTarget, type FileSystemType } from './SelectedFilesPanel'

const INITIAL_TARGETS: EraseTarget[] = [
  {
    id: 'audit-log',
    path: '/var/log/audit/system_audit.log',
    kind: 'file',
    clusterSize: '4 KB',
    fileSystem: 'ext4',
    size: '512 KB'
  },
  {
    id: 'sec-dump',
    path: 'D:\\Evidence\\sec_event.dd',
    kind: 'file',
    clusterSize: '4 KB',
    fileSystem: 'NTFS',
    size: '2.1 GB'
  },
  {
    id: 'temp-export',
    path: 'D:\\Evidence\\recovered_export',
    kind: 'folder',
    clusterSize: '32 KB',
    fileSystem: 'FAT32',
    size: '14.2 MB'
  }
]

const INITIAL_CONFIG: EraseConfig = {
  clearMetadata: true,
  wipeSlackSpace: true,
  overwriteMethod: 'zero',
  passCount: 1
}

function guessFileSystem(path: string): FileSystemType {
  return path.startsWith('/') ? 'ext4' : 'NTFS'
}

interface FileEraseTabProps {
  explorerOpen: boolean
  onToggleExplorer: () => void
}

export function FileEraseTab({ explorerOpen, onToggleExplorer }: FileEraseTabProps): ReactElement {
  const [targets, setTargets] = useState<EraseTarget[]>(INITIAL_TARGETS)
  const [config, setConfig] = useState<EraseConfig>(INITIAL_CONFIG)
  const [pathInput, setPathInput] = useState('')
  const [dialogOpen, setDialogOpen] = useState(false)
  const [executionState, setExecutionState] = useState<'ready' | 'dispatching' | 'dispatched'>(
    'ready'
  )

  const addTarget = (kind: EraseTarget['kind']): void => {
    const path = pathInput.trim()
    if (!path) return
    setTargets((current) => [
      ...current,
      {
        id: `${kind}-${Date.now()}`,
        path,
        kind,
        clusterSize: '4 KB',
        fileSystem: guessFileSystem(path),
        size: 'Pending scan'
      }
    ])
    setPathInput('')
  }

  const executeErase = async (): Promise<void> => {
    setDialogOpen(false)
    setExecutionState('dispatching')
    if (window.api.eraseFiles) await window.api.eraseFiles({ targets, config })
    setExecutionState('dispatched')
  }

  const statusLabel =
    executionState === 'ready'
      ? 'Ready'
      : executionState === 'dispatching'
        ? 'Dispatching request'
        : 'Request accepted'

  const statusColor =
    executionState === 'dispatched'
      ? 'bg-status-valid'
      : executionState === 'dispatching'
        ? 'bg-status-warning'
        : 'bg-text-muted'

  return (
    <div className="flex min-h-full flex-col gap-4 font-sans text-text-pure">
      <header className="flex flex-wrap items-end justify-between gap-3 border-b border-ui-outline pb-4">
        <div>
          <h1 className="text-xl font-semibold text-text-pure">File and folder eraser</h1>
          <p className="mt-1 max-w-2xl text-sm text-text-muted">
            Selective logical sanitization with an auditable NIST 800-88 clear profile.
          </p>
        </div>
        <div className="flex items-center gap-3 text-xs text-text-muted">
          <button
            type="button"
            onClick={onToggleExplorer}
            title={explorerOpen ? 'Hide explorer' : 'Show explorer'}
            aria-label={explorerOpen ? 'Hide explorer' : 'Show explorer'}
            className="flex items-center gap-1.5 border border-ui-outline px-2 py-1.5 transition-colors hover:bg-ui-selection hover:text-text-pure"
          >
            {explorerOpen ? <PanelLeftClose className="h-4 w-4" /> : <PanelLeftOpen className="h-4 w-4" />}
            <span className="hidden sm:inline">Explorer</span>
          </button>
          <span className={`h-1.5 w-1.5 rounded-full ${statusColor}`} />
          <span>{statusLabel}</span>
        </div>
      </header>

      <div className="flex flex-col gap-3 rounded-lg border border-ui-outline bg-background-sidebar p-3 lg:flex-row lg:items-center">
        <div className="flex flex-1 items-center gap-2">
          <span className="shrink-0 text-sm text-text-muted">Path</span>
          <input
            value={pathInput}
            onChange={(event) => setPathInput(event.target.value)}
            onKeyDown={(event) => {
              if (event.key === 'Enter') addTarget('file')
            }}
            placeholder="/path/to/file or D:\folder"
            className="min-w-0 flex-1 rounded-md border border-ui-outline bg-background-main px-3 py-2 text-sm text-text-pure outline-none placeholder:text-text-muted/60 focus:border-status-valid"
          />
        </div>
        <div className="flex gap-2">
          <button
            type="button"
            onClick={() => addTarget('file')}
            className="flex items-center gap-1.5 rounded-md border border-ui-outline px-3 py-2 text-sm text-text-muted transition-colors hover:bg-ui-selection hover:text-text-pure"
          >
            <Plus className="h-4 w-4" /> File
          </button>
          <button
            type="button"
            onClick={() => addTarget('folder')}
            className="flex items-center gap-1.5 rounded-md border border-ui-outline px-3 py-2 text-sm text-text-muted transition-colors hover:bg-ui-selection hover:text-text-pure"
          >
            <FolderPlus className="h-4 w-4" /> Folder
          </button>
        </div>
      </div>

      <main className="grid min-h-0 flex-1 grid-cols-1 gap-4 xl:grid-cols-[minmax(0,1.65fr)_minmax(20rem,1fr)]">
        <SelectedFilesPanel
          targets={targets}
          onRemove={(id) => setTargets((current) => current.filter((target) => target.id !== id))}
        />
        <EraseConfigPanel
          config={config}
          onChange={setConfig}
          targetCount={targets.length}
          onRequestErase={() => setDialogOpen(true)}
        />
      </main>
      <EraseConfirmDialog
        open={dialogOpen}
        targets={targets}
        config={config}
        onCancel={() => setDialogOpen(false)}
        onConfirm={executeErase}
      />
    </div>
  )
}