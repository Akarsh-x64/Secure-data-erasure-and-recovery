import React, { useRef, useState } from 'react';
import { Search, X } from 'lucide-react';

interface LogFilterProps {
  value: string;
  onChange: (value: string) => void;
  resultCount: number;
}

const SUGGESTED_TOKENS = [
  { token: 'action:drive-erase', hint: 'Filter by action' },
  { token: 'action:file-erase', hint: 'Filter by action' },
  { token: 'action:recovery-scan', hint: 'Filter by action' },
  { token: 'level:error', hint: 'Filter by severity' },
  { token: 'level:warning', hint: 'Filter by severity' },
  { token: 'operator:', hint: 'Filter by operator id' },
  { token: 'verified:false', hint: 'Filter by verification status' },
];

export const LogFilter: React.FC<LogFilterProps> = ({ value, onChange, resultCount }) => {
  const [focused, setFocused] = useState(false);
  const inputRef = useRef<HTMLInputElement>(null);

  const appendToken = (token: string): void => {
    const trimmed = value.trim();
    onChange(trimmed.length > 0 ? `${trimmed} ${token}` : token);
    inputRef.current?.focus();
  };

  return (
    <div className="rounded-lg border border-ui-outline bg-background-sidebar">
      <div className="flex items-center gap-2 px-3 py-2.5">
        <Search className="h-4 w-4 shrink-0 text-text-muted" />
        <input
          ref={inputRef}
          type="text"
          value={value}
          onChange={(e) => onChange(e.target.value)}
          onFocus={() => setFocused(true)}
          onBlur={() => setFocused(false)}
          placeholder="Filter logs, e.g. action:drive-erase level:error"
          spellCheck={false}
          className="min-w-0 flex-1 bg-transparent text-sm text-text-pure outline-none placeholder:text-text-muted/60"
        />
        {value.length > 0 && (
          <button
            type="button"
            onClick={() => onChange('')}
            title="Clear filter"
            className="rounded-md p-1 text-text-muted transition-colors hover:bg-ui-selection hover:text-text-pure"
          >
            <X className="h-3.5 w-3.5" />
          </button>
        )}
        <span className="shrink-0 rounded-full border border-ui-outline bg-background-icon px-2 py-0.5 text-xs text-text-muted">
          {resultCount} {resultCount === 1 ? 'entry' : 'entries'}
        </span>
      </div>

      {focused && (
        <div className="flex flex-wrap gap-1.5 border-t border-ui-outline px-3 py-2">
          {SUGGESTED_TOKENS.map((suggestion) => (
            <button
              key={suggestion.token}
              type="button"
              onMouseDown={(e) => {
                e.preventDefault();
                appendToken(suggestion.token);
              }}
              title={suggestion.hint}
              className="rounded-full border border-ui-outline bg-background-main px-2.5 py-1 text-xs text-text-muted transition-colors hover:bg-ui-selection hover:text-text-pure"
            >
              {suggestion.token}
            </button>
          ))}
        </div>
      )}
    </div>
  );
};