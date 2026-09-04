import React from 'react';
import { Download, Eye, File, FileImage, FileText, Puzzle } from 'lucide-react';

export interface RecoveredArtifact {
  id: string;
  name: string;
  type: string;
  size: string;
  fragments: number;
  confidence: number;
  confidenceNote: string;
  sectorOffset: string;
}

interface ResultsTableProps {
  artifacts: RecoveredArtifact[];
  onPreview?: (artifact: RecoveredArtifact) => void;
  onExport?: (artifact: RecoveredArtifact) => void;
}

function typeIcon(type: string): React.ReactNode {
  const t = type.toLowerCase();
  if (['jpeg', 'jpg', 'png', 'gif', 'image'].includes(t)) {
    return <FileImage className="h-4 w-4 text-status-valid" />;
  }
  if (['pdf', 'docx', 'txt', 'document'].includes(t)) {
    return <FileText className="h-4 w-4 text-text-muted" />;
  }
  return <File className="h-4 w-4 text-text-muted" />;
}

function confidenceColor(score: number): { text: string; bar: string } {
  if (score >= 90) return { text: 'text-status-valid', bar: 'bg-status-valid' };
  if (score >= 60) return { text: 'text-status-warning', bar: 'bg-status-warning' };
  return { text: 'text-status-error', bar: 'bg-status-error' };
}

export const ResultsTable: React.FC<ResultsTableProps> = ({ artifacts, onPreview, onExport }) => {
  return (
    <section className="flex min-h-0 flex-1 flex-col rounded-lg border border-ui-outline bg-background-sidebar">
      <div className="flex items-center justify-between border-b border-ui-outline px-4 py-3">
        <div>
          <h2 className="text-sm font-medium text-text-pure">Recovered artifacts</h2>
          <p className="mt-0.5 text-xs text-text-muted">
            {artifacts.length} {artifacts.length === 1 ? 'file' : 'files'} reconstructed
          </p>
        </div>
      </div>

      <div className="scrollbar-hidden min-h-0 flex-1 overflow-x-hidden overflow-y-auto">
        <table className="w-full table-fixed border-collapse text-left text-sm">
          <thead className="sticky top-0 z-10 bg-background-sidebar text-xs text-text-muted">
            <tr>
              <th className="border-b border-ui-outline px-4 py-2 font-normal">Name</th>
              <th className="border-b border-ui-outline px-2 py-2 font-normal">Size</th>
              <th className="border-b border-ui-outline px-2 py-2 font-normal">Fragments</th>
              <th className="border-b border-ui-outline px-2 py-2 font-normal">Sector offset</th>
              <th className="border-b border-ui-outline px-2 py-2 font-normal">Confidence</th>
              <th className="border-b border-ui-outline px-2 py-2" aria-label="Actions" />
            </tr>
          </thead>
          <tbody>
            {artifacts.map((artifact) => {
              const confidence = confidenceColor(artifact.confidence);
              return (
                <tr
                  key={artifact.id}
                  className="group border-b border-ui-outline/60 transition-colors hover:bg-ui-selection/40"
                >
                  <td className="max-w-[16rem] px-4 py-3">
                    <div className="flex min-w-0 items-center gap-2.5">
                      {typeIcon(artifact.type)}
                      <span className="truncate text-text-pure" title={artifact.name}>
                        {artifact.name}
                      </span>
                    </div>
                  </td>
                  <td className="whitespace-nowrap px-2 py-3 text-text-muted">{artifact.size}</td>
                  <td className="whitespace-nowrap px-2 py-3 text-text-muted">
                    <span className="flex items-center gap-1.5">
                      <Puzzle className="h-3.5 w-3.5" />
                      {artifact.fragments}
                    </span>
                  </td>
                  <td className="whitespace-nowrap px-2 py-3 text-text-muted">
                    {artifact.sectorOffset}
                  </td>
                  <td className="min-w-0 px-2 py-3">
                    <div className="flex min-w-0 items-center gap-2">
                      <div className="h-1.5 w-16 overflow-hidden rounded-full bg-ui-outline">
                        <div
                          className={`h-full rounded-full ${confidence.bar}`}
                          style={{ width: `${artifact.confidence}%` }}
                        />
                      </div>
                      <span className={`truncate text-xs ${confidence.text}`} title={`${artifact.confidence}%, ${artifact.confidenceNote}`}>
                        {artifact.confidence}%, {artifact.confidenceNote}
                      </span>
                    </div>
                  </td>
                  <td className="px-2 py-3 text-right">
                    <div className="flex items-center justify-end gap-1 opacity-0 transition-opacity group-hover:opacity-100">
                      <button
                        type="button"
                        onClick={() => onPreview?.(artifact)}
                        title={`Preview ${artifact.name}`}
                        className="rounded-md p-1.5 text-text-muted transition-colors hover:bg-ui-selection hover:text-text-pure"
                      >
                        <Eye className="h-3.5 w-3.5" />
                      </button>
                      <button
                        type="button"
                        onClick={() => onExport?.(artifact)}
                        title={`Export ${artifact.name}`}
                        className="rounded-md p-1.5 text-text-muted transition-colors hover:bg-status-valid/15 hover:text-status-valid"
                      >
                        <Download className="h-3.5 w-3.5" />
                      </button>
                    </div>
                  </td>
                </tr>
              );
            })}
            {artifacts.length === 0 && (
              <tr>
                <td colSpan={6} className="px-4 py-14 text-center text-sm text-text-muted">
                  No artifacts recovered yet. Run a scan to populate results.
                </td>
              </tr>
            )}
          </tbody>
        </table>
      </div>
    </section>
  );
};