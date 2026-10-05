# Python Flask & Socket.IO Backend Architecture

## 1. Overview & Role

The **Backend Subsystem** (`backend/main.py`) acts as the intermediary between the high-level Electron React UI and the low-level C++ pybind11 modules. It runs a lightweight, high-concurrency **Flask + Flask-SocketIO** server on port `5000`.

### Key Responsibilities
1. **Privilege Elevation**: Enforces administrative rights on startup, requesting Windows UAC elevation if needed.
2. **REST API**: Serves endpoints for device discovery, directory tree exploration, and erasure validation.
3. **Real-Time Telemetry Streaming**: Delivers live progress updates (`percent`, `phase`, `throughputMBps`, `etaSeconds`) via WebSockets to the Electron renderer.
4. **Asynchronous Job Workers**: Dispatches destructive erasure operations to isolated background threads, preventing UI lockup.
5. **Safety Barriers**: Requires client-generated idempotency keys and explicit confirmation strings (`CONFIRM_ERASE`, `CONFIRM_WIPE`).
6. **Automated Filesystem Repair**: Automatically runs native filesystem checkers (`chkdsk` or `e2fsck`) after erasure to guarantee zero filesystem corruption.

---

## 2. Elevation Supervisor (`ensure_admin`)

Because raw disk access (`\\.\PhysicalDriveX` and `\\.\E:`) fails with Access Denied without elevation:
```python
def ensure_admin():
    if os.name == 'nt':
        try:
            import ctypes
            is_admin = ctypes.windll.shell32.IsUserAnAdmin()
        except:
            is_admin = False
        
        if not is_admin:
            print("[INFO] Requesting Windows Administrator privileges...")
            import ctypes, sys
            ctypes.windll.shell32.ShellExecuteW(
                None, "runas", sys.executable, " ".join(sys.argv), None, 1
            )
            sys.exit(0)
    else:
        if os.geteuid() != 0:
            print("[ERROR] Please run this script with sudo.")
            sys.exit(1)
```

---

## 3. Data Contracts & Pydantic Models

All requests and responses adhere to strict Pydantic schemas:

### 3.1 Operation Lifecycle
```python
OperationState = Literal['queued', 'running', 'verifying', 'completed', 'failed', 'cancelled']

class OperationRef(BaseModel):
    operationId: str
    state: OperationState
    createdAt: str

class ProgressEvent(BaseModel):
    operationId: str
    state: OperationState
    phase: str  # 'preparing' | 'erasing' | 'verifying' | 'scanning'
    percent: int
    currentBytes: Optional[int] = None
    totalBytes: Optional[int] = None
    currentSector: Optional[int] = None
    totalSectors: Optional[int] = None
    passIndex: Optional[int] = None
    passTotal: Optional[int] = None
    throughputMBps: Optional[int] = None
    etaSeconds: Optional[int] = None
    message: Optional[str] = None
    timestamp: str
```

### 3.2 Storage Device Models
```python
class DeviceCapabilities(BaseModel):
    fileErase: bool
    driveErase: bool
    secureErase: bool
    trim: bool
    cryptoErase: bool

class StorageDevice(BaseModel):
    id: str
    path: str
    model: str
    serial: Optional[str] = None
    busType: Literal['SATA', 'NVMe', 'USB', 'unknown']
    capacityBytes: int
    sectorSizeBytes: int
    health: Literal['healthy', 'warning', 'critical', 'unknown']
    mounted: bool
    readOnly: bool
    writeProtected: bool
    identityToken: str
    capabilities: DeviceCapabilities
```

### 3.3 File Tree & Erasure Configurations
```python
class EraseTarget(BaseModel):
    nodeId: str
    canonicalPath: str
    kind: Optional[str] = None
    filesystem: str  # 'NTFS' | 'ext4' | 'exFAT' | 'FAT32'

class EraseConfig(BaseModel):
    clearMetadata: bool
    wipeSlackSpace: bool
    overwriteMethod: Literal["zero", "random", "dod"]
    passCount: int
```

