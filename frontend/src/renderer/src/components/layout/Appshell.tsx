import React from 'react';
import Header from './Header';
import ActivityBar, { type NavItem } from './ActivityBar';
import StatusBar from './StatusBar';

interface AppShellProps {
  activeTab: NavItem;
  onTabChange: (tab: NavItem) => void;
  children: React.ReactNode;
}

export default function AppShell({ activeTab, onTabChange, children }: AppShellProps) {
  return (
    <div className="flex h-screen w-full flex-col overflow-hidden bg-background-main text-text-pure">
      {/* Top Window Control Header */}
      <Header />

      {/* Main Workspace Grid */}
      <div className="flex-1 flex overflow-hidden">
        {/* 48px Left Nav */}
        <ActivityBar activeTab={activeTab} onTabChange={onTabChange} />

        {/* Primary Viewport Area */}
        <main className="scrollbar-hidden min-w-0 flex-1 overflow-x-hidden overflow-y-auto bg-background-main p-4">
          {children}
        </main>
      </div>

      {/* Dynamic Status Footer */}
      <StatusBar />
    </div>
  );
}