import { useState } from 'react'
import type { ReactElement } from 'react'
import { FileSystemTree } from './components/filetree/FileSystemTree'
import AppShell from './components/layout/Appshell'
import type { NavItem } from './components/layout/ActivityBar'
import { FileEraseTab } from './components/modules/file-erase/FileEraseTab'

function App() {
  const [activeTab, setActiveTab] = useState<NavItem>('file-erase')
  const [explorerOpen, setExplorerOpen] = useState(true)

  const renderModule = (): ReactElement => {
    if (activeTab === 'file-erase') {
      return (
        <div className="flex h-full min-h-0 gap-4">
          {explorerOpen && (
            <aside className="hidden w-72 shrink-0 lg:block">
              <FileSystemTree panel />
            </aside>
          )}
          <section className="min-w-0 flex-1">
            <FileEraseTab
              explorerOpen={explorerOpen}
              onToggleExplorer={() => setExplorerOpen((open) => !open)}
            />
          </section>
        </div>
      )
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
