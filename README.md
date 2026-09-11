# <p align="center"><img src="https://raw.githubusercontent.com/tandpfun/skill-icons/main/icons/Shield.svg" width="52" height="52" alt="SanitizeX Logo" /><br/>SanitizeX</p>

<p align="center">
  <strong>Enterprise- & Defense-Grade Multi-Filesystem Data Sanitization, Forensic Recovery & Cryptographic Verification Platform</strong>
</p>

<p align="center">
  <a href="#-compliance--standards"><img src="https://img.shields.io/badge/Compliance-NIST%20SP%20800--88%20Rev.1-007acc?style=for-the-badge&logo=security" alt="NIST SP 800-88" /></a>
  <a href="#-compliance--standards"><img src="https://img.shields.io/badge/Standard-DoD%205220.22--M-critical?style=for-the-badge&logo=lock" alt="DoD 5220.22-M" /></a>
  <a href="#%EF%B8%8F-system-architecture"><img src="https://img.shields.io/badge/Architecture-3--Layer%20Decoupled-success?style=for-the-badge" alt="3-Tier Architecture" /></a>
  <a href="#-key-capabilities"><img src="https://img.shields.io/badge/Filesystems-NTFS%20%7C%20ext4%20%7C%20FAT32%20%7C%20exFAT%20%7C%20XFS-blueviolet?style=for-the-badge" alt="Filesystem Support" /></a>
</p>

---

## ⚡ Tech Stack

<p align="center">
  <img src="https://img.shields.io/badge/C++17-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=white" alt="C++17" />
  <img src="https://img.shields.io/badge/CMake-064F8C?style=for-the-badge&logo=cmake&logoColor=white" alt="CMake" />
  <img src="https://img.shields.io/badge/Python%203.11+-3776AB?style=for-the-badge&logo=python&logoColor=white" alt="Python" />
  <img src="https://img.shields.io/badge/Flask-000000?style=for-the-badge&logo=flask&logoColor=white" alt="Flask" />
  <img src="https://img.shields.io/badge/Socket.io-010101?style=for-the-badge&logo=socketdotio&logoColor=white" alt="Socket.io" />
  <img src="https://img.shields.io/badge/Electron%2044-47848F?style=for-the-badge&logo=electron&logoColor=white" alt="Electron" />
  <img src="https://img.shields.io/badge/React%2019-20232A?style=for-the-badge&logo=react&logoColor=61DAFB" alt="React 19" />
  <img src="https://img.shields.io/badge/TypeScript%205-3178C6?style=for-the-badge&logo=typescript&logoColor=white" alt="TypeScript" />
  <img src="https://img.shields.io/badge/Vite%207-646CFF?style=for-the-badge&logo=vite&logoColor=white" alt="Vite" />
  <img src="https://img.shields.io/badge/Tailwind_CSS-38B2AC?style=for-the-badge&logo=tailwind-css&logoColor=white" alt="Tailwind CSS" />
  <img src="https://img.shields.io/badge/Linux%20POSIX-FCC624?style=for-the-badge&logo=linux&logoColor=black" alt="Linux" />
  <img src="https://img.shields.io/badge/Windows%20Win32-0078D6?style=for-the-badge&logo=windows&logoColor=white" alt="Windows" />
</p>

### Stack Overview

| Layer / Domain | Technologies & Libraries | Responsibility |
| :--- | :--- | :--- |
| **Core Erasure & Recovery** | `C++17`, `CMake` | Low-level surgical deletion drivers, partition parsers, mathematical verification routines, and raw sector file carvers. |
| **Native Interop** | `pybind11` | High-throughput, zero-copy bindings bridging native C++ binaries (`.pyd` / `.so`) with the Python application layer. |
| **IPC & Orchestration** | `Python 3.11+`, `Flask`, `Flask-SocketIO`, `Pydantic` | Async task queues, WebSocket real-time progress broadcast, UAC / sudo privilege checks, and tamper-evident audit logging. |
| **Desktop Shell** | `Electron 44`, `Node.js` | Sandboxed multi-process desktop runtime, OS dialog management, and secure IPC routing via `ContextBridge`. |
| **Client UI** | `React 19`, `TypeScript 5`, `Vite 7`, `Tailwind CSS`, `Lucide Icons` | Mission-control interface with drive selection, selective tree deletion, recovery scanning, and live entropy inspections. |

---

## 🚀 Key Capabilities

