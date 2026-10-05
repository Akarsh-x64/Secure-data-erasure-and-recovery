# Python pybind11 Native Extension Subsystem

## 1. Overview & Architectural Isolation

To enable high-speed automated integration testing, cross-platform Python scripting, and seamless integration with the Python Flask backend, SanitizeX implements a modular **Python 3 / pybind11 Native Extension Subsystem** located in [modules/](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/modules).

Rather than compiling a single monolithic, brittle C++ wrapper, the subsystem mirrors the core project's **3-Layer Decoupled Architecture**:
1. First, the underlying C++ source files are compiled into isolated static libraries with Position Independent Code (`-fPIC` / `CMAKE_POSITION_INDEPENDENT_CODE ON`).
2. Then, pybind11 bridges these static libraries into 7 fine-grained, standalone binary extension modules:
   - Windows: `.cp314-win_amd64.pyd`
   - Linux: `.cpython-314-x86_64-linux-gnu.so`

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                   PYTHON PYBIND11 EXTENSION ARCHITECTURE (`modules/`)                  │
├──────────────────────────┬─────────────────────────────┬───────────────────────────────┤
│ LAYER                    │ C++ STATIC LIBRARY          │ PYBIND11 MODULE (.pyd / .so)  │
├──────────────────────────┼─────────────────────────────┼───────────────────────────────┤
│ Layer 1: OS Storage I/O  │ osdevice_cpp                │ osdevice                      │
│                          │ (WindowsStorageDevice.cpp / │ (WindowsStorageDevice /       │
│                          │  LinuxStorageDevice.cpp)    │  LinuxStorageDevice)          │
├──────────────────────────┼─────────────────────────────┼───────────────────────────────┤
│ Layer 2: Hardware Ctrl   │ hdd_cpp (HDDController.cpp) │ hdd (HDDController)           │
├──────────────────────────┼─────────────────────────────┼───────────────────────────────┤
│ Layer 3: Filesystems     │ ext4_cpp (ext4.cpp)         │ ext4 (Ext4Driver)             │
│                          │ exfat_cpp (exFAT.cpp)       │ exfat (ExFatDriver)           │
│                          │ ntfs_cpp (NTFS.cpp)         │ ntfs (NtfsDriver)             │
│                          │ fat32_cpp (FAT32.cpp)       │ fat32 (Fat32Driver)           │
├──────────────────────────┼─────────────────────────────┼───────────────────────────────┤
│ Forensic Verification    │ verification_cpp            │ verification                  │
│                          │ (VerificationEngine.cpp,    │ (VerificationEngine,          │
│                          │  StatisticalTests.cpp,      │  StatisticalTests,            │
│                          │  SignatureCarver.cpp,       │  SignatureCarver,             │
│                          │  VerificationReport.cpp)    │  AuditReport)                 │
└──────────────────────────┴─────────────────────────────┴───────────────────────────────┘
```

---

## 2. Module Specifications

### 2.1 `osdevice`
* **Source Binding**: [modules/bindings_os.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/modules/bindings_os.cpp)
* **Classes Exposed**:
  - Windows: `osdevice.WindowsStorageDevice`
  - Linux: `osdevice.LinuxStorageDevice`
* **Methods**:
  - `Open(path: str) -> bool`
  - `Close() -> None`
  - `LockVolume() -> bool`
  - `DismountVolume() -> bool`
  - `UnlockVolume() -> bool`
  - `GetGeometry() -> DeviceGeometry` (bytesPerSector, totalSectors, totalBytes)
  - `IsOpen() -> bool`

### 2.2 `hdd`
* **Source Binding**: [modules/bindings_hw.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/modules/bindings_hw.cpp)
* **Classes Exposed**: `hdd.HDDController`
* **Constructor**: Takes an `IStorageDevice` instance (e.g. `hdd.HDDController(device)`).
* **Methods**:
  - `SecureEraseSectors(startSector: int, sectorCount: int) -> bool` (executes 3-pass DoD overwrite)
  - `SecureEraseDrive() -> bool`
  - `GetGeometry() -> DeviceGeometry`

### 2.3 `ntfs`
* **Source Binding**: [modules/bindings_ntfs.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/modules/bindings_ntfs.cpp)
* **Classes Exposed**: `ntfs.NtfsDriver`, `ntfs.TargetLocations`, `ntfs.NtfsExtent`
* **Methods**:
  - `Mount() -> bool`
  - `PrintBootInfo() -> None`
  - `EraseFile(relativePath: str) -> bool`
  - `EraseDirectory(relativePath: str) -> bool`
  - `WipeVolume() -> bool`
  - `FormatDrive(fullDriveSanitize: bool) -> bool`
  - `VerifyAndErase(targetPath: str) -> bool` (executes before-and-after `xxd` hex dump verification)
  - `VerifyAndFormatDrive(fullDriveSanitize: bool) -> bool`
  - `LocateTargetLocations(relativePath: str) -> tuple[bool, TargetLocations]`
  - Geometry Getters: `GetBytesPerSector()`, `GetBytesPerCluster()`, `GetMftRecordSize()`, `ClusterToSector(lcn)`.

### 2.4 `ext4`
* **Source Binding**: [modules/bindings_ext4.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/modules/bindings_ext4.cpp)
* **Classes Exposed**: `ext4.Ext4Driver`
* **Methods**: `Mount()`, `PrintSuperblockInfo()`, `EraseFile(relativePath)`, `WipeVolume()`.

### 2.5 `exfat`
* **Source Binding**: [modules/bindings_exfat.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/modules/bindings_exfat.cpp)
* **Classes Exposed**: `exfat.ExFatDriver`
* **Methods**: `Mount()`, `PrintVBRInfo()`, `EraseFile(relativePath)`, `WipeVolume()`.

### 2.6 `fat32`
* **Source Binding**: [modules/bindings_fat32.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/modules/bindings_fat32.cpp)
* **Classes Exposed**: `fat32.Fat32Driver`
* **Methods**: `Mount()`, `PrintBootInfo()`, `EraseFile(relativePath)`, `EraseDirectory(relativePath)`, `WipeVolume()`, `FormatDrive(fullDriveSanitize)`.

### 2.7 `verification`
* **Source Binding**: [modules/bindings_verification.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/modules/bindings_verification.cpp)
* **Classes & Structs Exposed**:
  - `VerificationScope` enum (`FILE_ERASURE`, `DIRECTORY_ERASURE`, `VOLUME_WIPE`)
  - `SignatureCategory` enum (`DOCUMENT`, `IMAGE`, `ARCHIVE`, `EXECUTABLE`, `AUDIO_VIDEO`, `DATABASE_SYSTEM`)
  - `CarvedArtifact` (signatureName, extension, category, byteOffset, lba)
  - `StatisticalAuditResult` (shannonEntropy, chiSquareValue, chiSquarePValue, serialCorrelation, monteCarloPi, monteCarloPiErrorPercent, byteHistogram)
  - `AuditReport` (`ToJson()`, `PrintTerminalReport()`, `passed`)
  - `StatisticalTests`: Python buffer-callable functions:
    - `CalculateShannonEntropy(bytes) -> float`
    - `CalculateChiSquare(bytes) -> tuple[float, float]`
    - `CalculateSerialCorrelation(bytes) -> float`
    - `EstimateMonteCarloPi(bytes) -> tuple[float, float]`
    - `ComputeSha256(bytes) -> str`
    - `RunFullAudit(bytes) -> StatisticalAuditResult`
  - `SignatureCarver`: `GetSignatureCount()`, `ScanBuffer(bytes, baseOffset, sectorSize)`
  - `VerificationEngine`: Multi-filesystem orchestrator:
    - `CapturePreWipeDigest(startSector, sectorCount)`
    - `AuditFileErasure(startSector, sectorCount, fileSize, relativePath, preWipeSha256)`
    - `AuditDirectoryErasure(path, deletedFiles)`
    - `AuditVolumeWipe(wipePatternByte, expectedEntropy)` (NIST SP 800-88 stratified audit)

---

## 3. Build System Architecture (`modules/CMakeLists.txt`)

The build configuration is defined in [modules/CMakeLists.txt](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/modules/CMakeLists.txt).

### Dual-Platform Support
* **Windows Toolchain (MSVC)**:
  - Invokes `find_package(Python3 COMPONENTS Interpreter Development REQUIRED)`.
  - Links against `python314.lib` (or current installed Python runtime).
  - Emits native Windows `.pyd` dynamic libraries.
* **Linux Toolchain (GCC/Clang)**:
  - Links against `libpython3.so`.
  - Emits standard `.so` Linux shared objects.

### Compilation Sequence
```cmake
# 1. Compile C++ sources into isolated static libraries
add_library(osdevice_cpp STATIC ${OS_SOURCES})
add_library(hdd_cpp STATIC ${HDD_SOURCES})
add_library(ext4_cpp STATIC ${EXT4_SOURCES})
add_library(exfat_cpp STATIC ${EXFAT_SOURCES})
add_library(ntfs_cpp STATIC ${NTFS_SOURCES})
add_library(fat32_cpp STATIC ${FAT32_SOURCES})
add_library(verification_cpp STATIC ${VERIFICATION_SOURCES})

# 2. Build pybind11 modules with private link dependencies
pybind11_add_module(osdevice bindings_os.cpp)
target_link_libraries(osdevice PRIVATE osdevice_cpp)

pybind11_add_module(hdd bindings_hw.cpp)
target_link_libraries(hdd PRIVATE hdd_cpp osdevice_cpp)
```

---

## 4. Test Harness & UAC Auto-Elevation (`modules/test.py`)

* **File**: [modules/test.py](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/modules/test.py)

Because raw physical drive access requires Administrator privileges on Windows, `test.py` embeds an automatic UAC elevation supervisor:
```python
def check_admin():
    try:
        return ctypes.windll.shell32.IsUserAnAdmin()
    except:
        return False

if not check_admin():
    ctypes.windll.shell32.ShellExecuteW(None, "runas", sys.executable, " ".join(sys.argv), None, 1)
    sys.exit(0)
```
Once elevated, `test.py` offers an interactive CLI menu to select target filesystems (`exFAT`, `ext4`, `NTFS`, `FAT32`), specify mounted drive letters (e.g. `\\.\E:`), and trigger verified file erasures or volume wipes directly through the native pybind11 modules.
