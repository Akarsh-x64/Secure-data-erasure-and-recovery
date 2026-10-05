# Build, Compilation & Verification Guide

## 1. System Requirements & Toolchains

### Windows Environment
* **C++ Compilers**:
  - MinGW-w64 GCC 14+ (`g++`) with C++17 support (for standalone test runners).
  - Microsoft Visual Studio 2022 (MSVC v143+) with Desktop C++ and CMake components (for pybind11 `.pyd` compilation).
* **Python Runtime**: Python 3.10 – 3.14 (64-bit) with `pip` and header files (`Python.h`).
* **Node.js**: Node.js v20+ / v22+ with `npm 10+`.
* **Privileges**: Administrative terminal prompt (`Run as Administrator`) for physical drive operations.

### Linux Environment
* **Compilers**: GCC 11+ / Clang 13+ with `libpthread`.
* **Build Systems**: CMake 3.15+ and GNU Make.
* **Headers**: `python3-dev`, `libtsk-dev` (optional for The Sleuth Kit).
* **Privileges**: `sudo` access or `root` permissions.

---

## 2. Core C++ Test Suite (`Tests/main.cpp`)

The unified master test suite consolidates all 5 filesystem drivers (NTFS, XFS, ext4, exFAT, FAT32) and the multi-tier forensic verification suite into a single native binary.

### 2.1 Compilation via Makefile
```powershell
# Windows (MinGW-w64 GCC)
make windows

# Linux (GCC / POSIX)
make linux
```

### 2.2 Manual Compilation Command (Windows MinGW)
```powershell
g++ -std=c++17 -O2 -Wall -Wextra -pthread `
  Tests/main.cpp `
  Erasure/OS/Windows/WindowsStorageDevice.cpp `
  Erasure/Hardware/Magnetic/HDDController.cpp `
  "Erasure/File Systems/NTFS/NTFS.cpp" `
  "Erasure/File Systems/XFS/XFS.cpp" `
  "Erasure/File Systems/ext4/ext4.cpp" `
  "Erasure/File Systems/exFAT/exFAT.cpp" `
  "Erasure/File Systems/FAT32/FAT32.cpp" `
  Erasure/Verification/StatisticalTests.cpp `
  Erasure/Verification/SignatureCarver.cpp `
  Erasure/Verification/VerificationReport.cpp `
  Erasure/Verification/VerificationEngine.cpp `
  -o Tests/main.exe
```

### 2.3 Running the Hermetic In-Memory Self-Test
The self-test runs entirely in RAM using `MemoryDiskDevice`. It requires **zero administrative rights** and touches **zero physical drives**:
```powershell
.\Tests\main.exe --test
```
* **Verifies**:
  - [1/6] NTFS Surgical Erasure, MFT Zeroing, and Runlist Decoding
  - [2/6] XFS Allocation Groups and B+Tree Erasure
  - [3/6] ext4 Inode and 48-Bit Extent Tree Sanitization
  - [4/6] exFAT Spec §6.2.1.1 Deletion and Checksum Verification
  - [5/6] FAT32 SFN/LFN Obliteration and Dual-FAT Sync
  - [6/6] Multi-Tier Verification (Shannon Entropy, Chi-Square, 120+ Signature Carver)

### 2.4 Running the Interactive Live Session
To sanitize or format an actual physical drive or secondary volume:
```powershell
# Launch as Administrator!
.\Tests\main.exe
```
Prompts for physical device path (e.g. `\\.\PhysicalDrive1` or `\\.\E:`), filesystem type, and target action (`EraseFile`, `EraseDirectory`, `WipeVolume`, `FormatDrive`).

---

## 3. pybind11 Native Extension Subsystem (`modules/`)

### 3.1 Compiling with Visual Studio 2022 (Windows)
```powershell
cd modules
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```
This generates the 7 compiled `.pyd` binary extensions:
* `osdevice.cp314-win_amd64.pyd`
* `hdd.cp314-win_amd64.pyd`
* `ntfs.cp314-win_amd64.pyd`
* `ext4.cp314-win_amd64.pyd`
* `exfat.cp314-win_amd64.pyd`
* `fat32.cp314-win_amd64.pyd`
* `verification.cp314-win_amd64.pyd`

### 3.2 Testing Extensions via Python Test Harness
```powershell
python modules/test.py
```
* Automatically detects if elevation is missing and requests a Windows UAC prompt.
* Provides an interactive CLI to test each pybind11 module against live storage devices.

---

## 4. Python Flask Backend Server (`backend/main.py`)

### 4.1 Install Dependencies
```powershell
pip install -r requirements.txt
```

### 4.2 Start Server
```powershell
python backend/main.py
```
* Verifies administrative privileges on startup.
* Spawns Flask-SocketIO server on `http://127.0.0.1:5000`.
* Serves REST endpoints (`/api/v1/devices`, `/api/v1/erase/files`, `/api/v1/erase/drives`) and WebSocket progress events.

---

## 5. Electron & React Desktop Frontend (`frontend/`)

### 5.1 Install Node Dependencies
```powershell
cd frontend
npm install
```

### 5.2 Start Development App (HMR + Desktop Window)
```powershell
npm run dev
```
* Boots Vite local dev server (`http://localhost:5173`).
* Launches the frameless Electron desktop window on your screen.

### 5.3 Type-Checking & Linting
```powershell
npm run typecheck
npm run lint
```

### 5.4 Build Windows Production Executable (`.exe`)
```powershell
npm run build:win
```
* Compiles optimized React production bundle via Vite.
* Packages native desktop executable and installer into `frontend/dist/`.

---

## 6. Standalone Forensic Recovery Tool (`Recovery/recover.cpp`)

### 6.1 Build with CMake
```powershell
cd Recovery
cmake -B build
cmake --build build --config Release
```

### 6.2 CLI Recovery Usage
```powershell
# Scan unallocated NTFS MFT records
.\build\recover --metadata image.dd

# Deep signature carving across raw sectors
.\build\recover --carve image.dd --out ./recovered_files/
```
