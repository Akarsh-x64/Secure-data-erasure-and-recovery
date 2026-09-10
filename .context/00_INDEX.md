# SanitizeX Context Index & Architecture Map

## 1. Executive Summary

**SanitizeX** is an enterprise- and defense-grade, multi-filesystem data sanitization, forensic recovery, and verification engine engineered in C++17. It couples deep kernel-bypassing surgical deletion with an immutable read-only forensic recovery subsystem, an adversarial mathematical verification loop, a modular pybind11 extension layer, a Python Flask/Socket.IO backend, and a modern Electron + React 19 desktop GUI.

This documentation suite in `.context/` provides an exhaustive, code-level reference for every subsystem in the repository.

---

## 2. Documentation Map & Navigation

| Document | Focus & Subsystems Covered | Key Technical References |
|---|---|---|
| [01_SYSTEM_ARCHITECTURE.md](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/.context/01_SYSTEM_ARCHITECTURE.md) | Architectural philosophy, 3-Layer Decoupled Design, safety guidelines, and security/elevation model. | `IStorageDevice`, `IHardwareController`, `IFileSystemDriver`, RAII handles, UAC elevation. |
| [02_STORAGE_AND_HARDWARE.md](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/.context/02_STORAGE_AND_HARDWARE.md) | OS I/O pipes (Win32 & POSIX), DoD 5220.22-M 3-pass overwrite, NVMe Sanitize/TRIM, and ATA Secure Erase. | `WindowsStorageDevice`, `LinuxStorageDevice`, `HDDController`, `NVMeController`, `ATAController`. |
| [03_FILESYSTEMS_ERASURE_ENGINE.md](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/.context/03_FILESYSTEMS_ERASURE_ENGINE.md) | Surgical deletion across all 5+ filesystems: NTFS, ext4, exFAT, FAT32, XFS, ext2/ext3. On-disk layouts, extent decoding, and directory unlinking. | `NtfsDriver`, `Ext4Driver`, `ExFatDriver`, `Fat32Driver`, `XfsDriver`. |
| [04_FORENSIC_RECOVERY_ENGINE.md](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/.context/04_FORENSIC_RECOVERY_ENGINE.md) | Read-only forensic foundation, partition parsers, deep unallocated MFT reconstruction, TSK bridge, and file carvers. | `IReadOnlyStorage`, `ByteReader`, `MBRParser`, `GPTParser`, `MFTParser`, `TskImageBridge`, PhotoRec. |
| [05_VERIFICATION_AND_AUDIT.md](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/.context/05_VERIFICATION_AND_AUDIT.md) | Adversarial verification loop: Shannon entropy, Chi-Square, Serial Correlation, Monte Carlo $\pi$, 120+ signature carver, and NIST SP 800-88 audit. | `StatisticalTests`, `SignatureCarver`, `VerificationEngine`, `VerificationReport`. |
| [06_PYBIND11_NATIVE_MODULES.md](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/.context/06_PYBIND11_NATIVE_MODULES.md) | Modular Python 3 C++ extensions (`.pyd`/`.so`), CMake dual-toolchain build system, and UAC elevation runner. | `osdevice`, `hdd`, `ntfs`, `ext4`, `exfat`, `fat32`, `verification`, `modules/test.py`. |
| [07_BACKEND_AND_IPC.md](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/.context/07_BACKEND_AND_IPC.md) | Python Flask + Socket.IO server, real-time telemetry streaming, REST contracts, background workers, and filesystem repair tools. | `backend/main.py`, `ProgressEvent`, `chkdsk`, `e2fsck`, `fsck.exfat`, `ntfsfix`. |
| [08_FRONTEND_ELECTRON_REACT.md](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/.context/08_FRONTEND_ELECTRON_REACT.md) | Electron 44 desktop runtime, preload ContextBridge, React 19 UI, Tailwind design system, FileSystemTree, and tabs. | `src/main/index.ts`, `src/preload/index.ts`, `FileEraseTab`, `DriveEraseTab`, `RecoveryTab`, `AuditLogsTab`. |
| [09_SHIPPING_AND_LIVEBOOT.md](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/.context/09_SHIPPING_AND_LIVEBOOT.md) | The 3 distribution formats: Windows Portable/MSI, Linux AppImage/DEB, and custom RAM-Disk LiveBoot OS to wipe host `C:\` drives. | `electron-builder`, `toram` tmpfs, bare-metal NVMe Sanitize on active system disks. |
| [10_BUILD_AND_TEST_GUIDE.md](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/.context/10_BUILD_AND_TEST_GUIDE.md) | End-to-end compilation, build toolchains (MinGW GCC, MSVC, CMake, npm), automated synthetic tests, and interactive demos. | `make windows`, `make linux`, `modules/CMakeLists.txt`, `npm run dev`, `Tests/main.exe`. |

---

## 3. High-Level Architectural Diagram

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                                SANITIZEX DESKTOP PLATFORM                              │
├────────────────────────────────────────────────────────────────────────────────────────┤
│  PRESENTATION & CONTROL LAYER: ELECTRON 44 + REACT 19 + TAILWIND CSS                   │
│  ┌─────────────────────────┬──────────────────────────┬──────────────────────────────┐ │
│  │ File & Folder Erasure   │ Whole Drive Sanitization │ Deep Forensic Recovery       │ │
│  │ (Interactive FileTree)  │ (DoD 3-Pass / NIST SP)   │ (MFT Search / PhotoRec)      │ │
│  └─────────────────────────┴──────────────────────────┴──────────────────────────────┘ │
│                                         │                                              │
│                     Electron ContextBridge (IPC / HTTP / WebSocket)                    │
│                                         ▼                                              │
│  APPLICATION & IPC LAYER: PYTHON FLASK + SOCKET.IO SERVER (`backend/main.py`)          │
│  - Elevation Supervisor (`ensure_admin`)    - Background Job Workers                   │
│  - Real-time Progress & Telemetry Emitter   - Automatic OS Filesystem Repair (`chkdsk`)│
│                                         │                                              │
│                         pybind11 Native C++ Extensions (`modules/`)                    │
│   (osdevice.pyd, hdd.pyd, ntfs.pyd, ext4.pyd, exfat.pyd, fat32.pyd, verification.pyd)  │
│                                         │                                              │
│  ┌──────────────────────────────────────┴───────────────────────────────────────────┐  │
│  │                      CORE C++17 ENGINES & SUBSYSTEMS                             │  │
│  ├──────────────────────────────────────────┬───────────────────────────────────────┤  │
│  │ SURGICAL ERASURE ENGINE (`Erasure/`)     │ DATA RECOVERY SUBSYSTEM (`Recovery/`) │  │
│  │ • 3-Layer Decoupled Architecture         │ • Immutable IReadOnlyStorage Kernel   │  │
│  │ • Layer 1: OS Pipes (Win32 & POSIX)      │ • Arbitrary ByteReader Alignment      │  │
│  │ • Layer 2: DoD 3-Pass Overwriter         │ • MBR & GPT Partition Parsers         │  │
│  │ • Layer 3: Filesystem Drivers:           │ • NTFS Unallocated MFT Parser         │  │
│  │   NTFS, ext4, exFAT, FAT32, XFS, ext2/3  │ • TSK Image Bridge & PhotoRec Carver  │  │
│  │                                          │ • Artifact Verification Suite         │  │
│  ├──────────────────────────────────────────┴───────────────────────────────────────┤  │
│  │ ADVERSARIAL VERIFICATION & AUDIT ENGINE (`Erasure/Verification/`)                 │  │
│  │ • Shannon Entropy ($H \to 8.0$)          • Chi-Square ($\chi^2$) Goodness-of-Fit │  │
│  │ • Serial Correlation Coefficient         • Monte Carlo $\pi$ Uniformity          │  │
│  │ • 120+ Adversarial Signature Carver      • NIST SP 800-88 Stratified Sampling    │  │
│  └──────────────────────────────────────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 4. Technology Stack Matrix

| Subsystem | Languages / Toolchains | Primary Libraries & Standards |
|---|---|---|
| **Core Erasure** | C++17 (MSVC / MinGW GCC) | Win32 API (`CreateFileW`, `IOCTL`), POSIX Direct I/O (`O_DIRECT`), DoD 5220.22-M |
| **Forensic Recovery** | C++17, CMake | The Sleuth Kit (TSK), PhotoRec / TestDisk, Brian Carrier / Simson Garfinkel models |
| **Verification Engine** | C++17, Zero-Dependency SHA-256 | Shannon Entropy, Chi-Square Distribution, NIST SP 800-88 Rev. 1 Stratified Sampling |
| **Native Bindings** | C++17, pybind11 | Python 3.10-3.14 C-API, CMake Position-Independent Code (`-fPIC`) |
| **Backend & IPC** | Python 3 | Flask, Flask-CORS, Flask-SocketIO, Pydantic v2, Windows ctypes |
| **Frontend UI** | TypeScript, React 19, HTML5/CSS3 | Electron 44, electron-vite, Vite 7, Tailwind CSS 3.4, Lucide React Icons |
| **LiveBoot OS** | Linux Kernel, Alpine / Debian Base | RAM-disk execution (`toram` tmpfs), bare-metal NVMe Sanitize & ATA Secure Erase |
