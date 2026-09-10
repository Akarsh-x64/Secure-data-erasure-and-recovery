import { File, Folder, Trash2, Plus, FolderPlus } from 'lucide-react'
import type { ReactElement } from 'react'

export type FileSystemType = 'NTFS' | 'ext4' | 'FAT32'

export interface EraseTarget {
  id: string
  path: string
  kind: 'file' | 'folder'
  clusterSize: string
  fileSystem: FileSystemType
  size: string
}

interface SelectedFilesPanelProps {
  targets: EraseTarget[]
  onRemove: (id: string) => void
  onAddFiles?: () => void
  onAddFolder?: () => void
}

export function SelectedFilesPanel({ targets, onRemove, onAddFiles, onAddFolder }: SelectedFilesPanelProps): ReactElement {
  return (
    <section className="flex min-h-0 flex-1 flex-col rounded-lg border border-ui-outline bg-background-sidebar">
      <div className="flex items-center justify-between border-b border-ui-outline px-4 py-3">
        <div>
          <h2 className="text-sm font-medium text-text-pure">Target queue</h2>
          <p className="mt-0.5 text-xs text-text-muted">
            {targets.length} {targets.length === 1 ? 'item' : 'items'} selected
          </p>
        </div>
        <div className="flex items-center gap-2">
          {onAddFiles && (
            <button
              type="button"
              onClick={onAddFiles}
              className="flex items-center gap-1 rounded-md border border-ui-outline bg-background-main px-2.5 py-1 text-xs text-text-pure transition-colors hover:bg-ui-selection"
            >
              <Plus className="h-3.5 w-3.5" />
              Add files
            </button>
          )}
          {onAddFolder && (
            <button
              type="button"
              onClick={onAddFolder}
              className="flex items-center gap-1 rounded-md border border-ui-outline bg-background-main px-2.5 py-1 text-xs text-text-pure transition-colors hover:bg-ui-selection"
            >
              <FolderPlus className="h-3.5 w-3.5" />
              Add folder
            </button>
          )}
          <span className="flex items-center gap-1.5 rounded-full border border-ui-outline bg-background-main px-2.5 py-1 text-xs text-status-warning">
            <span className="h-1.5 w-1.5 rounded-full bg-status-warning" />
            Awaiting erase
          </span>
        </div>
      </div>

      <div className="min-h-0 flex-1 overflow-auto">
        <table className="w-full border-collapse text-left text-sm">
          <thead className="sticky top-0 z-10 bg-background-sidebar text-xs text-text-muted">
            <tr>
              <th className="border-b border-ui-outline px-4 py-2 font-normal">Path</th>
              <th className="border-b border-ui-outline px-2 py-2 font-normal">Size</th>
              <th className="border-b border-ui-outline px-2 py-2 font-normal">Cluster</th>
              <th className="border-b border-ui-outline px-2 py-2 font-normal">File system</th>
              <th className="border-b border-ui-outline px-2 py-2" aria-label="Actions" />
            </tr>
          </thead>
          <tbody>
            {targets.map((target) => (
              <tr
                key={target.id}
                className="group border-b border-ui-outline/60 transition-colors hover:bg-ui-selection/40"
              >
                <td className="max-w-[18rem] px-4 py-3">
                  <div className="flex min-w-0 items-center gap-2.5">
                    {target.kind === 'folder' ? (
                      <Folder className="h-4 w-4 shrink-0 text-status-warning" />
                    ) : (
                      <File className="h-4 w-4 shrink-0 text-text-muted" />
                    )}
                    <div className="min-w-0">
                      <span className="block truncate text-text-pure" title={target.path}>
                        {target.path}
                      </span>
                      <span className="text-xs capitalize text-text-muted">{target.kind}</span>
                    </div>
                  </div>
                </td>
                <td className="whitespace-nowrap px-2 py-3 text-text-muted">{target.size}</td>
                <td className="whitespace-nowrap px-2 py-3 text-text-muted">
                  {target.clusterSize}
                </td>
                <td className="px-2 py-3 text-status-valid">{target.fileSystem}</td>
                <td className="px-2 py-3 text-right">
                  <button
                    type="button"
                    onClick={() => onRemove(target.id)}
                    title={`Remove ${target.path}`}
                    className="rounded-md p-1.5 text-text-muted opacity-0 transition-colors hover:bg-status-error/15 hover:text-status-error group-hover:opacity-100"
                  >
                    <Trash2 className="h-4 w-4" />
                  </button>
                </td>
              </tr>
            ))}
            {targets.length === 0 && (
              <tr>
                <td colSpan={5} className="px-4 py-14 text-center text-sm text-text-muted">
                  Queue is empty. Add a file or folder above to get started.
                </td>
              </tr>
            )}
          </tbody>
        </table>
      </div>
    </section>
  )
}