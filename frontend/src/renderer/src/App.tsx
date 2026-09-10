import { useState } from 'react'
import type { ReactElement } from 'react'
import { FileSystemTree } from './components/filetree/FileSystemTree'
import AppShell from './components/layout/Appshell'
import type { NavItem } from './components/layout/ActivityBar'
import { DriveEraseTab } from './components/modules/drive-erase/DriveEraseTab'
import { FileEraseTab } from './components/modules/file-erase/FileEraseTab'
import { RecoveryTab } from './components/modules/recovery/RecoveryTab'
import { AuditLogsTab } from './components/modules/audit-logs/AuditLogsTabs'
import type { ForensicNode } from './components/filetree/TreeNode'

import type { EraseTarget } from './components/modules/file-erase/SelectedFilesPanel'

function App(): ReactElement {
  const [activeTab, setActiveTab] = useState<NavItem>('file-erase')
  const [explorerOpen, setExplorerOpen] = useState(true)
  const [eraseTreeTarget, setEraseTreeTarget] = useState<ForensicNode | null>(null)
  const [eraseTreeNodes, setEraseTreeNodes] = useState<ForensicNode[]>([])
  const [eraseUnmarkRequest, setEraseUnmarkRequest] = useState<{ id: string; request: number } | undefined>()
  const [eraseQueueResetToken, setEraseQueueResetToken] = useState(0)

  const handleEraseDirectorySelected = (nodes: ForensicNode[]): void => {
    setEraseTreeNodes(nodes)
    setEraseTreeTarget(null)
    setEraseQueueResetToken((token) => token + 1)
  }

  const clearEraseDirectory = (): void => {
    setEraseTreeNodes([])
    setEraseTreeTarget(null)
    setEraseQueueResetToken((token) => token + 1)
  }

  const removeEraseTarget = (id: string): void => {
    setEraseTreeTarget((current) => (current?.id === id ? null : current))
    setEraseUnmarkRequest({ id, request: Date.now() })
  }

  const handleEraseCompleted = (erasedTargets: EraseTarget[]): void => {
    const idsAndPaths = new Set<string>()
    erasedTargets.forEach((t) => {
      idsAndPaths.add(t.id)
      idsAndPaths.add(t.path)
      idsAndPaths.add(t.path.replace(/\\/g, '/'))
      idsAndPaths.add(t.path.replace(/\//g, '\\'))
    })

    const filterNodesRecursively = (nodes: ForensicNode[]): ForensicNode[] => {
      return nodes
        .filter((node) => {
          const pFwd = (node.path || '').replace(/\\/g, '/')
          const pBack = (node.path || '').replace(/\//g, '\\')
          return !(
            idsAndPaths.has(node.id) ||
            idsAndPaths.has(node.name) ||
            idsAndPaths.has(node.path || '') ||
            idsAndPaths.has(pFwd) ||
            idsAndPaths.has(pBack)
          )
        })
        .map((node) => ({
          ...node,
          children: node.children ? filterNodesRecursively(node.children) : undefined
        }))
    }

    setEraseTreeNodes((prev) => filterNodesRecursively(prev))
    setEraseTreeTarget(null)
  }

  const renderModule = (): ReactElement => {
    if (activeTab === 'file-erase') {
      return (
        <div className="flex h-full min-h-0 gap-4">
          <aside className={`hidden h-full min-h-0 shrink-0 lg:block ${explorerOpen ? 'w-72' : 'w-10'}`}>
            <FileSystemTree
              panel
              explorerOpen={explorerOpen}
              onToggleExplorer={() => setExplorerOpen((open) => !open)}
              nodes={eraseTreeNodes}
              actionMode="erase"
              onAction={setEraseTreeTarget}
              onDirectorySelected={handleEraseDirectorySelected}
              onDirectoryCleared={clearEraseDirectory}
              unmarkNode={eraseUnmarkRequest}
            />
          </aside>
          <section className="scrollbar-hidden h-full min-h-0 min-w-0 flex-1 overflow-y-auto">
            <FileEraseTab
              treeTarget={eraseTreeTarget}
              onTargetRemoved={removeEraseTarget}
              onEraseCompleted={handleEraseCompleted}
              queueResetToken={eraseQueueResetToken}
            />
          </section>
        </div>
      )
    }

    if (activeTab === 'drive-erase') {
      return <DriveEraseTab />
    }

    if (activeTab === 'recovery') {
      return <RecoveryTab />
    }

    if(activeTab==='audit'){
      return <AuditLogsTab/>
    }

    return (
      <div className="flex h-full items-center justify-center border border-ui-outline bg-background-sidebar text-sm text-text-muted">
        This module is not available yet.
      </div>
    )
  }

  return (
    <AppShell activeTab={activeTab} onTabChange={setActiveTab}>
      {renderModule()}
    </AppShell>
  )
}

export default App
