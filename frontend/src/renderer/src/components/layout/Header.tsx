import React from 'react';

const menuItems = {
  File: ['New Case', 'Open Evidence', 'Save Case'],
  Edit: ['Undo', 'Redo'],
  View: ['Explorer', 'Activity Bar', 'Status Bar'],
};

export default function Header() {
  return (
    <header 
      style={{ WebkitAppRegion: 'drag' } as React.CSSProperties}
      className="h-8 w-full bg-background-sidebar border-b border-ui-outline flex items-center justify-between px-3 select-none"
    >
      {/* Left: App Title / Menu Bar */}
      <div style={{ WebkitAppRegion: 'no-drag' } as React.CSSProperties} className="flex items-center gap-3">
        <span className="font-sans text-xs font-bold tracking-wider text-text-pure">NTRO</span>
        <span className="text-ui-outline font-sans text-xs">|</span>

        <nav className="flex h-full items-center gap-1 font-sans text-xs" aria-label="Application menu">
          {(Object.entries(menuItems) as [keyof typeof menuItems, string[]][]).map(([label, items]) => (
            <div key={label} className="group relative h-full">
              <button
                type="button"
                className="flex h-full items-center gap-1 rounded-md px-2 text-text-muted hover:bg-ui-selection hover:text-text-pure"
                aria-haspopup="true"
              >
                {label}
              </button>

              <div className="invisible absolute left-0 top-full z-50 min-w-40 rounded-md border border-ui-outline bg-ui-selection py-2 opacity-0 shadow-lg transition-opacity group-hover:visible group-hover:opacity-100">
                {items.map((item) => (
                  <button
                    key={item}
                    type="button"
                    className="block w-full whitespace-nowrap rounded-sm px-3 py-1.5 text-left text-xs text-text-muted hover:bg-ui-selection hover:text-text-pure"
                  >
                    {item}
                  </button>
                ))}
              </div>
            </div>
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
        className="h-full px-3 text-text-muted hover:bg-ui-selection hover:text-text-pure"
      >
        <svg className="w-3 h-3" fill="none" viewBox="0 0 24 24" stroke="currentColor" strokeWidth="2">
          <path strokeLinecap="square" d="M6 18L18 6M6 6l12 12" />
        </svg>
      </button>
      
    </div>
    </header>
  );
}