- 🎯 **Multi-Filesystem Surgical Erasure**: Pinpoint file and folder eradication natively implemented for **NTFS**, **ext4**, **exFAT**, **FAT32**, and **XFS**. Overwrites targeted metadata (NTFS MFT records, FAT directory entries, ext4 inodes) and target clusters.
- 💽 **Direct-to-Hardware Sanitization**: Raw disk and partition wiping bypassing OS cache buffers via direct Win32 (`CreateFileW` / `DeviceIoControl`) and POSIX direct I/O (`O_DIRECT` / `ioctl`).
- 🔬 **Adversarial Verification Loop**: Mathematical and forensic validation confirming zero residual data:
  - **Shannon Entropy Analysis**: Confirms uniform randomness ($\approx 8.000$ for PRNG random fill, $0.000$ for zero-fill).
  - **Chi-Square ($\chi^2$) Goodness-of-Fit**: Calculates byte uniformity with statistical $p$-values.
  - **Serial Bit Correlation**: Detects linear dependencies across adjacent blocks.
  - **Monte Carlo $\pi$ Approximation**: Evaluates pseudo-random distribution geometry.
  - **Forensic Signature Carver**: Evaluates sectors against 120+ file header/footer signatures to confirm zero discoverable files.
- 🛡️ **Forensic Data Recovery Subsystem**: Read-only forensic analysis engine featuring MBR/GPT partition discovery, NTFS unallocated MFT reconstruction, and deep raw-sector signature carving.
- 📜 **Tamper-Evident Audit Ledger**: Cryptographically sealed audit reports complete with pre- and post-erasure SHA-256 digests, operator identifiers, timestamps, and pass-fail compliance metrics.

---

## 🏛️ System Architecture

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                              SANITIZEX DESKTOP PLATFORM                                │
├────────────────────────────────────────────────────────────────────────────────────────┤
│  PRESENTATION LAYER: Electron 44 + React 19 + TypeScript + Tailwind CSS                │
│  ├─ Drive Erase Tab      ├─ File/Folder Erase Tab                                      │
│  ├─ Recovery Workspace   └─ Cryptographic Audit Logs                                   │
└───────────────────────────────────────────┬────────────────────────────────────────────┘
                                            │ ContextBridge / WebSocket Events
┌───────────────────────────────────────────▼────────────────────────────────────────────┐
│  IPC & ORCHESTRATION LAYER: Python 3 Flask + Socket.IO Server (`src/backend/main.py`)  │
│  ├─ Privileged Escalation Gatekeeper (Windows RunAs / POSIX root)                      │
│  ├─ Asynchronous Operation Workers & Real-Time Progress Emitters                       │
│  └─ Cryptographic Audit Ledger & Verification Report Generator                         │
└───────────────────────────────────────────┬────────────────────────────────────────────┘
                                            │ pybind11 Native C++ Bindings (`src/modules`)
┌───────────────────────────────────────────▼────────────────────────────────────────────┐
│  CORE C++17 ENGINES & SUBSYSTEMS (`src/build_modules/`)                                │
│  ┌─────────────────────────┬─────────────────────────┬───────────────────────────────┐ │
│  │  Surgical Erasure       │  Forensic Recovery      │  Adversarial Verification     │ │
│  │  ├─ NTFS Driver         │  ├─ MBR / GPT Parser    │  ├─ Shannon Entropy (8.00)    │ │
│  │  ├─ ext4 Driver         │  ├─ MFT Reconstruction  │  ├─ Chi-Square & Monte Carlo  │ │
│  │  ├─ exFAT / FAT32       │  ├─ TSK Bridge          │  ├─ Serial Correlation        │ │
│  │  └─ XFS Native Parser   │  └─ Deep 120+ Carver    │  └─ Raw Signature Carver      │ │
│  └─────────────────────────┴─────────────────────────┴───────────────────────────────┘ │
│  ┌───────────────────────────────────────────────────────────────────────────────────┐ │
│  │  HARDWARE & OS ABSTRACTION LAYER                                                  │ │
│  │  ├─ Windows Direct I/O (Win32 CreateFileW / DeviceIoControl)                      │ │
│  │  ├─ Linux POSIX Direct I/O (O_DIRECT / ioctl / blockdev)                          │ │
│  │  └─ Hardware Commands: DoD 5220.22-M, NVMe Sanitize, ATA Secure Erase, TRIM       │ │
│  └───────────────────────────────────────────────────────────────────────────────────┘ │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 🔒 Compliance & Standards

