import { ElectronAPI } from '@electron-toolkit/preload'

declare global {
  interface SelectedFileItem {
    id: string
    path: string
    name: string
    size: string
    isDirectory: boolean
  }

  interface WindowControlsAPI {
    minimizeWindow: () => void
    maximizeWindow: () => void
    closeWindow: () => void
    selectDirectory?: () => Promise<{ path: string; name: string; nodes: any[] } | null>
    readDirectoryContents?: (dirPath: string) => Promise<any[]>
    selectFiles?: () => Promise<SelectedFileItem[] | null>
    getPathForFile?: (file: File) => string
    eraseFiles?: (request: unknown) => Promise<{ accepted: boolean; message?: string; directErased?: boolean; count?: number; verifications?: any[] }>
    getDevices?: () => Promise<any[]>
    createTestVhd?: () => Promise<{ success: boolean; message?: string; error?: string }>
    eraseDrive?: (request: unknown) => Promise<{ accepted: boolean; message?: string; operationId?: string; directWiped?: boolean; verifications?: any[]; error?: string }>
    getAuditLogs?: () => Promise<any[]>
    saveAuditLog?: (entry: unknown) => Promise<void>
  }

  interface Window {
    electron: ElectronAPI
    api: WindowControlsAPI
  }
}
