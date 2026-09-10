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
  onEraseCompleted?: (erasedTargets: EraseTarget[]) => void
  queueResetToken?: number
}

export function FileEraseTab({
  treeTarget,
  onTargetRemoved,
  onEraseCompleted,
  queueResetToken = 0
}: FileEraseTabProps): ReactElement {
  const [targets, setTargets] = useState<EraseTarget[]>([])
  const [config, setConfig] = useState<EraseConfig>(INITIAL_CONFIG)
  const [dialogOpen, setDialogOpen] = useState(false)
  const [executionState, setExecutionState] = useState<'ready' | 'dispatching' | 'dispatched'>(
    'ready'
  )

  useEffect(() => {
    if (!treeTarget || targets.some((target) => target.id === treeTarget.id || target.path === (treeTarget.path || treeTarget.name))) return
    // The tree action is an external event delivered through the prop.
    // eslint-disable-next-line react-hooks/set-state-in-effect
    const targetPath = treeTarget.path || treeTarget.name
    setTargets((current) => [
      ...current,
      {
        id: treeTarget.id,
        path: targetPath,
        kind: treeTarget.isDirectory ? 'folder' : 'file',
        clusterSize: '4 KB',
        fileSystem: guessFileSystem(targetPath),
        size: treeTarget.size ?? 'Pending scan'
      }
    ])
  }, [targets, treeTarget])

  useEffect(() => {
    setTargets([])
    setExecutionState('ready')
  }, [queueResetToken])

  const handleAddFiles = async (): Promise<void> => {
    if (window.api?.selectFiles) {
      const selected = await window.api.selectFiles()
      if (selected && selected.length > 0) {
        setTargets((current) => [
          ...current,
          ...selected
            .filter((item) => !current.some((t) => t.path === item.path))
            .map((item) => ({
              id: item.id,
              path: item.path,
              kind: item.isDirectory ? ('folder' as const) : ('file' as const),
              clusterSize: '4 KB',
              fileSystem: guessFileSystem(item.path),
              size: item.size
            }))
        ])
      }
    }
  }

  const handleAddFolder = async (): Promise<void> => {
    if (window.api?.selectDirectory) {
      const res = await window.api.selectDirectory()
      if (res && res.path) {
        setTargets((current) => {
          if (current.some((t) => t.path === res.path)) return current
          return [
            ...current,
            {
              id: res.path,
              path: res.path,
              kind: 'folder' as const,
              clusterSize: '4 KB',
              fileSystem: guessFileSystem(res.path),
              size: 'Directory'
            }
          ]
        })
      }
    }
  }

  const executeErase = async (): Promise<void> => {
    setDialogOpen(false)
    setExecutionState('dispatching')
    if (window.api?.eraseFiles) {
      const res = await window.api.eraseFiles({ targets, config })
      if (res?.accepted) {
        setExecutionState('dispatched')
        const finishedTargets = [...targets]
        setTimeout(() => {
          setTargets([])
          onEraseCompleted?.(finishedTargets)
          setExecutionState('ready')
        }, 800)
        return
      }
    }
    setExecutionState('ready')
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
          onAddFiles={handleAddFiles}
          onAddFolder={handleAddFolder}
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