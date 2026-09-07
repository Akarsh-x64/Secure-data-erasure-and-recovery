import { useEffect, useState } from 'react'
import type { ReactElement } from 'react'
import type { ForensicNode } from '../../filetree/TreeNode'
import { EraseConfirmDialog } from './EraseConfirmDialog'
import { EraseConfigPanel, type EraseConfig } from './EraseConfigPanel'
import { SelectedFilesPanel, type EraseTarget, type FileSystemType } from './SelectedFilesPanel'

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
  treeTarget?: ForensicNode | null
  onTargetRemoved?: (id: string) => void
  queueResetToken?: number
}

export function FileEraseTab({ treeTarget, onTargetRemoved, queueResetToken = 0 }: FileEraseTabProps): ReactElement {
  const [targets, setTargets] = useState<EraseTarget[]>([])
  const [config, setConfig] = useState<EraseConfig>(INITIAL_CONFIG)
  const [dialogOpen, setDialogOpen] = useState(false)
  const [executionState, setExecutionState] = useState<'ready' | 'dispatching' | 'dispatched'>(
    'ready'
  )

  useEffect(() => {
    if (!treeTarget || targets.some((target) => target.id === treeTarget.id)) return
    // The tree action is an external event delivered through the prop.
    // eslint-disable-next-line react-hooks/set-state-in-effect
    setTargets((current) => [
      ...current,
      {
        id: treeTarget.id,
        path: treeTarget.name,
        kind: treeTarget.isDirectory ? 'folder' : 'file',
        clusterSize: '4 KB',
        fileSystem: guessFileSystem(treeTarget.name),
        size: treeTarget.size ?? 'Pending scan'
      }
    ])
  }, [targets, treeTarget])

  useEffect(() => {
    setTargets([])
    setExecutionState('ready')
  }, [queueResetToken])

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
        {executionState !== 'ready' && (
          <div className="flex items-center gap-3 text-xs text-text-muted">
            <span className={`h-1.5 w-1.5 rounded-full ${statusColor}`} />
            <span>{statusLabel}</span>
          </div>
        )}
      </header>

      <main className="grid min-h-0 flex-1 grid-cols-1 gap-4 xl:grid-cols-[minmax(0,1.65fr)_minmax(20rem,1fr)]">
        <SelectedFilesPanel
          targets={targets}
          onRemove={(id) => {
            setTargets((current) => current.filter((target) => target.id !== id))
            onTargetRemoved?.(id)
          }}
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