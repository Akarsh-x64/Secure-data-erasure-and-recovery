import { contextBridge, ipcRenderer, webUtils } from 'electron'
import { electronAPI } from '@electron-toolkit/preload'

// Custom APIs for renderer
const api = {
  minimizeWindow: () => ipcRenderer.send('window-minimize'),
  maximizeWindow: () => ipcRenderer.send('window-maximize'),
  closeWindow: () => ipcRenderer.send('window-close'),
  selectDirectory: () => ipcRenderer.invoke('select-directory'),
  readDirectoryContents: (dirPath: string) => ipcRenderer.invoke('read-directory-contents', dirPath),
  selectFiles: () => ipcRenderer.invoke('select-files'),
  getPathForFile: (file: File) => webUtils.getPathForFile(file),
  eraseFiles: (request: unknown) => ipcRenderer.invoke('erase-files', request),
  getDevices: () => ipcRenderer.invoke('get-devices'),
  createTestVhd: () => ipcRenderer.invoke('create-test-vhd'),
  eraseDrive: (request: unknown) => ipcRenderer.invoke('erase-drive', request),
  getAuditLogs: () => ipcRenderer.invoke('get-audit-logs'),
  saveAuditLog: (entry: unknown) => ipcRenderer.invoke('save-audit-log', entry)
}

// Use `contextBridge` APIs to expose Electron APIs to
// renderer only if context isolation is enabled, otherwise
// just add to the DOM global.
if (process.contextIsolated) {
  try {
    contextBridge.exposeInMainWorld('electron', electronAPI)
    contextBridge.exposeInMainWorld('api', api)
  } catch (error) {
    console.error(error)
  }
} else {
  // @ts-ignore (define in dts)
  window.electron = electronAPI
  // @ts-ignore (define in dts)
  window.api = api
}
