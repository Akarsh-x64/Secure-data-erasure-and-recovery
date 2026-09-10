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
    selectFiles?: () => Promise<SelectedFileItem[] | null>
    getPathForFile?: (file: File) => string
    eraseFiles?: (request: unknown) => Promise<{ accepted: boolean; message?: string; directErased?: boolean; count?: number }>
  }

  interface Window {
    electron: ElectronAPI
    api: WindowControlsAPI
  }
}
