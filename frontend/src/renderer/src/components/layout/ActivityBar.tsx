import { FileText, HardDrive, RefreshCw, Settings, Trash2, type LucideIcon } from 'lucide-react';
import type { ReactElement } from 'react';

export type NavItem = 'drive-erase' | 'file-erase' | 'recovery' | 'audit' | 'settings';

interface ActivityBarProps {
  activeTab: NavItem;
  onTabChange: (tab: NavItem) => void;
}

export default function ActivityBar({ activeTab, onTabChange }: ActivityBarProps): ReactElement {

  const navItems: { id: NavItem; label: string; icon: LucideIcon }[] = [
    
    {
      id: 'drive-erase',
      label: 'Drive Erase',
      icon: HardDrive,
    },
    {
      id: 'file-erase',
      label: 'File Erase',
      icon: Trash2,
    },
    {
      id: 'recovery',
      label: 'Recovery',
      icon: RefreshCw,
    },
    {
      id: 'audit',
      label: 'Audit Logs',
      icon: FileText,
    },
  ];

  return (
    <aside className="w-12 h-full flex flex-col justify-between items-center bg-background-sidebar border-r border-ui-outline py-2 select-none">
      {/* Primary Tools */}
      <div className="flex flex-col items-center gap-1 w-full">
        {navItems.map((item) => {
          const isActive = activeTab === item.id;
          return (
            <button
              key={item.id}
              onClick={() => onTabChange(item.id)}
              title={item.label}
              className={`relative w-10 h-10 flex items-center justify-center rounded-none transition-colors ${
                isActive
                  ? 'bg-ui-selection text-text-pure'
                  : 'text-text-muted hover:text-text-pure hover:bg-ui-selection/50'
              }`}
            >
              {/* Active Indicator Accent Line */}
              {isActive && (
                <div className="absolute left-0 top-0 bottom-0 w-[2px] bg-text-pure" />
              )}
              <item.icon className="h-5 w-5" strokeWidth={1.5} />
            </button>
          );
        })}
      </div>

      {/* Bottom Settings Button */}
      <div className="w-full flex justify-center">
        <button
          onClick={() => onTabChange('settings')}
          title="Settings"
          className={`relative w-10 h-10 flex items-center justify-center rounded-none transition-colors ${
            activeTab === 'settings'
              ? 'bg-ui-selection text-text-pure'
              : 'text-text-muted hover:text-text-pure hover:bg-ui-selection/50'
          }`}
        >
          {activeTab === 'settings' && (
            <div className="absolute left-0 top-0 bottom-0 w-[2px] bg-text-pure" />
          )}
          <Settings className="h-5 w-5" strokeWidth={1.5} />
        </button>
      </div>
    </aside>
  );
}