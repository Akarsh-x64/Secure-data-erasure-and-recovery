import { useState } from 'react'
import type { ReactElement } from 'react'
import { FileSystemTree } from './components/filetree/FileSystemTree'
import AppShell from './components/layout/Appshell'
import type { NavItem } from './components/layout/ActivityBar'
import { DriveEraseTab } from './components/modules/drive-erase/DriveEraseTab'
import { FileEraseTab } from './components/modules/file-erase/FileEraseTab'
import { RecoveryTab } from './components/modules/recovery/RecoveryTab'

function App() {
  const [activeTab, setActiveTab] = useState<NavItem>('file-erase')
  const [explorerOpen, setExplorerOpen] = useState(true)

  const renderModule = (): ReactElement => {
    if (activeTab === 'file-erase') {
      return (
        <div className="flex h-full min-h-0 gap-4">
          <aside className={`hidden h-full min-h-0 shrink-0 lg:block ${explorerOpen ? 'w-72' : 'w-10'}`}>
            <FileSystemTree
              panel
              explorerOpen={explorerOpen}
              onToggleExplorer={() => setExplorerOpen((open) => !open)}
            />
          </aside>
          <section className="scrollbar-hidden h-full min-h-0 min-w-0 flex-1 overflow-y-auto">
            <FileEraseTab />
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