---

## 4. REST API Endpoint Specifications

### 4.1 Device Discovery
* **Endpoint**: `GET /api/v1/devices`
* **Response**: List of `StorageDevice` objects detailing connected drives, capacities, bus types, and capabilities.

### 4.2 Filesystem Node Explorer
* **Endpoint**: `GET /api/v1/file-node`
* **Response**: Hierarchical `FileNode` tree representing partitions, folders, active files, and recovered/deleted files.

### 4.3 Surgical File Erasure Pipeline
1. **Validate**: `POST /api/v1/erase/files/validate`
   - Validates paths, filesystems, and configurations before prompting the user.
2. **Execute**: `POST /api/v1/erase/files`
   - **Headers**: `Idempotency-Key: <uuid>` (prevents duplicate execution).
   - **Body**:
     ```json
     {
       "targets": [{ "nodeId": "node-1", "canonicalPath": "C:\\evidence\\audit.log", "filesystem": "NTFS" }],
       "config": { "clearMetadata": true, "wipeSlackSpace": true, "overwriteMethod": "dod", "passCount": 3 },
       "confirmation": "CONFIRM_ERASE"
     }
     ```
   - **Response**: `202 Accepted` returning `OperationRef` (`operationId`, `state: queued`, `auditId`).
   - Spawns `background_file_erase_worker`.

### 4.4 Drive-Wide Erasure Pipeline
1. **Validate**: `POST /api/v1/erase/drives/validate`
2. **Execute**: `POST /api/v1/erase/drives`
   - **Headers**: `Idempotency-Key: <uuid>`
   - **Body**: Requires `"confirmation": "CONFIRM_WIPE"`.
   - **Response**: `202 Accepted` returning `OperationRef`.
   - Spawns `background_drive_erase_worker`.

---

## 5. Background Workers & Telemetry Emission

### 5.1 File Erasure Worker (`background_file_erase_worker`)
1. Emits initial WebSocket progress: `{ state: "running", phase: "preparing", percent: 0 }`.
2. Groups target files by physical drive and filesystem: `(device_path, fs_type)`.
3. Opens the storage device via `osdevice.WindowsStorageDevice()` or `LinuxStorageDevice()`.
4. Obtains exclusive locks: `device.LockVolume()` and `device.DismountVolume()`.
5. Initializes Layer 2 `hdd.HDDController(device)` and mounts Layer 3 driver (`fs_driver.Mount()`).
6. Iterates targets, calling `fs_driver.EraseFile(rel_path)` and emitting real-time percentage updates via Socket.IO (`socketio.emit('progress', event)`).
7. Releases device handle and runs `repair_filesystem()`.
8. Emits final status: `{ state: "completed", phase: "verifying", percent: 100 }`.

---

## 6. Automated Post-Erasure Filesystem Repair (`repair_filesystem`)

Modifying on-disk structures directly while an operating system is running can leave minor flag discrepancies. To ensure that the OS never reports errors or marks volumes dirty, the backend automatically triggers an OS repair tool upon closing the disk handle:

### Windows Systems
Runs native `chkdsk` in fix mode:
```python
cmd = ['chkdsk', volume_name, '/f', '/x']
subprocess.run(cmd, capture_output=True, text=True, shell=True)
```
* `/f`: Fixes errors on the disk.
* `/x`: Forces the volume to dismount first if necessary.

### Linux Systems
Uses bundled repair utilities located in `_externals/`:
* **ext4**: `_externals/e2fsck -y -f <device>`
* **exFAT**: `_externals/fsck.exfat -a <device>`
* **FAT32**: `_externals/fsck.fat -a <device>`
* **NTFS**: `_externals/ntfsfix -d <device>` (clears NTFS dirty flag)
* **XFS**: `_externals/xfs_repair <device>`