| Standard / Algorithm | Passes / Pattern | Target Application |
| :--- | :--- | :--- |
| **NIST SP 800-88 Rev. 1 Clear** | 1 Pass (Logical Zero-Fill `0x00`) | Standard media sanitized for internal re-use |
| **NIST SP 800-88 Rev. 1 Purge** | Cryptographic PRNG / Firmware Purge | Physical drives cleared for cross-classification transfer |
| **DoD 5220.22-M** | 3 Passes (`0x00` $\rightarrow$ `0xFF` $\rightarrow$ CSPRNG Pseudo-Random) | Defense-grade magnetic and solid-state sanitization |
| **Hardware Secure Erase** | Native ATA Secure Erase / NVMe Sanitize / TRIM | Direct controller-level flash cell eradication |

---

## 📂 Repository Layout

```text
Secure-data-erasure-and-recovery/
├── assets/                  # Project assets, icons, and diagrams
├── docs/                    # Architectural documents & design specifications
├── src/
│   ├── _externals/          # Filesystem check utilities (e2fsck, ntfsfix, fsck.*)
│   ├── backend/             # Python Flask + Socket.IO server & audit ledger
│   │   ├── main.py          # REST & WebSocket API, worker threads, admin checks
│   │   └── audit_logs.json  # Cryptographic log persistence
│   ├── build_modules/       # Native C++ core libraries
│   │   ├── Erasure/         # File systems, OS device wrappers, verification engines
│   │   └── Recovery/        # Forensic disk parsing, carving, recovery tools
│   ├── frontend/            # Electron 44 + React 19 + TypeScript application
│   │   ├── src/main/        # Electron main process & IPC bridges
│   │   ├── src/preload/     # Secure ContextBridge APIs
│   │   └── src/renderer/    # React 19 UI components & Tailwind styles
│   └── modules/             # pybind11 C++ export wrappers & compiled modules (.pyd/.so)
└── README.md
```

---

## 🛠️ Build & Installation Guide

### Prerequisites

- **C++ Compiler**: GCC 10+ / Clang 12+ (Linux) or MSVC v143 / MinGW-w64 (Windows)
- **CMake**: Version 3.15 or newer
- **Python**: Python 3.11+ with development headers
- **Node.js**: Node.js 20+ and `npm`

---

### 1. Build C++ Native Extensions (`src/modules`)

Compile the high-performance C++17 filesystem drivers and verification routines into Python bindings:

```bash
cd src/modules

# On Linux
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# On Windows (Visual Studio 2022 x64)
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Run the extension test harness:
```bash
python test.py
```

---

### 2. Configure Python Backend (`src/backend`)

Install Python dependencies:

```bash
# Recommended: create and activate a virtual environment
python -m venv venv
source venv/bin/activate       # On Windows: .\venv\Scripts\activate

pip install flask flask-cors flask-socketio pydantic pybind11
```

Run the backend server (requires elevated administrator/root rights for low-level drive access):

```bash
# Linux (root required for raw block access)
sudo python src/backend/main.py

# Windows (auto-prompts UAC elevation if not already elevated)
python src/backend/main.py
```

---

### 3. Launch Desktop GUI (`src/frontend`)

Install dependencies and start the Electron application with Hot Module Replacement (HMR):

```bash
cd src/frontend

# Install dependencies
npm install

# Start Vite local development server and launch Electron
npm run dev
```

To build a production standalone installer:

```bash
# Package for host OS
npm run build:linux   # Linux AppImage / deb
npm run build:win     # Windows NSIS Installer / portable .exe
```

---

## 🧪 Forensic Verification & Self-Test

SanitizeX includes an automated mathematical verification suite to validate that data is irrecoverable:

```bash
cd src/modules
python -c "import verification; print('Verification Engine Loaded Successfully')"
```

The verification engine evaluates:
1. **Entropy Metric**: Computes information density per block ($H(X) = -\sum P(x) \log_2 P(x)$).
2. **Chi-Square Analysis**: Checks whether byte distributions exhibit uniform statistical randomness.
3. **Known Signature Scan**: Scans sectors against 120+ standard forensic signatures (`PDF`, `PNG`, `JPEG`, `ZIP`, `ELF`, `PE`, `DOCX`, etc.) to confirm zero discoverable files.

---

## ⚖️ License & Ethical Usage

This software is developed for authorized system administrators, security auditors, and forensic technicians for legitimate data decommissioning and recovery operations. Unauthorized destruction of data on computer systems without explicit owner permission is strictly prohibited.
