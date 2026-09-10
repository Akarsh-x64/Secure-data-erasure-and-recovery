# SanitizeX

**Enterprise- and defense-grade, multi-filesystem data sanitization, forensic recovery, and verification engine.**

## Overview

SanitizeX is a highly modular C++17 engine engineered for low-level, surgical file destruction across diverse storage hardware (HDD, SATA SSD, NVMe) and file systems. It couples deep kernel-bypassing surgical deletion with an immutable read-only forensic recovery subsystem, an adversarial mathematical verification loop, a modular pybind11 extension layer, a Python Flask/Socket.IO backend, and a modern Electron + React 19 desktop GUI.

## Key Features

- **3-Layer Decoupled Architecture**: Separates OS I/O (Win32 & POSIX), Hardware erasure commands (DoD 5220.22-M 3-pass overwrite, NVMe Sanitize/TRIM, ATA Secure Erase), and Filesystem logical operations.
- **Multi-Filesystem Surgical Erasure**: Native support for NTFS, ext4, exFAT, FAT32, and XFS. Capable of pinpoint file deletion, recursive directory eradication, and full volume wiping.
- **Forensic Recovery Engine**: Immutable read-only storage foundation with partition parsers (MBR/GPT), NTFS unallocated MFT reconstruction, TSK bridge, and comprehensive file carving.
- **Adversarial Verification Loop**: Validates data destruction compliance using Shannon Entropy, Chi-Square Goodness-of-Fit, Serial Correlation, Monte Carlo π, and a 120+ signature carver (NIST SP 800-88 audit).
- **Modern Desktop Platform**: Built on an Electron 44 + React 19 UI, communicating with a Python 3 Flask + Socket.IO backend, which dynamically loads native pybind11 C++ extensions.

## Architecture

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                                SANITIZEX DESKTOP PLATFORM                              │
├────────────────────────────────────────────────────────────────────────────────────────┤
│  PRESENTATION & CONTROL LAYER: ELECTRON 44 + REACT 19 + TAILWIND CSS                   │
│                                         ▼                                              │
│  APPLICATION & IPC LAYER: PYTHON FLASK + SOCKET.IO SERVER (`backend/main.py`)          │
│                                         ▼                                              │
│                         pybind11 Native C++ Extensions (`modules/`)                    │
│                                         ▼                                              │
│  ┌──────────────────────────────────────┴───────────────────────────────────────────┐  │
│  │                      CORE C++17 ENGINES & SUBSYSTEMS                             │  │
│  ├──────────────────────────────────────────┬───────────────────────────────────────┤  │
│  │ SURGICAL ERASURE ENGINE (`Erasure/`)     │ DATA RECOVERY SUBSYSTEM (`Recovery/`) │  │
│  ├──────────────────────────────────────────┴───────────────────────────────────────┤  │
│  │ ADVERSARIAL VERIFICATION & AUDIT ENGINE (`Erasure/Verification/`)                 │  │
│  └──────────────────────────────────────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

## Build & Test Instructions

### 1. Core C++ Test Suite (Windows MinGW)
The unified master test suite consolidates all 5 filesystem drivers and the multi-tier forensic verification suite into a single native binary.
```powershell
make windows
# Run the hermetic in-memory self-test (requires zero administrative rights)
.\Tests\main.exe --test
```

### 2. pybind11 Native Extensions (`modules/`)
Compile the native C++ bindings for the Python backend.
```powershell
cd modules
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
# Test the extensions
python test.py
```

### 3. Python Flask Backend (`backend/`)
Starts the local server that acts as the IPC bridge between the GUI and the C++ engine.
```powershell
pip install -r requirements.txt
python backend/main.py
```

### 4. Electron & React Desktop Frontend (`frontend/`)
Launch the user interface.
```powershell
cd frontend
npm install
# Start Vite local dev server and Electron window
npm run dev
```

### 5. Standalone Forensic Recovery Tool (`Recovery/`)
Build the CLI tool for scanning raw sectors and recovering data.
```powershell
cd Recovery
cmake -B build
cmake --build build --config Release
# Deep signature carving across raw sectors
.\build\recover --carve image.dd --out ./recovered_files/
```

## Documentation
For a deep dive into each subsystem, refer to the exhaustive documentation suite inside the `.context/` folder. 
It provides code-level references covering OS I/O pipes, filesystem execution intensive details, the forensic recovery engine, statistical verification methodology, and distribution formats.
