import React, { useState, useMemo } from 'react';
import {
  FolderTree,
  Search,
  RefreshCw,
  Flame,
  Scissors,
  SlidersHorizontal,
  PanelLeftClose,
  PanelLeftOpen
} from 'lucide-react';
import { TreeNode, ForensicNode } from './TreeNode';
import { FilePreview } from '../shared/FilePreview';

const INITIAL_TREE_DATA: ForensicNode[] = [
  {
    id: 'root-sdb1',
    name: '/dev/sdb1 (Unallocated Space)',
    isDirectory: true,
    isDrive: true,
    size: '128 GB',
    children: [
      {
        id: 'fat32-sys',
        name: 'FAT32_SYS',
        isDirectory: true,
        isCorrupted: true,
        statusLabel: 'Damaged',
        children: [
          {
            id: 'mft-corrupted',
            name: '$MFT_Corrupted',
            isDirectory: false,
            isCorrupted: true,
            size: '4 KB',
          },
          {
            id: 'img-0921',
            name: 'IMG_0921.raw',
            isDirectory: false,
            isDeleted: true,
            confidence: 94,
            size: '14.2 MB',
            flaggedForCarving: true,
          },
        ],
      },
      {
        id: 'var-log-audit',
        name: '/var/log/audit',
        isDirectory: true,
        children: [
          {
            id: 'audit-log',
            name: 'system_audit.log',
            isDirectory: false,
            size: '512 KB',
          },
          {
            id: 'sec-dump',
            name: 'sec_event.dd',
            isDirectory: false,
            flaggedForErasure: true,
            size: '2.1 GB',
          },
        ],
      },
    ],
  },
];

// Recursive helper to toggle flag state
const toggleNodeFlagInTree = (
  nodes: ForensicNode[],
  targetId: string,
  type: 'erasure' | 'carving'
): ForensicNode[] => {
  return nodes.map((node) => {
    if (node.id === targetId) {
      return {
        ...node,
        flaggedForErasure:
          type === 'erasure' ? !node.flaggedForErasure : node.flaggedForErasure,
        flaggedForCarving:
          type === 'carving' ? !node.flaggedForCarving : node.flaggedForCarving,
      };
    }
    if (node.children) {
      return {
        ...node,
        children: toggleNodeFlagInTree(node.children, targetId, type),
      };
    }
    return node;
  });
};

// Recursive helper to filter nodes by query or active flags
const filterTreeNodes = (
  nodes: ForensicNode[],
  query: string,
  flagFilter: 'all' | 'carve' | 'erase'
): ForensicNode[] => {
  return nodes.reduce<ForensicNode[]>((acc, node) => {
    const matchesQuery = node.name.toLowerCase().includes(query.toLowerCase());
    const matchesFlag =
      flagFilter === 'all' ||
      (flagFilter === 'carve' && node.flaggedForCarving) ||
      (flagFilter === 'erase' && node.flaggedForErasure);

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
  panel?: boolean;
  explorerOpen?: boolean;
  onToggleExplorer?: () => void;
}

export const FileSystemTree: React.FC<FileSystemTreeProps> = ({
  panel = false,
  explorerOpen = true,
  onToggleExplorer,
}) => {
  const [treeData, setTreeData] = useState<ForensicNode[]>(INITIAL_TREE_DATA);
  const [selectedNode, setSelectedNode] = useState<ForensicNode | null>(
    INITIAL_TREE_DATA[0].children![0].children![1] // Default selection: IMG_0921.raw
  );
  const [searchQuery, setSearchQuery] = useState('');
  const [flagFilter, setFlagFilter] = useState<'all' | 'carve' | 'erase'>('all');

  const handleToggleFlag = (id: string, type: 'erasure' | 'carving') => {
    setTreeData((prev) => toggleNodeFlagInTree(prev, id, type));
  };

  const filteredData = useMemo(
    () => filterTreeNodes(treeData, searchQuery, flagFilter),
    [treeData, searchQuery, flagFilter]
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
    <div className={`grid grid-cols-1 ${panel ? '' : 'lg:grid-cols-12'} h-full w-full overflow-hidden rounded-lg border border-[var(--ui-outline)] bg-[var(--bg-main)] font-sans text-[var(--text-pure)] select-none`}>
      {/* Sidebar / Explorer Tree Container (5 columns on LG) */}
      <div className="lg:col-span-5 flex flex-col h-full bg-[var(--bg-sidebar)] border-r border-[var(--ui-outline)]">
        {/* Explorer Header */}
        <div className="flex items-center justify-between px-3 py-2 border-b border-[var(--ui-outline)] bg-[var(--bg-icon)]">
          <div className="flex items-center space-x-2">
            <FolderTree className="w-4 h-4 text-[var(--text-muted)]" />
            <span className="text-xs font-bold tracking-wider text-[var(--text-pure)]">
              Explorer
            </span>
          </div>

          <div className="flex items-center space-x-2 text-[var(--text-muted)]">
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
            <button
              onClick={() => setTreeData(INITIAL_TREE_DATA)}
              title="Reset Tree View"
              className="p-1 hover:text-[var(--text-pure)] hover:bg-[var(--ui-selection)] transition-colors"
            >
              <RefreshCw className="w-3.5 h-3.5" />
            </button>
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
            <div className="flex items-center space-x-1">
              <SlidersHorizontal className="w-3 h-3 text-[var(--text-muted)] mr-1" />
              <button
                onClick={() => setFlagFilter('all')}
                className={`px-1.5 py-0.5 ${
                  flagFilter === 'all'
                    ? 'rounded-md bg-[var(--ui-selection)] text-[var(--text-pure)]'
                    : 'rounded-md text-[var(--text-muted)] hover:text-[var(--text-pure)]'
                }`}
              >
                All
              </button>
              <button
                onClick={() => setFlagFilter('carve')}
                className={`px-1.5 py-0.5 flex items-center space-x-1 ${
                  flagFilter === 'carve'
                    ? 'rounded-md border border-[var(--status-warning)]/40 bg-[var(--status-warning)]/20 text-[var(--status-warning)]'
                    : 'rounded-md text-[var(--text-muted)] hover:text-[var(--status-warning)]'
                }`}
              >
                <Scissors className="w-2.5 h-2.5" />
                <span>Carve</span>
              </button>
              <button
                onClick={() => setFlagFilter('erase')}
                className={`px-1.5 py-0.5 flex items-center space-x-1 ${
                  flagFilter === 'erase'
                    ? 'rounded-md border border-[var(--status-error)]/40 bg-[var(--status-error)]/20 text-[var(--status-error)]'
                    : 'rounded-md text-[var(--text-muted)] hover:text-[var(--status-error)]'
                }`}
              >
                <Flame className="w-2.5 h-2.5" />
                <span>Erase</span>
              </button>
            </div>
          </div>
        </div>

        {/* Tree Nodes List */}
        <div className="flex-1 overflow-y-auto py-1">
          {filteredData.length > 0 ? (
            filteredData.map((node) => (
              <TreeNode
                key={node.id}
                node={node}
                selectedId={selectedNode?.id || null}
                onSelect={(selected) => setSelectedNode(selected)}
                onToggleFlag={handleToggleFlag}
              />
            ))
          ) : (
            <div className="p-4 text-center text-xs text-[var(--text-muted)]">
              No matching forensic nodes found.
            </div>
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
            <span className="flex items-center text-[var(--status-warning)]">
              <Scissors className="w-3 h-3 mr-0.5" /> Carve
            </span>
            <span className="flex items-center text-[var(--status-error)]">
              <Flame className="w-3 h-3 mr-0.5" /> Erase
            </span>
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