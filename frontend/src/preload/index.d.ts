import { ElectronAPI } from '@electron-toolkit/preload'

declare global {
  interface WindowControlsAPI {
    minimizeWindow: () => void
    maximizeWindow: () => void
    closeWindow: () => void
    eraseFiles?: (request: unknown) => Promise<{ accepted: boolean }>
  }

  interface Window {
    electron: ElectronAPI
    api: WindowControlsAPI
  }
}
