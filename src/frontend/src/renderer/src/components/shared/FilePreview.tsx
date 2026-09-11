import React, { useState, useMemo } from 'react';
import { Binary, FileText, Cpu, Hash, ShieldAlert } from 'lucide-react';
import { ForensicNode } from '../filetree/TreeNode';

interface FilePreviewProps {
  node: ForensicNode | null;
}

interface HexRow {
  offset: string;
  bytes: number[];
}

// Generates deterministic sample raw sector byte data (0x00 through 0xFF)
const generateHexData = (node: ForensicNode | null) => {
  if (!node) return [];
  const seed = node.id.split('').reduce((acc, char) => acc + char.charCodeAt(0), 0);
  const bytesCount = 256;
  const bytes: number[] = [];

  for (let i = 0; i < bytesCount; i++) {
    bytes.push((seed * (i + 13) + i * 37) % 256);
  }
  return bytes;
};

export const FilePreview: React.FC<FilePreviewProps> = ({ node }) => {
  const [viewMode, setViewMode] = useState<'hex' | 'text'>('hex');

  const hexBytes = useMemo(() => generateHexData(node), [node]);

  if (!node) {
    return (
      <div className="flex h-full flex-col items-center justify-center gap-1.5 border border-ui-outline bg-background-main p-6 text-text-muted">
        <Cpu className="mb-1 h-8 w-8 text-text-muted opacity-40" />
        <span className="text-sm">No target selected</span>
        <span className="text-xs text-text-muted/60">
          Select a node from the file tree to inspect sector headers
        </span>
      </div>
    );
  }

  // Format 256 bytes into 16-byte rows
  const rows: HexRow[] = [];
  for (let i = 0; i < hexBytes.length; i += 16) {
    rows.push({
      offset: i.toString(16).padStart(8, '0').toUpperCase(),
      bytes: hexBytes.slice(i, i + 16),
    });
  }

  const asciiContent = hexBytes
    .map((b) => (b >= 32 && b <= 126 ? String.fromCharCode(b) : '.'))
    .join('');

  return (
    <div className="flex h-full select-none flex-col border border-ui-outline bg-background-main text-sm">
      {/* Header bar */}
      <div className="flex items-center justify-between border-b border-ui-outline bg-background-sidebar px-3 py-2.5">
        <div className="flex min-w-0 items-center gap-2">
          <Binary className="h-4 w-4 shrink-0 text-status-valid" />
          <span className="truncate font-medium text-text-pure">{node.name}</span>
          <span className="rounded-full border border-ui-outline bg-background-icon px-2 py-0.5 text-xs text-text-muted">
            {node.isDirectory ? 'Directory' : node.size || '256 bytes'}
          </span>
        </div>

        {/* View mode controls */}
        <div className="flex shrink-0 items-center rounded-md border border-ui-outline bg-background-icon p-0.5">
          <button
            onClick={() => setViewMode('hex')}
            className={`flex items-center gap-1.5 rounded px-2.5 py-1 text-xs transition-colors ${
              viewMode === 'hex'
                ? 'bg-ui-selection text-text-pure'
                : 'text-text-muted hover:text-text-pure'
            }`}
          >
            <Hash className="h-3 w-3" />
            Hex
          </button>
          <button
            onClick={() => setViewMode('text')}
            className={`flex items-center gap-1.5 rounded px-2.5 py-1 text-xs transition-colors ${
              viewMode === 'text'
                ? 'bg-ui-selection text-text-pure'
                : 'text-text-muted hover:text-text-pure'
            }`}
          >
            <FileText className="h-3 w-3" />
            Text
          </button>
        </div>
      </div>

      {/* Target status indicator */}
      <div className="flex items-center justify-between border-b border-ui-outline bg-background-icon px-3 py-2 text-xs">
        <div className="flex items-center gap-4 text-text-muted">
          <span>
            Range: <span className="font-medium text-text-pure">0x00000000 – 0x000000FF</span>
          </span>
          <span className="flex items-center gap-1.5">
            Status:
            <span
              className={`flex items-center gap-1.5 font-medium ${
                node.isCorrupted ? 'text-status-error' : 'text-status-valid'
              }`}
            >
              <span
                className={`h-1.5 w-1.5 rounded-full ${
                  node.isCorrupted ? 'bg-status-error' : 'bg-status-valid'
                }`}
              />
              {node.isCorrupted ? 'Corrupted sector' : 'Read-only'}
            </span>
          </span>
        </div>
        {node.isDeleted && (
          <div className="flex items-center gap-1 text-status-warning">
            <ShieldAlert className="h-3 w-3" />
            <span>Unallocated target</span>
          </div>
        )}
      </div>

      {/* Main content area */}
      <div className="flex-1 overflow-auto p-3 leading-relaxed">
        {viewMode === 'hex' ? (
          <div className="min-w-[520px] text-xs">
            {/* Hex column headers */}
            <div className="mb-2 grid grid-cols-[80px_1fr_140px] gap-4 border-b border-ui-outline pb-1.5 font-medium text-text-muted">
              <div>Offset</div>
              <div className="grid grid-cols-16 gap-1 text-center">
                {Array.from({ length: 16 }).map((_, idx) => (
                  <span key={idx}>{idx.toString(16).toUpperCase().padStart(2, '0')}</span>
                ))}
              </div>
              <div className="border-l border-ui-outline pl-2">Decoded text</div>
            </div>

            {/* Hex byte rows */}
            {rows.map((row, idx) => {
              const rowAscii = row.bytes
                .map((b) => (b >= 32 && b <= 126 ? String.fromCharCode(b) : '.'))
                .join('');

              return (
                <div
                  key={idx}
                  className="-mx-1 grid grid-cols-[80px_1fr_140px] gap-4 rounded px-1 py-0.5 hover:bg-ui-selection/50"
                >
                  <div className="text-text-muted">{row.offset}</div>

                  <div className="grid grid-cols-16 gap-1 text-center">
                    {row.bytes.map((byte, bIdx) => {
                      const hexStr = byte.toString(16).padStart(2, '0').toUpperCase();
                      const isZero = byte === 0;
                      return (
                        <span
                          key={bIdx}
                          className={isZero ? 'text-text-muted/40' : 'font-medium text-text-pure'}
                        >
                          {hexStr}
                        </span>
                      );
                    })}
                  </div>

                  <div className="truncate border-l border-ui-outline pl-2 tracking-wide text-status-valid">
                    {rowAscii}
                  </div>
                </div>
              );
            })}
          </div>
        ) : (
          <div className="whitespace-pre-wrap break-all rounded-md border border-ui-outline bg-background-sidebar p-3 text-sm text-text-pure">
            {asciiContent}
          </div>
        )}
      </div>
    </div>
  );
};