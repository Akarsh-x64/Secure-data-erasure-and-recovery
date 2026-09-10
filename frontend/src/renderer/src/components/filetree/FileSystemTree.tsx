import React, { useEffect, useMemo, useRef, useState } from 'react';
import {
  FolderTree,
  FolderOpen,
  FolderSearch,
  Search,
  PanelLeftClose,
  PanelLeftOpen,
  X
} from 'lucide-react';
import { TreeNode, ForensicNode } from './TreeNode';
import { FilePreview } from '../shared/FilePreview';

// Recursive helper to filter nodes by query or active flags
const filterTreeNodes = (
  nodes: ForensicNode[],
  query: string,
  flagFilter: 'all'
): ForensicNode[] => {
  return nodes.reduce<ForensicNode[]>((acc, node) => {
    const matchesQuery = node.name.toLowerCase().includes(query.toLowerCase());
    const matchesFlag = flagFilter === 'all';

    const filteredChildren = node.children
      ? filterTreeNodes(node.children, query, flagFilter)
      : [];

    const isMatch = (matchesQuery && matchesFlag) || filteredChildren.length > 0;

    if (isMatch) {
      acc.push({
        ...node,
        children: filteredChildren.length > 0 ? filteredChildren : node.children,
      });
    }

    return acc;
  }, []);
};

interface FileSystemTreeProps {
  nodes?: ForensicNode[];
  panel?: boolean;
  explorerOpen?: boolean;
  onToggleExplorer?: () => void;
  onSelectNode?: (node: ForensicNode) => void;
  actionMode?: 'erase' | 'recovery';
  scanned?: boolean;
  onAction?: (node: ForensicNode) => void;
  onDirectorySelected?: (nodes: ForensicNode[]) => void;
  onDirectoryCleared?: () => void;
  unmarkNode?: { id: string; request: number };
}

