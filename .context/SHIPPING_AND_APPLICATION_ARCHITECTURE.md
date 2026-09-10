# Shipping Formats, Application Architecture & Recovery Subsystem Manual

This document details the complete end-to-end product architecture, packaging and distribution strategy, Electron frontend integration, native C++ backend IPC protocol, and the comprehensive **Data Recovery Subsystem** (`Recovery/`) operating alongside the **Secure Erasure Engine** (`Erasure/`).

---

# Table of Contents
1. [Product Distribution & The 3 Shipping Formats](#1-product-distribution--the-3-shipping-formats)
   - 1.1 [Format 1: Windows Native Application](#11-format-1-windows-native-application)
   - 1.2 [Format 2: Linux Native Application](#12-format-2-linux-native-application)
   - 1.3 [Format 3: Dedicated Live Boot Environment (Formatting C: & System Drives)](#13-format-3-dedicated-live-boot-environment-formatting-c--system-drives)
   - 1.4 [Comparative Matrix of Shipping Formats](#14-comparative-matrix-of-shipping-formats)
2. [Application Architecture: Electron GUI + C++ Native Backend](#2-application-architecture-electron-gui--c-native-backend)
   - 2.1 [High-Level Architectural Model](#21-high-level-architectural-model)
   - 2.2 [Separation of Concerns & Privilege Boundaries](#22-separation-of-concerns--privilege-boundaries)
   - 2.3 [IPC Protocol Specification (JSON-RPC over stdio / Named Pipes)](#23-ipc-protocol-specification-json-rpc-over-stdio--named-pipes)
   - 2.4 [Real-Time Telemetry & Progress Streaming](#24-real-time-telemetry--progress-streaming)
3. [Data Recovery Subsystem Architecture (`Recovery/`)](#3-data-recovery-subsystem-architecture-recovery)
   - 3.1 [Phase 1: Read-Only Storage Foundation (`Recovery/Core/` & `Recovery/Acquisition/`)](#31-phase-1-read-only-storage-foundation-recoverycore--recoveryacquisition)
   - 3.2 [Phase 2: Partition Parsing (`Recovery/Partitions/`)](#32-phase-2-partition-parsing-recoverypartitions)
   - 3.3 [Phase 3: Filesystem Detection (`Recovery/Filesystems/`)](#33-phase-3-filesystem-detection-recoveryfilesystems)
   - 3.4 [Phase 4: Deep NTFS File Recovery (`Recovery/Filesystems/NTFS/`)](#34-phase-4-deep-ntfs-file-recovery-recoveryfilesystemsntfs)
4. [The Adversarial Verification Feedback Loop (Erasure ↔ Recovery)](#4-the-adversarial-verification-feedback-loop-erasure--recovery)
   - 4.1 [Architectural Philosophy: Recovery as the Verification Auditor](#41-architectural-philosophy-recovery-as-the-verification-auditor)
   - 4.2 [End-to-End Audit Sequence](#42-end-to-end-audit-sequence)
   - 4.3 [Forensic Certificate of Sanitization](#43-forensic-certificate-of-sanitization)
5. [Native Python pybind11 Extension Subsystem (`modules/`)](#5-native-python-pybind11-extension-subsystem-modules)
   - 5.1 [Architecture & Modular Isolation](#51-architecture--modular-isolation)
   - 5.2 [Supported Python Extension Modules](#52-supported-python-extension-modules)
   - 5.3 [Automated Cross-Platform Toolchains & Testing](#53-automated-cross-platform-toolchains--testing)

---

# 1. Product Distribution & The 3 Shipping Formats

The software is engineered to be distributed in **three distinct formats** to satisfy different operational requirements, security environments, and hardware constraints:

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                               3 PRODUCT SHIPPING TARGETS                               │
├──────────────────────────┬─────────────────────────────┬───────────────────────────────┤
│ FORMAT 1: WINDOWS APP    │ FORMAT 2: LINUX APP         │ FORMAT 3: LIVE BOOT OS (ISO)  │
│ - Portable .exe / MSI    │ - AppImage / .deb / .rpm    │ - Standalone RAM-disk Live OS │
│ - Electron + Win32 C++   │ - Electron + POSIX C++      │ - Custom Linux Live (toram)   │
│ - Wipes secondary drives │ - Wipes secondary drives    │ - 100% UNLOCKED HARDWARE      │
│ - Restricted on C:\      │ - Restricted on root (/)    │ - Wipes C:\ & System Boot M.2 │
└──────────────────────────┴─────────────────────────────┴───────────────────────────────┘
```

---

## 1.1 Format 1: Windows Native Application

### Target Environment
* **Supported OS**: Windows 10, Windows 11, Windows Server 2016/2019/2022 (x64 and ARM64).
* **Packaging**: Single-file portable executable (`.exe`) or Windows Installer (`.msi`) built via `electron-builder`.

### Execution Model & Privilege Elevation
* **Application Manifest**: Embedded with `<requestedExecutionLevel level="requireAdministrator" uiAccess="false" />`.
* **Privilege Requirement**: Low-level disk operations (`CreateFileA("\\\\.\\PhysicalDriveX")`, `FSCTL_LOCK_VOLUME`, `FSCTL_DISMOUNT_VOLUME`) require full Administrator rights. Launching triggers a standard Windows User Account Control (UAC) prompt.

### Capabilities & Operational Scope
* **Target Storage**:
  * Secondary internal storage drives (`D:\`, `E:\`, secondary SATA HDDs, secondary NVMe SSDs).
  * External storage: USB thumb drives, external USB hard drives, SD/microSD cards.
  * Virtual Disk images: `.vhd`, `.vhdx`, and encrypted VeraCrypt container files.
* **Dual Operation**: Supports both **Secure Erasure** (DoD 3-Pass, surgical file erasure, folder wiping, volume formatting) and **Data Recovery** (deep partition scans, unallocated file carving).

### The Inherent Host OS Limitation: The `C:\` Drive Lock
* **The Problem**: On an active, booted Windows operating system, the system volume (`C:\`) cannot be fully formatted, unmounted, or overwritten at the raw sector level.
* **Kernel Lock Mechanisms**:
  1. `FSCTL_LOCK_VOLUME` returns `ERROR_ACCESS_DENIED` (5) or `ERROR_SHARING_VIOLATION` (32) because the Windows kernel (`ntoskrnl.exe`), active device drivers, system registry hives (`SAM`, `SYSTEM`, `SOFTWARE`), and active services hold permanent exclusive handles on `C:\`.
  2. The virtual memory pagefile (`pagefile.sys`) and hibernation file (`hiberfil.sys`) lock vast contiguous cluster ranges.
  3. Direct write operations (`WriteFile` to `\\.\C:`) are blocked by the Windows Volume Manager filter driver for any sector within the mounted filesystem boundaries.
* **Conclusion**: While the Windows application excels at surgical file deletion, folder purging, and secondary drive erasure, **formatting the active Windows boot drive (`C:\`) is physically impossible from within that running Windows OS**.

---

## 1.2 Format 2: Linux Native Application

### Target Environment
* **Supported Distributions**: Ubuntu, Debian, Fedora, Arch Linux, RHEL/CentOS, openSUSE.
* **Packaging**: Standalone AppImage, `.deb` package, `.rpm` package, or tarball.

### Execution Model & Privilege Elevation
* **Root / Sudo Elevation**:
  * Raw block devices (`/dev/sdX`, `/dev/nvmeXnY`) in Linux are owned by `root:disk` with `0660` permissions.
  * Accessing raw disk sectors, sending pass-through IOCTLs (`NVME_IOCTL_ADMIN_CMD`, `SG_IO`, `HDIO_DRIVE_CMD`), and flushing filesystem caches (`BLKFLSBUF`, `ioctl(BLKRRPART)`) requires `CAP_SYS_RAWIO` and `CAP_SYS_ADMIN`.
  * The application prompts for elevation via `pkexec` (PolicyKit GUI prompt) or requires launch via `sudo`.

### Capabilities & Operational Scope
* **Target Storage**:
  * Unmounted secondary hard drives and SSDs (`/dev/sdb`, `/dev/nvme1n1`).
  * External USB storage and memory cards.
  * Loopback devices and raw disk images (`.img`, `.raw`).
* **Direct POSIX I/O**:
  * Opens devices using `O_RDWR | O_DIRECT | O_SYNC` to completely bypass Linux page cache and ensure immediate hardware commit.

### Host OS Limitation: Active Root (`/`) Partition
* **The Problem**: Just as in Windows, a running Linux kernel cannot unmount or obliterate its own root filesystem (`/`), `/boot`, or `/home` while active system processes and the kernel itself are executing from those blocks.

---

## 1.3 Format 3: Dedicated Live Boot Environment (Formatting C: & System Drives)

### The Purpose of Format 3
Format 3 is the ultimate enterprise-grade sanitization vehicle. It is explicitly designed to **format and completely obliterate the primary host storage drive (including the Windows `C:\` drive or primary Linux root NVMe)** without OS lock interference.

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                        FORMAT 3: LIVE BOOT ENVIRONMENT ARCHITECTURE                    │
├────────────────────────────────────────────────────────────────────────────────────────┤
│                                                                                        │
│   USB Flash Drive / ISO Boot (UEFI / Legacy BIOS)                                      │
│         │                                                                              │
│         ▼                                                                              │
│   Linux Kernel boots with 'toram' parameter                                            │
│         │                                                                              │
│         ▼                                                                              │
│   Entire OS (Kernel + initramfs + Alpine/Debian base + Electron GUI)                   │
│   is copied into Host RAM (tmpfs)                                                      │
│         │                                                                              │
│         ▼                                                                              │
│   USB Drive can even be safely removed!                                                │
│         │                                                                              │
│         ▼                                                                              │
│   HOST STORAGE DRIVES (NVMe M.2 / SATA SSD / C: Drive) ARE 100% UNMOUNTED & OFFLINE    │
│   ┌────────────────────────────────────────────────────────────────────────────────┐   │
│   │ Exclusive, Unrestricted Raw Hardware Access:                                  │   │
│   │ 1. ATA Secure Erase & Sanitize (Crypto Scramble / Block Erase)                 │   │
│   │ 2. NVMe Format NVM & Sanitize (User Data Erase across all namespaces)          │   │
│   │ 3. 3-Pass DoD 5220.22-M Overwrite on all physical LBA sectors of C: Drive      │   │
│   │ 4. Re-initialization of clean partition tables (GPT/MBR) and fresh filesystems │   │
│   └────────────────────────────────────────────────────────────────────────────────┘   │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

### Technical Implementation Details
1. **Base Distribution**: Minimalist, hardened Linux Live distribution (e.g., customized Alpine Linux or Debian Live x64).
2. **RAM-Disk Execution (`toram` / `copytoram`)**:
   * During the boot phase, the kernel copies the entire squashfs filesystem into RAM (`tmpfs`).
   * The root filesystem (`/`) runs 100% in volatile memory.
   * Not a single byte is read from or written to the host's internal storage during OS operation.
3. **Zero-Lock Guarantee on Windows `C:\`**:
   * The host's internal drives appear purely as raw block devices: `/dev/nvme0n1` (the primary Windows M.2 drive) and `/dev/sda` (internal SATA drive).
   * Because Windows is **not running**, no kernel locks exist, no pagefiles are open, and no volume managers are active.
   * Our engine has direct, exclusive, unrestricted raw sector write access to Sector 0 (MBR/GPT), the VBR of the `C:\` partition, the Master File Table, and every data cluster on the disk.
4. **Hardware Demolition Capabilities in Live Boot**:
   * **NVMe Hardware Sanitize**: Issues raw NVMe Admin commands (`Format NVM` with `SES=1` or `Sanitize` block erase) targeting the primary controller, instantly purging flash blocks across all namespaces including over-provisioned areas.
   * **ATA Secure Erase**: Sends `SECURITY ERASE UNIT` pass-through commands to SATA SSDs/HDDs.
   * **DoD 5220.22-M 3-Pass Wipe**: Overwrites the entire physical LBA span ($0 \dots \text{MaxLBA}$) with `0x00` $\to$ `0xFF` $\to$ PRNG noise.
   * **Pristine Partition Creation**: Writes a brand-new GPT table and clean filesystem so the machine is ready for a fresh OS installation or decommissioning.

---

## 1.4 Comparative Matrix of Shipping Formats

| Feature / Capability | Format 1: Windows App | Format 2: Linux App | Format 3: Live Boot OS |
|---|---|---|---|
| **Target OS** | Windows 10 / 11 | Ubuntu / Debian / RHEL | Independent (Hardware Boot) |
| **Packaging** | Portable `.exe` / `.msi` | AppImage / `.deb` / `.rpm` | Bootable ISO / USB Image |
| **Execution Context** | Running Windows Host | Running Linux Host | RAM-Disk (`toram` tmpfs) |
| **Elevation Required** | Windows UAC (Admin) | Sudo / Polkit (Root) | Built-in Root in RAM |
| **Secondary Drives Erasure** | Full Support | Full Support | Full Support |
| **External USB / SD Erasure**| Full Support | Full Support | Full Support |
| **Active C:\ Boot Drive Erasure** | **Blocked by OS Kernel** | N/A (Linux) | **100% Full Sanitization** |
| **Active Root (/) Drive Erasure** | N/A (Windows) | **Blocked by OS Kernel** | **100% Full Sanitization** |
| **Hardware NVMe Sanitize** | Limited by Win32 StorNVMe | Supported via ioctl | **Direct Kernel Pass-through** |
| **Data Recovery Capabilities**| Secondary / External | Secondary / External | **All Internal + External** |

---

# 2. Application Architecture: Electron GUI + C++ Native Backend

## 2.1 High-Level Architectural Model

The application employs a decoupled client-server architecture inside the desktop runtime:

```
┌──────────────────────────────────────────────────────────────────────────────────────────┐
│                            FRONTEND: ELECTRON RUNTIME                                    │
│  ┌────────────────────────────────────────────────────────────────────────────────────┐  │
│  │ RENDERER PROCESS (HTML5 / CSS3 / TypeScript / React UI)                            │  │
│  │  - Interactive Drive Selector (Model, Serial, Interface, Capacity, S.M.A.R.T.)     │  │
│  │  - Mode Switcher: [Secure Erasure]  vs.  [Data Recovery]                           │  │
│  │  - Algorithm Chooser (DoD 3-Pass, NIST 800-88, NVMe Sanitize, Zero-Fill)           │  │
│  │  - Real-time Sector Heatmap & LBA Position Gauge                                   │  │
│  │  - Forensic Hex Viewer (xxd style) & Shannon Entropy Indicator                     │  │
│  │  - Certificate of Sanitization Export (PDF / Signed JSON)                          │  │
│  └──────────────────────────────────────┬─────────────────────────────────────────────┘  │
│                                         │ ContextBridge (Preload Script)                 │
│  ┌──────────────────────────────────────▼─────────────────────────────────────────────┐  │
│  │ MAIN PROCESS (Node.js Controller)                                                  │  │
│  │  - Window management & native menu integration                                     │  │
│  │  - Privileged backend process spawning & lifecycle supervision                     │  │
│  │  - Bidirectional JSON-RPC IPC router                                               │  │
│  └──────────────────────────────────────┬─────────────────────────────────────────────┘  │
└─────────────────────────────────────────┼────────────────────────────────────────────────┘
                                          │ Bidirectional JSON-RPC (stdin / stdout / Named Pipe)
┌─────────────────────────────────────────▼────────────────────────────────────────────────┐
│                         BACKEND: C++ NATIVE ENGINE                                       │
│  ┌────────────────────────────────────────────────────────────────────────────────────┐  │
│  │ IPC DISPATCHER & JSON-RPC PROTOCOL HANDLER                                         │  │
│  │  - Parses incoming JSON requests & dispatches to worker threads                     │  │
│  │  - Streams high-frequency telemetry events (percent, LBA, MB/s, entropy)           │  │
│  ├──────────────────────────────────┬─────────────────────────────────────────────────┤  │
│  │ SECURE ERASURE SUBSYSTEM         │ DATA RECOVERY SUBSYSTEM                         │  │
│  │  - IStorageDevice (OS Pipe)      │  - IReadOnlyStorage (Safety Interface)          │  │
│  │  - IHardwareController (Demolish)│  - ByteReader (Aligned Block Buffering)         │  │
│  │  - HDDController (3-Pass DoD)    │  - MBRParser & GPTParser (Partition Tables)     │  │
│  │  - NtfsDriver / XfsDriver        │  - FilesystemDetector & Ext4Detector            │  │
│  │  - Ext4Driver / ExFatDriver      │  - NTFS MFTParser & DataRunParser               │  │
│  │  - Verification Engine           │  - File Carver & Deleted File Scanner           │  │
│  └──────────────────────────────────┴─────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 2.2 Separation of Concerns & Privilege Boundaries

1. **Sandboxed UI**:
   * The Electron Renderer process runs with `contextIsolation: true` and `nodeIntegration: false`.
   * The UI has zero direct access to filesystem APIs or raw disk handles, protecting the host system from UI-level vulnerabilities.
2. **Elevated Native Backend**:
   * The C++ backend is compiled as an independent native binary (`ErasureBackend.exe` on Windows, `ErasureBackend` on Linux) or a Node.js C++ native addon (`.node`).
   * When launched by the Electron main process, the backend is spawned with elevated Administrator or root credentials, isolating all low-level hardware I/O inside a dedicated, crash-resilient process.

---

## 2.3 IPC Protocol Specification (JSON-RPC over stdio / Named Pipes)

Communication between the Electron Main process and the C++ Backend is structured as line-delimited JSON-RPC messages.

### 1. Drive Enumeration Request (`device.list`)
* **Electron $\to$ C++**:
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "device.list",
  "params": {}
}
```
* **C++ $\to$ Electron**:
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "devices": [
      {
        "id": "\\\\.\\PhysicalDrive0",
        "model": "Samsung SSD 980 PRO 1TB",
        "serial": "S5GXNF0T123456",
        "interface": "NVMe",
        "capacityBytes": 1000204886016,
        "sectorSize": 512,
        "isSystem": true,
        "partitions": [
          { "index": 1, "mount": "C:", "label": "Windows", "fs": "NTFS", "size": 999200000000 }
        ]
      },
      {
        "id": "\\\\.\\PhysicalDrive1",
        "model": "WDC WD20EZAZ 2TB",
        "serial": "WD-WCC4N123456",
        "interface": "SATA",
        "capacityBytes": 2000398934016,
        "sectorSize": 512,
        "isSystem": false,
        "partitions": [
          { "index": 1, "mount": "D:", "label": "Data", "fs": "NTFS", "size": 2000390000000 }
        ]
      }
    ]
  }
}
```

### 2. Secure Erasure Execution (`erasure.start`)
* **Electron $\to$ C++**:
```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "method": "erasure.start",
  "params": {
    "targetType": "file",
    "devicePath": "\\\\.\\PhysicalDrive1",
    "partitionIndex": 1,
    "relativePath": "/Confidential/Financials.xlsx",
    "algorithm": "DoD_5220_22_M_3PASS",
    "verifyAfter": true
  }
}
```

### 3. Data Recovery Scan (`recovery.scan`)
* **Electron $\to$ C++**:
```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "method": "recovery.scan",
  "params": {
    "devicePath": "\\\\.\\PhysicalDrive1",
    "startSector": 2048,
    "sectorCount": 4194304,
    "filesystem": "NTFS",
    "deepCarve": true
  }
}
```

---

## 2.4 Real-Time Telemetry & Progress Streaming

During long-running erasure or recovery operations, the C++ backend worker thread emits asynchronous telemetry events at 10 Hz (every 100 ms):

### Erasure Progress Notification
```json
{
  "jsonrpc": "2.0",
  "method": "telemetry.erasure_progress",
  "params": {
    "operationId": "op_98234",
    "percent": 68.4,
    "currentPass": 3,
    "totalPasses": 3,
    "passName": "PRNG_GIBBERISH",
    "currentLba": 14285700,
    "totalLba": 20889600,
    "speedMBps": 194.5,
    "etaSeconds": 142,
    "currentEntropy": 7.962
  }
}
```

### Recovery Discovered File Notification
```json
{
  "jsonrpc": "2.0",
  "method": "telemetry.recovery_file_found",
  "params": {
    "fileRecord": {
      "recordNumber": 4012,
      "fileName": "Q3_Revenue_Forecast.pdf",
      "extension": "pdf",
      "sizeBytes": 2458912,
      "creationTime": 1693489200,
      "modificationTime": 1693575600,
      "firstCluster": 89120,
      "health": "Intact",
      "recoverabilityScore": 0.98
    }
  }
}
```

---

# 3. Data Recovery Subsystem Architecture (`Recovery/`)

The **Recovery Subsystem** is an enterprise-grade forensic acquisition and undeletion engine designed to reconstruct and carve lost data from damaged or deleted volumes.

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                               RECOVERY SUBSYSTEM MODULES                               │
├──────────────────────────┬─────────────────────────────────────────────────────────────┤
│ 1. Core Abstractions     │ IReadOnlyStorage, ByteReader, DataRange, FileRecord, Enums  │
│    (Recovery/Core/)      │ Zero-write safety guarantee, aligned buffer reading         │
├──────────────────────────┼─────────────────────────────────────────────────────────────┤
│ 2. Acquisition Layer     │ WindowsReadOnlyStorage (Win32) / LinuxReadOnlyStorage(POSIX)│
│    (Recovery/Acquisition)│ Read-only physical handles, IOCTL geometry extraction       │
├──────────────────────────┼─────────────────────────────────────────────────────────────┤
│ 3. Partition Parsing     │ MBRParser (Legacy MBR & EBR) / GPTParser (Primary & Backup) │
│    (Recovery/Partitions/)│ CRC32 validation, partition GUID translation                │
├──────────────────────────┼─────────────────────────────────────────────────────────────┤
│ 4. Filesystem Detectors  │ FilesystemDetector (Signature heuristic), Ext4Detector      │
│    (Recovery/Filesystems)│ Identifies NTFS, FAT32, exFAT, ext4, XFS partition bounds   │
├──────────────────────────┼─────────────────────────────────────────────────────────────┤
│ 5. Deep NTFS Recovery    │ MFTParser, DataRunParser, NTFSFileSystem                    │
│    (Filesystems/NTFS/)   │ Unallocated record scanning, cluster chain reconstruction   │
└──────────────────────────┴─────────────────────────────────────────────────────────────┘
```

---

## 3.1 Phase 1: Read-Only Storage Foundation (`Recovery/Core/` & `Recovery/Acquisition/`)

### The Absolute Safety Guarantee: `IReadOnlyStorage`
In forensic data recovery, accidental write commands to a damaged or recovering storage volume are catastrophic (can overwrite the very clusters being salvaged).
The recovery module eliminates this possibility through a dedicated, immutable abstraction:

```cpp
class IReadOnlyStorage {
public:
    virtual ~IReadOnlyStorage() = default;
    virtual bool Open(const std::string& path) = 0;
    virtual void Close() = 0;
    virtual bool Read(uint64_t sectorOffset, uint32_t sectorCount, void* buffer) = 0;
    virtual uint64_t GetSize() const = 0;
    virtual uint32_t GetSectorSize() const = 0;
    virtual bool IsOpen() const = 0;
    // CRITICAL: Notice that NO WriteSectors() method exists on this interface!
};
```

### OS Kernel-Level Enforcement
* **Windows (`WindowsReadOnlyStorage.cpp`)**:
  * Calls `CreateFileA` with **`GENERIC_READ` strictly**.
  * Does NOT specify `GENERIC_WRITE`.
  * Even if rogue code attempted to cast the handle and call Win32 `WriteFile`, the Windows NT kernel immediately rejects the request with `ERROR_ACCESS_DENIED`.
* **Linux (`LinuxReadOnlyStorage.cpp`)**:
  * Calls `open(path, O_RDONLY | O_DIRECT)`.
  * Write syscalls are rejected at the VFS layer.

### Sector-Aligned Buffering: `ByteReader`
`ByteReader` wraps `IReadOnlyStorage` to provide arbitrary byte-level reading across sector boundaries:
* `ReadBytes(byteOffset, length, outBuffer)`: Automatically aligns non-aligned byte offsets to physical sector boundaries, reads the enclosing sectors, and slices the requested sub-range.
* `ReadStruct<T>(byteOffset, outStruct)`: Reads and deserializes on-disk packed C++ structures directly from raw byte offsets.

---

## 3.2 Phase 2: Partition Parsing (`Recovery/Partitions/`)

Before files can be recovered, partition tables must be discovered and validated:

```
Physical Disk LBA 0..34
┌────────────────────────┬────────────────────────┬──────────────────────────────────────┐
│ LBA 0: MBR / Prot. MBR │ LBA 1: Primary GPT Hdr │ LBA 2..33: GPT Partition Entries     │
│ 0xAA55 Signature       │ "EFI PART"             │ 128 Bytes per entry x 128 entries    │
│ 4 Partition Entries    │ Header CRC32, Array CRC│ Partition Type GUID, Unique GUID,    │
│                        │ First/Last Usable LBA  │ Starting LBA, Ending LBA, Name       │
└────────────────────────┴────────────────────────┴──────────────────────────────────────┘
```

### 1. Master Boot Record Parser (`MBRParser`)
* Validates `0xAA55` boot signature at offset `0x1FE`.
* Parses four 16-byte primary partition table entries starting at byte offset `0x1BE`.
* Detects Extended Partitions (`Type 0x05` or `0x0F`) and recursively traverses the Extended Boot Record (EBR) chain to discover all logical partitions.

### 2. GUID Partition Table Parser (`GPTParser`)
* Detects Protective MBR at LBA 0 (Type `0xEE`).
* Reads Primary GPT Header at LBA 1 and verifies magic signature `"EFI PART"` (`0x5452415020494645`).
* Computes and verifies **CRC32 Checksums** over the GPT header and the Partition Entry Array.
* Decodes partition type GUIDs to classify partition types:
  * Windows Basic Data: `EBD0A0A2-B9E5-4433-87C0-68B6B72699C7`
  * Linux Filesystem Data: `0FC63DAF-8483-4772-8E79-3D69D8477DE4`
  * EFI System Partition: `C12A7328-F81F-11D2-BA4B-00A0C93EC93B`
* **Redundancy Fallback**: If the Primary GPT header at LBA 1 is corrupted, automatically reads and recovers from the **Backup GPT Header** located at the last LBA of the physical disk ($LBA = \text{TotalSectors} - 1$).

---

## 3.3 Phase 3: Filesystem Detection (`Recovery/Filesystems/`)

The `FilesystemDetector` inspects the leading sectors of identified partitions to establish filesystem identity:

```
Filesystem Signature Identification Table
┌────────────┬────────────────────────┬──────────────────┬───────────────────────────────┐
│ Filesystem │ Search Sector / Offset │ Byte Offset      │ Expected Magic Signature      │
├────────────┼────────────────────────┼──────────────────┼───────────────────────────────┤
│ NTFS       │ Partition LBA + 0      │ Offset 3 (8 B)   │ "NTFS    " (0xAA55 at 0x1FE)  │
│ exFAT      │ Partition LBA + 0      │ Offset 3 (8 B)   │ "EXFAT   " (0xAA55 at 0x1FE)  │
│ FAT32      │ Partition LBA + 0      │ Offset 82 (8 B)  │ "FAT32   " (0xAA55 at 0x1FE)  │
│ ext4       │ Partition LBA + 2      │ Offset 1024 + 56 │ 0xEF53 (s_magic)              │
│ XFS        │ Partition LBA + 0      │ Offset 0 (4 B)   │ "XFSB" (0x58465342)           │
└────────────┴────────────────────────┴──────────────────┴───────────────────────────────┘
```

---

## 3.4 Phase 4: Deep NTFS File Recovery (`Recovery/Filesystems/NTFS/`)

The deep recovery pipeline for NTFS scans the raw Master File Table to resurrect deleted files:

```
[MFT Record Sequence] ──> Read 1024-Byte Record ──> ApplyFixup()
                                │
                                ▼
                   Record Header Flags Inspection
                   flags == 0x0000 (UNALLOCATED / DELETED FILE)
                                │
                                ▼
                   Parse $FILE_NAME Attribute (0x30)
                     - Recover original file name (UTF-16LE -> UTF-8)
                     - Recover parent folder record number
                     - Recover Creation, Access, Modification timestamps
                                │
                                ▼
                   Parse $DATA Attribute (0x80)
                     ┌──────────┴──────────┐
                     ▼                     ▼
          Resident Data Payload     Non-Resident Data Runs
          Directly extract bytes    Decode runlist via DataRunParser
          from MFT record buffer    Reconstruct physical cluster chain
                     │                     │
                     └──────────┬──────────┘
                                │
                                ▼
                   Cluster Integrity & Conflict Analysis
                     - Checks $Bitmap cluster allocation status
                     - If clusters marked free: 100% Intact
                     - If clusters reallocated: Partially Overwritten / Corrupted
                                │
                                ▼
                   Carve & Extract Recovered File to Output Destination
```

### Components of Deep Recovery
1. **`MFTParser`**: Iterates through all MFT records, filtering for inactive records (`flags & 0x0001 == 0`). Identifies deleted files whose metadata has not yet been overwritten by Windows.
2. **`DataRunParser`**: Decodes variable-length NTFS data runs to map the physical clusters of deleted files, allowing fragmented multi-gigabyte files to be fully reconstructed.
3. **`NTFSFileSystem`**: Rebuilds the folder hierarchy by tracing `parentDirectory` record pointers in `$FILE_NAME` attributes back to Root (`Record 5`), restoring files with their original directory paths.

---

# 4. The Adversarial Verification Feedback Loop (Erasure ↔ Recovery)

## 4.1 Architectural Philosophy: Recovery as the Verification Auditor

Most data erasure utilities suffer from a fatal design flaw: **they assume their write commands succeeded without testing whether forensic tools can recover the data**.

Our system solves this by turning the **Recovery Engine** into an **Adversarial Auditor** for the **Erasure Engine**:

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                       THE ADVERSARIAL VERIFICATION FEEDBACK LOOP                       │
├────────────────────────────────────────────────────────────────────────────────────────┤
│                                                                                        │
│   1. ERASURE EXECUTION                                                                 │
│      NtfsDriver::EraseFile("/Confidential/Financials.xlsx")                            │
│        - Cluster extents overwritten with 3-Pass DoD (0x00 -> 0xFF -> PRNG)           │
│        - Cluster bitmap bits cleared                                                   │
│        - 1024-byte on-disk MFT record zeroed                                           │
│        - Directory B-Tree entry scrubbed                                               │
│                                                                                        │
│                              │                                                         │
│                              ▼                                                         │
│                                                                                        │
│   2. INDEPENDENT ADVERSARIAL AUDIT (RECOVERY SUBSYSTEM)                                │
│      Recovery engine mounts volume in strict Read-Only mode                            │
│        - MFTParser scans unallocated MFT records for "Financials.xlsx"                 │
│        - Directory index scanner searches B-Tree blocks for orphaned names             │
│        - Raw Signature Carver scans wiped clusters for XLSX/ZIP magic ("PK\x03\x04")   │
│        - Shannon Entropy Engine measures information density of physical sectors       │
│                                                                                        │
│                              │                                                         │
│                              ▼                                                         │
│                                                                                        │
│   3. MATHEMATICAL VERIFICATION VERDICT                                                 │
│      ┌─────────────────────────────────────────────────────────────────────────────┐   │
│      │ METRIC                         MEASURED VALUE       PASS REQUIREMENT        │   │
│      ├─────────────────────────────────────────────────────────────────────────────┤   │
│      │ Original Payload Bit Match     0.0000 %             < 0.001 %      [PASS]   │   │
│      │ Lingering MFT / Inode Record   0x00000000 (Zeroed)  0 Records      [PASS]   │   │
│      │ Lingering Directory References 0 Found              0 Entries      [PASS]   │   │
│      │ File Magic Signature Matches   0 Found              0 Signatures   [PASS]   │   │
│      │ Target Sector Shannon Entropy  7.954 bits/byte      > 7.5 bits/byte [PASS]  │   │
│      └─────────────────────────────────────────────────────────────────────────────┘   │
│                                                                                        │
│                              │                                                         │
│                              ▼                                                         │
│                                                                                        │
│   4. DIGITALLY SIGNED CERTIFICATE OF SANITIZATION GENERATED                            │
│                                                                                        │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 4.2 End-to-End Audit Sequence

When an erasure operation completes with `--verify` enabled:
1. **Target Boundary Acquisition**: The erasure engine passes the target's physical sector boundaries (LBA range) and metadata record IDs to the verification engine.
2. **Metadata Destruction Verification**:
   * Reads the target MFT record or Inode table slice from disk.
   * Asserts that all bytes are `0x00000000`.
   * Asserts that magic signatures (`FILE`, `IN`, `0xEF53`) are absent.
3. **Directory Unlink Verification**:
   * Reads the parent directory blocks.
   * Traverses directory entries and asserts zero occurrence of the deleted filename.
4. **Adversarial File Carving Scan**:
   * The carver reads all sectors in the target cluster range.
   * Scans for 100+ standard magic signatures (PDF, DOCX, ZIP, PNG, JPEG, ELF, MZ, MP4).
   * Asserts zero magic signature matches.
5. **Shannon Entropy Validation**:
   * Computes the 256-bin entropy across all sanitized sectors.
   * Confirms that entropy $H(X) \ge 7.50\text{ bits/byte}$, proving that original structured user data ($H < 5.0$) has been replaced with high-entropy magnetic saturation PRNG noise.

---

## 4.3 Forensic Certificate of Sanitization

Upon successful verification, the engine exports a standardized audit certificate:
* **Host Information**: Motherboard UUID, Hostname, OS Version, Execution Timestamp.
* **Storage Device Details**: Drive Model, Serial Number, Firmware Revision, Bus Interface (NVMe/SATA/USB), Total LBA Capacity.
* **Sanitization Parameters**: Standard Applied (DoD 5220.22-M 3-Pass), Target Path/Volume, Total Physical Sectors Wiped.
* **Audit Results**: Bit match rate ($0.00\%$), File signatures detected ($0$), Residual metadata found ($0$), Final average Shannon entropy ($7.95\text{ bits/byte}$).
* **Cryptographic Signatures**: SHA-256 pre-erasure hash, SHA-256 post-erasure hash, and digital signature of the certificate payload.

---

# 5. Native Python pybind11 Extension Subsystem (`modules/`)

## 5.1 Architecture & Modular Isolation

In addition to the standalone C++ CLI executables and Electron desktop wrappers, the project provides a comprehensive, high-performance **Python 3 Native Extension Layer** built with `pybind11`. 

Located in `modules/`, this subsystem is specifically designed for:
- Automated hardware integration and regression testing in CI/CD pipelines.
- External security auditor scripting, algorithmic verification, and headless forensic automation.
- Rapid algorithmic prototyping without recompiling the entire application suite.

To maintain architectural purity, the extension layer preserves the project's **3-Layer Decoupled Architecture**: each abstraction layer is compiled into its own isolated static library and exposed as an independent Python C-extension module (`.pyd` on Windows and `.so` on Linux).

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                   PYTHON EXTENSION SUBSYSTEM ARCHITECTURE (`modules/`)                 │
├──────────────────────────┬─────────────────────────────┬───────────────────────────────┤
│ LAYER                    │ C++ STATIC LIBRARY          │ PYTHON MODULE (.pyd / .so)    │
├──────────────────────────┼─────────────────────────────┼───────────────────────────────┤
│ Layer 1: OS Storage I/O  │ osdevice_cpp                │ osdevice                      │
│ Layer 2: Hardware Ctrl   │ hdd_cpp                     │ hdd                           │
│ Layer 3: Filesystems     │ ext4_cpp                    │ ext4                          │
│                          │ exfat_cpp                   │ exfat                         │
│                          │ ntfs_cpp                    │ ntfs                          │
│                          │ fat32_cpp                   │ fat32                         │
│ Forensic Verification    │ verification_cpp            │ verification                  │
└──────────────────────────┴─────────────────────────────┴───────────────────────────────┘
```

---

## 5.2 Supported Python Extension Modules

1. **`osdevice` (`modules/bindings_os.cpp`)**:
   - `WindowsStorageDevice`: Raw device handle lifecycle (`Open`, `Close`), exclusive volume locking (`LockVolume`), unmounting (`DismountVolume`), and disk geometry querying (`GetGeometry`).
   - `LinuxStorageDevice`: POSIX block device direct I/O bindings (`O_RDWR | O_DIRECT | O_SYNC`).
2. **`hdd` (`modules/bindings_hw.cpp`)**:
   - `HDDController`: Magnetic and virtual disk physical overwrite engine implementing DoD 5220.22-M 3-pass sanitization.
3. **`ntfs` (`modules/bindings_ntfs.cpp`)**:
   - `NtfsDriver`: Core driver mounting, MFT traversal, file and recursive directory erasure (`EraseFile`, `EraseDirectory`), surgical volume wipe (`WipeVolume`), and drive re-formatting (`FormatDrive`).
   - `TargetLocations` & `NtfsExtent`: Inspection structures detailing on-disk MFT record indices, cluster runlists, and physical sector offsets for forensic validation.
4. **`fat32` (`modules/bindings_fat32.cpp`)**:
   - `Fat32Driver`: Dual-FAT synchronization, 28-bit cluster chain traversal, SFN/LFN directory entry eradication, volume wiping, and pristine BPB/FSInfo formatting.
5. **`ext4` (`modules/bindings_ext4.cpp`)**:
   - `Ext4Driver`: Superblock inspection, inode extent tree parsing, block group bitmap sanitization, and secure file/volume erasure.
6. **`exfat` (`modules/bindings_exfat.cpp`)**:
   - `ExFatDriver`: VBR parsing, cluster allocation bitmap wiping, directory entry sanitization, and volume wiping.
7. **`verification` (`modules/bindings_verification.cpp`)**:
   - `VerificationEngine`: Multi-filesystem verification orchestrator performing pre-wipe cryptographic baselines, file audit verification, directory audit verification, and NIST SP 800-88 Rev. 1 Stratified Sampling volume audits.
   - `StatisticalTests`: Python-callable endpoints for Shannon entropy calculation, Chi-Square goodness-of-fit, serial correlation, Monte Carlo $\pi$ estimation, SHA-256 computation, and ASCII terminal gauge renderers.
   - `SignatureCarver`: Memory buffer scanner matching 120+ standard file signatures across documents, archives, multimedia, executables, and databases.
   - `AuditReport`: Comprehensive structured audit report with JSON serialization (`ToJson()`) and terminal report generation (`PrintTerminalReport()`).

---

## 5.3 Automated Cross-Platform Toolchains & Testing

The build process is managed via `modules/CMakeLists.txt`:
- **Windows**: Compiles against MSVC 2022 and Python 3.14 (`find_package(Python3 COMPONENTS Interpreter Development REQUIRED)`), generating `.cp314-win_amd64.pyd` binary extensions.
- **Linux**: Compiles against GCC 11+ and Python 3 (`python3 -m pybind11 --cmakedir`), generating `.so` shared libraries.

An automated interactive test runner is provided in `modules/test.py`:
- Detects administrative privileges on Windows and automatically prompts for UAC elevation via `ShellExecuteW("runas", ...)`.
- Provides an interactive console menu to mount any detected partition (exFAT, ext4, NTFS, FAT32), execute surgical file or volume wipes, run filesystem consistency checks, and trigger post-erasure forensic verification.

