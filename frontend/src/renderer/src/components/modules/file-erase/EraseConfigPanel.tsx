import { Check, FileKey2, Gauge, ShieldCheck } from 'lucide-react'
import type { ReactElement } from 'react'

export type OverwriteMethod = 'zero' | 'random'

export interface EraseConfig {
  clearMetadata: boolean
  wipeSlackSpace: boolean
  overwriteMethod: OverwriteMethod
  passCount: number
}

interface EraseConfigPanelProps {
  config: EraseConfig
  onChange: (config: EraseConfig) => void
  targetCount: number
  onRequestErase: () => void
}

export function EraseConfigPanel({
  config,
  onChange,
  targetCount,
  onRequestErase
}: EraseConfigPanelProps): ReactElement {
  const nistReady = targetCount > 0 && config.passCount > 0
  const update = (patch: Partial<EraseConfig>): void => onChange({ ...config, ...patch })

  return (
    <section className="flex flex-col rounded-lg border border-ui-outline bg-background-sidebar">
      <div className="border-b border-ui-outline px-4 py-3">
        <h2 className="text-sm font-medium text-text-pure">Sanitization profile</h2>
        <p className="mt-0.5 text-xs text-text-muted">NIST SP 800-88 Rev. 1, logical clear</p>
      </div>

      <div className="space-y-6 p-4">
        <div className="flex items-center justify-between rounded-md border border-ui-outline bg-background-main px-3 py-2.5">
          <div className="flex items-center gap-2">
            <ShieldCheck className="h-4 w-4 text-status-valid" />
            <span className="text-sm text-text-pure">Clear method</span>
          </div>
          <span
            className={`flex items-center gap-1.5 text-xs ${nistReady ? 'text-status-valid' : 'text-status-warning'}`}
          >
            <span
              className={`h-1.5 w-1.5 rounded-full ${nistReady ? 'bg-status-valid' : 'bg-status-warning'}`}
            />
            {nistReady ? 'Compliant' : 'Needs a target'}
          </span>
        </div>

        <div className="space-y-2.5">
          <p className="text-xs font-medium text-text-muted">Metadata handling</p>
          <label className="flex cursor-pointer items-center justify-between rounded-md border border-ui-outline px-3 py-3 transition-colors hover:bg-ui-selection/40">
            <span className="flex items-center gap-2.5 text-sm text-text-pure">
              <FileKey2 className="h-4 w-4 text-text-muted" /> Clear MFT and inode records
            </span>
            <input
              type="checkbox"
              checked={config.clearMetadata}
              onChange={(event) => update({ clearMetadata: event.target.checked })}
              className="h-4 w-4 accent-status-valid"
            />
          </label>
          <label className="flex cursor-pointer items-center justify-between rounded-md border border-ui-outline px-3 py-3 transition-colors hover:bg-ui-selection/40">
            <span className="flex items-center gap-2.5 text-sm text-text-pure">
              <Gauge className="h-4 w-4 text-text-muted" /> Wipe file slack space
            </span>
            <input
              type="checkbox"
              checked={config.wipeSlackSpace}
              onChange={(event) => update({ wipeSlackSpace: event.target.checked })}
              className="h-4 w-4 accent-status-valid"
            />
          </label>
        </div>

        <div className="space-y-2.5">
          <p className="text-xs font-medium text-text-muted">Overwrite pattern</p>
          <div className="grid grid-cols-2 gap-2">
            {(['zero', 'random'] as OverwriteMethod[]).map((method) => (
              <button
                key={method}
                type="button"
                onClick={() => update({ overwriteMethod: method })}
                className={`flex items-center justify-between rounded-md border px-3 py-3 text-left text-sm transition-colors ${
                  config.overwriteMethod === method
                    ? 'border-status-valid/50 bg-status-valid/10 text-text-pure'
                    : 'border-ui-outline text-text-muted hover:bg-ui-selection/40'
                }`}
              >
                <span>{method === 'zero' ? 'Single pass, zero fill' : 'Random overwrite'}</span>
                {config.overwriteMethod === method && (
                  <Check className="h-4 w-4 text-status-valid" />
                )}
              </button>
            ))}
          </div>
        </div>

        <label className="block text-xs font-medium text-text-muted">
          Pass count
          <div className="mt-2.5 flex items-center gap-3">
            <input
              type="range"
              min={1}
              max={7}
              value={config.passCount}
              onChange={(event) => update({ passCount: Number(event.target.value) })}
              className="w-full accent-text-pure"
            />
            <output className="w-9 rounded-md border border-ui-outline bg-background-main py-1 text-center text-sm text-text-pure">
              {config.passCount}
            </output>
          </div>
        </label>

        <button
          type="button"
          disabled={!nistReady}
          onClick={onRequestErase}
          className="flex w-full items-center justify-center gap-2 rounded-md border border-ui-outline bg-ui-selection px-3 py-3 text-sm font-medium text-text-pure transition-colors hover:border-text-pure hover:bg-text-pure hover:text-background-main disabled:cursor-not-allowed disabled:bg-ui-selection disabled:text-text-muted"
        >
          <ShieldCheck className="h-4 w-4" /> Review and erase {targetCount} target
          {targetCount === 1 ? '' : 's'}
        </button>
      </div>
    </section>
  )
}