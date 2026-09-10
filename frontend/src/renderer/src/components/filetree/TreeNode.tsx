import React, { useState } from 'react';
import { ChevronRight, ChevronDown, Scissors, ScanSearch } from 'lucide-react';
import { FileIcon } from './FileIcon';

export interface ForensicNode {
  id: string;
  name: string;
  isDirectory?: boolean;
  isDrive?: boolean;
  isCorrupted?: boolean;
  isDeleted?: boolean;
  confidence?: number;
  statusLabel?: string;
  size?: string;
  flaggedForErasure?: boolean;
  flaggedForCarving?: boolean;
  children?: ForensicNode[];
}

interface TreeNodeProps {
  node: ForensicNode;
  depth?: number;
  selectedId: string | null;
  onSelect: (node: ForensicNode) => void;
  actionMode?: 'erase' | 'recovery';
  onAction?: (node: ForensicNode) => void;
  markedNodeIds?: ReadonlySet<string>;
}

export const TreeNode: React.FC<TreeNodeProps> = ({
  node,
  depth = 0,
  selectedId,
  onSelect,
  actionMode,
  onAction,
  markedNodeIds,
}) => {
  const [isOpen, setIsOpen] = useState<boolean>(depth === 0);
  const [showMenu, setShowMenu] = useState<boolean>(false);

  const isSelected = selectedId === node.id;
  const marked = markedNodeIds?.has(node.id) ?? false;
  const hasChildren = Boolean(node.children && node.children.length > 0);
  const showAction = actionMode === 'erase' || (actionMode === 'recovery' && !node.isDirectory && node.isCorrupted);

  const handleToggleExpand = (e: React.MouseEvent): void => {
    e.stopPropagation();
    if (hasChildren || node.isDirectory) {
      setIsOpen(!isOpen);
    }
  };

  const handleSelectNode = (): void => {
    onSelect(node);
  };

  const handleContextMenu = (e: React.MouseEvent): void => {
    e.preventDefault();
    setShowMenu(!showMenu);
  };

  return (
    <div className="select-none font-sans text-sm">
      <div
        onClick={handleSelectNode}
        onContextMenu={handleContextMenu}
        style={{ paddingLeft: `${depth * 14 + 8}px` }}
        className={`group relative flex h-8 cursor-pointer items-center border-l-2 pr-2 transition-colors duration-100 ${
          isSelected
            ? 'border-l-text-pure bg-ui-selection text-text-pure'
            : 'border-l-transparent text-text-muted hover:bg-ui-selection/60 hover:text-text-pure'
        }`}
      >
        {/* Expand / collapse chevron */}
        <button
          onClick={handleToggleExpand}
          className={`mr-1 flex h-4 w-4 items-center justify-center rounded transition-opacity hover:bg-ui-outline ${
            hasChildren || node.isDirectory ? 'opacity-100' : 'pointer-events-none opacity-0'
          }`}
        >
          {isOpen ? (
            <ChevronDown className="h-3 w-3 text-text-muted" />
          ) : (
            <ChevronRight className="h-3 w-3 text-text-muted" />
          )}
        </button>

        {/* File/folder icon */}
        <FileIcon
          name={node.name}
          isDirectory={node.isDirectory}
          isOpen={isOpen}
          isCorrupted={node.isCorrupted}
          isDeleted={node.isDeleted}
          isDrive={node.isDrive}
          className="mr-2 h-4 w-4 shrink-0"
        />

        {/* Status label (e.g. Damaged) */}
        {node.statusLabel && (
          <span className="mr-1.5 rounded bg-status-error/15 px-1.5 py-0.5 text-xs font-medium text-status-error">
            {node.statusLabel}
          </span>
        )}

        {/* File/folder name */}
        <span className="truncate text-[13px]">{node.name}</span>

        {marked && (
          <span className="ml-2 shrink-0 rounded-full border border-status-valid/40 bg-status-valid/10 px-1.5 py-0.5 text-[10px] font-medium text-status-valid">
            {actionMode === 'recovery' ? 'Carve' : 'Erase'}
          </span>
        )}

        {/* Deleted & confidence tag */}
        {node.isDeleted && (
          <span className="ml-1.5 truncate text-xs text-status-warning/90">
            Deleted{node.confidence ? `, ${node.confidence}% confidence` : ''}
          </span>
        )}

        {/* Active flag badges */}
        <div className="ml-auto flex shrink-0 items-center gap-1">
          {/* Quick action buttons on hover */}
          {showAction && onAction && (
            <div className="ml-1 hidden items-center gap-1 group-hover:flex">
              <button
                title={actionMode === 'erase' ? 'Move to erase target queue' : 'Move to recovery queue'}
                onClick={(e) => {
                  e.stopPropagation();
                  onAction(node);
                }}
                className="rounded p-1 text-text-muted hover:bg-ui-outline hover:text-status-warning"
              >
                {actionMode === 'erase' ? (
                  <Scissors className="h-3.5 w-3.5" />
                ) : (
                  <ScanSearch className="h-3.5 w-3.5" />
                )}
              </button>
            </div>
          )}
        </div>

        {/* Context menu popup */}
        {showMenu && showAction && onAction && (
          <div
            className="absolute right-2 top-8 z-50 w-48 rounded-md border border-ui-outline bg-background-sidebar py-1 text-sm shadow-xl"
            onMouseLeave={() => setShowMenu(false)}
          >
            <button
              onClick={(e) => {
                e.stopPropagation();
                onAction(node);
                setShowMenu(false);
              }}
              className="flex w-full items-center px-3 py-2 text-left text-text-pure hover:bg-ui-selection"
            >
              {actionMode === 'erase' ? (
                <Scissors className="mr-2 h-4 w-4 text-status-warning" />
              ) : (
                <ScanSearch className="mr-2 h-4 w-4 text-status-warning" />
              )}
              {actionMode === 'erase' ? 'Move to erase target queue' : 'Move to recovery queue'}
            </button>
          </div>
        )}
      </div>

      {/* Children nodes */}
      {isOpen && hasChildren && (
        <div>
          {node.children!.map((child) => (
            <TreeNode
              key={child.id}
              node={child}
              depth={depth + 1}
              selectedId={selectedId}
              onSelect={onSelect}
              actionMode={actionMode}
              onAction={onAction}
              markedNodeIds={markedNodeIds}
            />
          ))}
        </div>
      )}
    </div>
  );
};