const formatFileSize = (bytes: number): string => {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  if (bytes < 1024 * 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
  return `${(bytes / (1024 * 1024 * 1024)).toFixed(1)} GB`;
};

const createDirectoryTree = (files: FileList): ForensicNode[] => {
  const firstFile = files[0] as File & { webkitRelativePath?: string };
  const relativePath = firstFile.webkitRelativePath || firstFile.name;
  const rootName = relativePath.split('/')[0] || 'Selected directory';
  const root: ForensicNode = {
    id: `directory-${rootName}`,
    name: rootName,
    isDirectory: true,
    children: [],
  };

  Array.from(files).forEach((file) => {
    const path = (file as File & { webkitRelativePath?: string }).webkitRelativePath || file.name;
    const parts = path.split('/').filter(Boolean);
    const pathParts = parts[0] === rootName ? parts.slice(1) : parts;
    let current = root;

    pathParts.forEach((part, index) => {
      const isFile = index === pathParts.length - 1;
      const existing = current.children?.find((child) => child.name === part);
      if (existing) {
        current = existing;
        return;
      }

      const next: ForensicNode = {
        id: `${current.id}/${part}`,
        name: part,
        isDirectory: !isFile,
        ...(isFile ? { size: formatFileSize(file.size) } : { children: [] }),
      };
      current.children?.push(next);
      current = next;
    });
  });

  return [root];
};

export const FileSystemTree: React.FC<FileSystemTreeProps> = ({
  nodes = [],
  panel = false,
  explorerOpen = true,
  onToggleExplorer,
  onSelectNode,
  actionMode,
  scanned = false,
  onAction,
  onDirectorySelected,
  onDirectoryCleared,
  unmarkNode,
}) => {
  const directoryInputRef = useRef<HTMLInputElement>(null);
  const [selectedNode, setSelectedNode] = useState<ForensicNode | null>(null);
  const [markedNodeIds, setMarkedNodeIds] = useState<Set<string>>(new Set());
  const [searchQuery, setSearchQuery] = useState('');
  const [flagFilter] = useState<'all'>('all');

  useEffect(() => {
    setSelectedNode(null);
    setMarkedNodeIds(new Set());
  }, [nodes]);

  useEffect(() => {
    if (!unmarkNode) return;
    setMarkedNodeIds((current) => {
      const next = new Set(current);
      next.delete(unmarkNode.id);
      return next;
    });
  }, [unmarkNode]);

  const handleAction = (node: ForensicNode): void => {
    setMarkedNodeIds((current) => {
      const next = new Set(current);
      next.add(node.id);
      return next;
    });
    onAction?.(node);
  };

  const filteredData = useMemo(
    () => filterTreeNodes(nodes, searchQuery, flagFilter),
    [nodes, searchQuery, flagFilter]
  );

  if (panel && !explorerOpen && onToggleExplorer) {
    return (
      <div className="flex h-full w-full items-start justify-center rounded-lg border border-[var(--ui-outline)] bg-[var(--bg-sidebar)] pt-2">
        <button
          type="button"
          onClick={onToggleExplorer}
          title="Show explorer"
          aria-label="Show explorer"
          className="p-1 text-[var(--text-muted)] transition-colors hover:bg-[var(--ui-selection)] hover:text-[var(--text-pure)]"
        >
          <PanelLeftOpen className="h-3.5 w-3.5" />
        </button>
      </div>
    );
  }

  return (
    <div className={`grid grid-cols-1 ${panel ? '' : 'lg:grid-cols-12'} h-full min-h-0 w-full overflow-hidden rounded-lg border border-[var(--ui-outline)] bg-[var(--bg-main)] font-sans text-[var(--text-pure)] select-none`}>
      {/* Sidebar / Explorer Tree Container (5 columns on LG) */}
      <div className="lg:col-span-5 flex h-full min-h-0 flex-col bg-[var(--bg-sidebar)] border-r border-[var(--ui-outline)]">
        {/* Explorer Header */}
        <div className="flex items-center justify-between px-3 py-2 border-b border-[var(--ui-outline)] bg-[var(--bg-icon)]">
          <div className="flex items-center space-x-2">
            <FolderTree className="w-4 h-4 text-[var(--text-muted)]" />
            <span className="text-xs font-bold tracking-wider text-[var(--text-pure)]">
              Explorer
            </span>
          </div>

          <div className="flex items-center space-x-2 text-[var(--text-muted)]">
            {panel && actionMode === 'erase' && nodes.length > 0 && onDirectorySelected && (
              <>
                <button
                  type="button"
                  onClick={() => directoryInputRef.current?.click()}
                  title="Change directory"
                  aria-label="Change directory"
                  className="p-1 transition-colors hover:bg-[var(--ui-selection)] hover:text-[var(--text-pure)]"
                >
                  <FolderOpen className="h-3.5 w-3.5" />
                </button>
                {onDirectoryCleared && (
                  <button
                    type="button"
                    onClick={onDirectoryCleared}
                    title="Remove directory"
                    aria-label="Remove directory"
                    className="p-1 transition-colors hover:bg-[var(--ui-selection)] hover:text-[var(--status-error)]"
                  >
                    <X className="h-3.5 w-3.5" />
                  </button>
                )}
              </>
            )}
            {panel && onToggleExplorer && (
              <button
                type="button"
                onClick={onToggleExplorer}
                title={explorerOpen ? 'Hide explorer' : 'Show explorer'}
                aria-label={explorerOpen ? 'Hide explorer' : 'Show explorer'}
                className="p-1 hover:bg-[var(--ui-selection)] hover:text-[var(--text-pure)] transition-colors"
              >
                {explorerOpen ? <PanelLeftClose className="w-3.5 h-3.5" /> : <PanelLeftOpen className="w-3.5 h-3.5" />}
              </button>
            )}
          </div>
        </div>

        {/* Search & Filter Bar */}
        <div className="p-2 border-b border-[var(--ui-outline)] bg-[var(--bg-sidebar)] space-y-2">
          <div className="relative flex items-center">
            <Search className="w-3.5 h-3.5 absolute left-2 text-[var(--text-muted)]" />
            <input
              type="text"
              value={searchQuery}
              onChange={(e) => setSearchQuery(e.target.value)}
              placeholder="Filter nodes or paths..."
              className="w-full rounded-md border border-[var(--ui-outline)] bg-[var(--bg-main)] py-1 pl-7 pr-2 text-xs text-[var(--text-pure)] placeholder:text-[var(--text-muted)] focus:border-[var(--status-valid)] focus:outline-none"
            />
          </div>

          {/* Quick Flag Filters */}
          <div className="flex items-center justify-between text-[10px]">
            <span className="text-[var(--text-muted)]">
              {actionMode === 'recovery' && scanned ? 'Scan results' : actionMode === 'erase' ? 'Erase targets' : 'File explorer'}
            </span>
          </div>
        </div>

        {/* Tree Nodes List */}
        <div className="scrollbar-hidden flex-1 overflow-y-auto py-1">
          <input
            ref={directoryInputRef}
            type="file"
            className="hidden"
            // @ts-expect-error non-standard directory selection attribute
            webkitdirectory=""
            onChange={(event) => {
              if (event.target.files?.length) {
                onDirectorySelected?.(createDirectoryTree(event.target.files));
              }
              event.target.value = '';
            }}
          />
          {filteredData.length > 0 ? (
            filteredData.map((node) => (
              <TreeNode
                key={node.id}
                node={node}
                selectedId={selectedNode?.id || null}
                onSelect={(selected): void => {
                  setSelectedNode(selected);
                  onSelectNode?.(selected);
                }}
                actionMode={actionMode}
                onAction={handleAction}
                markedNodeIds={markedNodeIds}
              />
            ))
          ) : (
            nodes.length === 0 && actionMode === 'erase' && onDirectorySelected ? (
              <div className="mx-2 my-3 flex flex-col items-center justify-center gap-1.5 rounded-md border border-dashed border-[var(--ui-outline)] bg-[var(--bg-main)] px-3 py-5 text-center">
                <FolderSearch className="h-6 w-6 text-[var(--text-muted)]" />
                <p className="text-sm text-[var(--text-pure)]">
                  <button
                    type="button"
                    onClick={() => directoryInputRef.current?.click()}
                    className="text-[var(--status-valid)] underline-offset-2 hover:underline"
                  >
                    Choose a directory
                  </button>{' '}
                  to browse its files
                </p>
                <p className="text-[11px] text-[var(--text-muted)]">
                  Select a folder to load its file tree
                </p>
              </div>
            ) : (
              <div className="p-4 text-center text-xs text-[var(--text-muted)]">
                {nodes.length === 0 ? 'No directory selected.' : 'No matching files found.'}
              </div>
            )
          )}
        </div>

        {/* Status Bar Summary Footer */}
        <div className="p-2 border-t border-[var(--ui-outline)] bg-[var(--bg-icon)] text-[10px] flex items-center justify-between text-[var(--text-muted)]">
          <div className="flex items-center space-x-3">
            <span>
              Target Selected:{' '}
              <span className="text-[var(--text-pure)]">
                {selectedNode ? selectedNode.name : 'None'}
              </span>
            </span>
          </div>
          <div className="flex items-center space-x-2">
            {actionMode === 'recovery' && scanned && <span className="text-[var(--status-warning)]">Recovery action available on damaged files</span>}
            {actionMode === 'erase' && <span className="text-[var(--status-warning)]">Erase action available</span>}
          </div>
        </div>
      </div>

      {/* Main Inspector View (7 columns on LG) */}
      {!panel && (
        <div className="lg:col-span-7 h-full flex flex-col bg-[var(--bg-main)]">
          <FilePreview node={selectedNode} />
        </div>
      )}
    </div>
  );
};