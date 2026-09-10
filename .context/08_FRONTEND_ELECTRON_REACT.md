# Electron & React Desktop Frontend Architecture

## 1. Overview & Technology Stack

The SanitizeX desktop frontend provides an intuitive, high-visibility user interface for defense analysts, forensic investigators, and system administrators.

```
┌────────────────────────────────────────────────────────────────────────┐
│                     FRONTEND TECHNOLOGY STACK                          │
├───────────────────┬────────────────────────────────────────────────────┤
│ Desktop Shell     │ Electron 44 (Native Win32/Linux Window Management) │
│ Build / HMR       │ electron-vite 5 + Vite 7 (High-Speed Dev Server)   │
│ UI Framework      │ React 19 + TypeScript 5.9                          │
│ Styling / Tokens  │ Tailwind CSS 3.4 (Custom Cyberpunk/Dark UI Theme)  │
│ Iconography       │ Lucide React Icons                                 │
└───────────────────┴────────────────────────────────────────────────────┘
```

The frontend source code is located in [frontend/](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/frontend).

---

## 2. Process Architecture & Boundaries

The frontend strictly separates privileged operating system operations from the user interface:

```
┌────────────────────────────────────────────────────────────────────────┐
│                        ELECTRON MULTI-PROCESS MODEL                    │
├────────────────────────────────────────────────────────────────────────┤
│ RENDERER PROCESS (React 19 UI)                                         │
│ • Sandboxed: contextIsolation = true, nodeIntegration = false          │
│ • Renders FileTree, Drive Selector, Recovery Table, and Audit Gauges   │
│ • ZERO direct access to Node.js fs, child_process, or raw handles      │
│     │                                                                  │
│     ▼                                                                  │
│ PRELOAD SCRIPT (frontend/src/preload/index.ts)                         │
│ • Uses contextBridge.exposeInMainWorld('api', ...)                     │
│ • Exposes strictly typed methods (minimize, maximize, eraseFiles)      │
│     │                                                                  │
│     ▼                                                                  │
│ MAIN PROCESS (frontend/src/main/index.ts)                              │
│ • Manages frameless desktop window (frame: false)                      │
│ • IPC event handlers (ipcMain.handle('erase-files'))                   │
│ • Connects to native C++ backend / Python REST/Socket.IO server        │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Main Process & Preload API

### 3.1 Main Process Configuration (`src/main/index.ts`)
* **Frameless Window**: Configured with `frame: false` and `autoHideMenuBar: true` to support a modern, bespoke dark-theme title bar.
* **Window Dimensions**: Default $900 \times 670$ pixels (resizable).
* **HMR vs Production Loading**:
  - Development: Loads `process.env['ELECTRON_RENDERER_URL']` with Hot Module Replacement (HMR).
  - Production: Loads static bundled files via `mainWindow.loadFile(join(__dirname, '../renderer/index.html'))`.
* **IPC Handlers**:
  - `window-minimize`, `window-maximize`, `window-close`
  - `erase-files`: Receives targets and configuration from the renderer.

### 3.2 Preload Script (`src/preload/index.ts`)
Safely bridges window controls and erasure APIs into `window.api`:
```typescript
const api = {
  minimizeWindow: () => ipcRenderer.send('window-minimize'),
  maximizeWindow: () => ipcRenderer.send('window-maximize'),
  closeWindow: () => ipcRenderer.send('window-close'),
  eraseFiles: (request: unknown) => ipcRenderer.invoke('erase-files', request)
}

if (process.contextIsolated) {
  contextBridge.exposeInMainWorld('electron', electronAPI)
  contextBridge.exposeInMainWorld('api', api)
}
```

---

## 4. Renderer Application Modules (`src/renderer/src/`)

The main renderer is structured around 4 primary operational modules switched via the `ActivityBar`:

```
┌────────────────────────────────────────────────────────────────────────┐
│                           APP SHELL LAYOUT                             │
├──────────────┬─────────────────────────┬───────────────────────────────┤
│ ActivityBar  │ FileSystemTree Explorer │ Active Module Tab             │
│ [File Erase] │ (Collapsible Sidebar)   │ (FileEraseTab / DriveEraseTab/│
│ [Drive Erase]│                         │  RecoveryTab / AuditLogsTab)  │
│ [Recovery]   │                         │                               │
│ [Audit Logs] │                         │                               │
└──────────────┴─────────────────────────┴───────────────────────────────┘
```

### 4.1 Module 1: File & Folder Erasure (`FileEraseTab.tsx`)
* **Source Files**: [src/renderer/src/components/modules/file-erase/](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/frontend/src/renderer/src/components/modules/file-erase)
* **Components**:
  - `FileSystemTree.tsx`: Displays hierarchical folder trees with live indicators for file health, size, cluster allocation, and deletion status. Supports selective item marking and queueing.
  - `SelectedFilesPanel.tsx`: Displays target files queued for destruction, indicating filesystem (`NTFS`, `ext4`, `exFAT`, `FAT32`), cluster sizes, and physical sector spans.
  - `EraseConfigPanel.tsx`: Configures overwrite method (`zero`, `random`, `dod`), pass count ($1 \dots 7$), slack space wiping toggle, and metadata obliteration toggle.
  - `EraseConfirmDialog.tsx`: Safety modal requiring explicit user acknowledgement before dispatching the destructive IPC command.

### 4.2 Module 2: Whole Drive Erasure (`DriveEraseTab.tsx`)
* **Source Files**: [src/renderer/src/components/modules/drive-erase/](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/frontend/src/renderer/src/components/modules/drive-erase)
* **Capabilities**:
  - Interactive Drive Selector: Lists physical devices (model, bus type `NVMe`/`SATA`/`USB`, capacity, sector size).
  - Sanitization Standard Selector:
    - **DoD 5220.22-M** (3-Pass: Zeroes, Ones, PRNG)
    - **NIST SP 800-88 Clear** (1-Pass Zero-fill)
    - **NIST SP 800-88 Purge** (Hardware NVMe Sanitize / ATA Secure Erase)
  - Safety Lockout: Requires typing `CONFIRM_WIPE` to prevent accidental sanitization of system drives.

### 4.3 Module 3: Forensic Data Recovery (`RecoveryTab.tsx`)
* **Source Files**: [src/renderer/src/components/modules/recovery/](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/frontend/src/renderer/src/components/modules/recovery)
* **Capabilities**:
  - `DiskImageSelector.tsx`: Registers raw forensic disk images (`.dd`, `.raw`, `.img`).
  - `RecoveryOptions.tsx`: Toggleable 120+ file signature checklist (Documents, Images, Media, Binaries).
  - `ResultsTable.tsx`: Live results view displaying discovered artifacts, recovery confidence scores ($0.0 \dots 1.0$), recovered file sizes, and verification status.

### 4.4 Module 4: Forensic Audit & Certificates (`AuditLogsTabs.tsx`)
* **Source Files**: [src/renderer/src/components/modules/audit-logs/](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/frontend/src/renderer/src/components/modules/audit-logs)
* **Capabilities**:
  - Live inspection of cryptographic pre-wipe and post-wipe SHA-256 digests.
  - Real-time Shannon Entropy visual graphs and Chi-Square distribution histograms.
  - Exportable, tamper-evident Sanitization Certificates (PDF and signed JSON formats).

---

## 5. Development & Build Commands

* **Run Dev Server with Hot Reloading**:
  ```powershell
  cd frontend
  npm run dev
  ```
* **Type-check TypeScript**:
  ```powershell
  npm run typecheck
  ```
* **Build Production Windows Portable & Installer (.exe)**:
  ```powershell
  npm run build:win
  ```
