import React from 'react';

interface HeaderProps {
  breadcrumbs?: string[];
}

export default function Header({ breadcrumbs = ['root', 'target_drive', 'unallocated'] }: HeaderProps) {
  return (
    <header 
      style={{ WebkitAppRegion: 'drag' } as React.CSSProperties}
      className="h-8 w-full bg-background-sidebar border-b border-ui-outline flex items-center justify-between px-3 select-none"
    >
      {/* Left: App Title / Identifier */}
      <div className="flex items-center gap-2">
        <span className="font-sans text-xs font-bold tracking-wider text-text-pure">NTRO</span>
        <span className="text-ui-outline font-sans text-xs">|</span>
        
        {/* Breadcrumb Path */}
        <nav className="flex items-center gap-1.5 font-sans text-xs">
          {breadcrumbs.map((crumb, idx) => (
            <React.Fragment key={crumb}>
              {idx > 0 && <span className="text-text-muted/40 font-sans">&gt;</span>}
              <span className={idx === breadcrumbs.length - 1 ? 'text-text-pure' : 'text-text-muted'}>
                {crumb}
              </span>
            </React.Fragment>
          ))}
        </nav>
      </div>

      {/* Right: Window Controls (Non-draggable) */}
      <div style={{ WebkitAppRegion: 'no-drag' } as React.CSSProperties} className="flex items-center -mr-3 h-full">
  
      <button 
        onClick={() => window.api.minimizeWindow()}
        className="h-full px-3 text-text-muted hover:text-text-pure hover:bg-ui-selection"
      >
        <div className="w-2.5 h-[1px] bg-current" />
      </button>
      
      <button 
        onClick={() => window.api.maximizeWindow()}
        className="h-full px-3 text-text-muted hover:text-text-pure hover:bg-ui-selection"
      >
        <div className="w-2.5 h-2.5 border border-current rounded-none" />
      </button>
      
      <button 
        onClick={() => window.api.closeWindow()}
        className="h-full px-3 text-text-muted hover:text-text-pure hover:bg-status-error"
      >
        <svg className="w-3 h-3" fill="none" viewBox="0 0 24 24" stroke="currentColor" strokeWidth="2">
          <path strokeLinecap="square" d="M6 18L18 6M6 6l12 12" />
        </svg>
      </button>
      
    </div>
    </header>
  );
}