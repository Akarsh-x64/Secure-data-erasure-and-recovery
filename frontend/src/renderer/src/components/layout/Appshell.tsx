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
    <div className="flex flex-col h-screen w-screen overflow-hidden bg-background-main text-text-pure">
      {/* Top Window Control Header */}
      <Header />

      {/* Main Workspace Grid */}
      <div className="flex-1 flex overflow-hidden">
        {/* 48px Left Nav */}
        <ActivityBar activeTab={activeTab} onTabChange={onTabChange} />

        {/* Primary Viewport Area */}
        <main className="min-w-0 flex-1 overflow-auto bg-background-main p-4">
          {children}
        </main>
      </div>

      {/* Dynamic Status Footer */}
      <StatusBar />
    </div>
  );
}