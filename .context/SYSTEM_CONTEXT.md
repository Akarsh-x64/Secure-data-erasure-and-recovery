# Multi-Filesystem Secure Deletion Engine - Architecture & System Context

## System Architecture & Philosophy
* **Goal:** A modular C++ engine for low-level, surgical file destruction across diverse storage hardware (HDD, SATA SSD, NVMe) and file systems (exFAT, NTFS, FAT32, etc.).
* **Decoupled Design:** Separates Operating System I/O (`IStorageDevice`), specific Hardware erasure commands (`IHardwareController`), and logical file system operations (`IFileSystemDriver`).

## Core Abstraction Layers (The 3-Layer Architecture)

### 1. `IStorageDevice` (OS Layer / The Pipe)
* **Responsibility:** Manages the low-level connection to the device via the host operating system.
* **Implementation:** `WindowsStorageDevice` (Win32) or `LinuxStorageDevice` (POSIX).
* **Role:** Acts as a "dumb pipe". It exposes basic `ReadSectors`, `WriteSectors`, and `SendDeviceCommand` (for pass-through). It handles OS-specific locks (e.g., `FSCTL_LOCK_VOLUME`).

### 2. `IHardwareController` (Hardware Layer / Demolition)
* **Responsibility:** Generates the specific byte-payloads needed to securely erase data based on the physical drive type.
* **Implementation:** `NVMeController` (TRIM / Format NVM), `ATAController` (ATA Secure Erase), `HDDController` (DoD 3-pass).
* **Virtual Disks:** VHDs and VeraCrypt volumes are abstracted by the OS and reject hardware pass-through commands. They fallback to using the `HDDController` (standard overwriting).
* **The Metadata Limitation:** Because `TRIM` operates on massive flash blocks (1MB+), small metadata (like 32-byte directory entries) cannot be TRIMmed without destroying surrounding files. Therefore, metadata MUST be overwritten using standard `WriteSectors`, accepting the risk that SSD wear-leveling firmware may leave ghost copies on hidden flash chips until Garbage Collection runs.

### 3. `IFileSystemDriver` (File System Layer / Detective)
* **Responsibility:** Parses the raw sectors to understand the logical file system layout.
* **Role:** When asked to `DeleteFile(path)`, it locates the exact physical sectors of the file, commands the `IHardwareController` to zap those specific sectors, and finally overwrites the metadata (directory entries, allocation bitmaps).
* **Implementation Details:** Operates purely on abstractions. Drivers like `ExFatDriver` handle VBR parsing, FAT chains, and directory traversal privately.

## Engine Safety & Memory Guidelines
* **Zero Hardcoded Offsets:** Never assume table layouts or cluster sizes. Drivers must compute offsets dynamically from the volume boot record or superblock.
* **Strict Struct Packing:** Wrap all on-disk data structures in `#pragma pack(push, 1)` to prevent compiler padding errors during raw disk reads.
* **RAII Encapsulation:** Wrap Win32 `HANDLE` types in dedicated RAII wrappers (or classes) to guarantee resource cleanup (and `FSCTL_UNLOCK_VOLUME`) on completion or failure.
* **Privilege Enforcement:** Require Administrator rights via application manifest (`requireAdministrator`) to permit raw volume locking and low-level I/O.


2) Recovery commit: 
# Phase 1 — Read-Only Storage Foundation: Walkthrough

## Existing Architecture (Erasure Side)

The Erasure module's storage stack works like this:

```
IStorageDevice (abstract)
    ↓ implements
WindowsStorageDevice
    ↓ uses
CreateFileA(path, GENERIC_READ | GENERIC_WRITE, ...)
    ↓
SetFilePointerEx + ReadFile / WriteFile
```

`IStorageDevice` exposes **10 methods** including `WriteSectors`, `LockVolume`, `DismountVolume`, `SendDeviceCommand` — all destructive or intrusive operations that Recovery must never call.

## How the Recovery Abstraction Fits

The Recovery module creates a **completely separate interface hierarchy** in its own namespace:

```
IReadOnlyStorage (abstract, Recovery::Core)
    ↓ implements
WindowsReadOnlyStorage (Recovery::Acquisition)
    ↓ uses
CreateFileA(path, GENERIC_READ, ...)   ← NO GENERIC_WRITE
    ↓
SetFilePointerEx + ReadFile only
```

```
ByteReader (Recovery::Core)
    ↓ wraps
IReadOnlyStorage
    ↓ provides
Arbitrary byte-offset reads with automatic sector alignment
```

The safety guarantee is **two-layered**:
1. **API level**: `IReadOnlyStorage` has no write method — code that takes this interface literally cannot call write
2. **OS kernel level**: The Win32 handle is opened with `GENERIC_READ` only — even if someone cast to HANDLE and called `WriteFile`, Windows would reject it

## Files Created

| File | Lines | Purpose |
|---|---|---|
| [IReadOnlyStorage.h](file:///home/ishaan/dev/sihv3/Secure-data-erasure-and-recovery/Recovery/Core/IReadOnlyStorage.h) | 42 | Abstract interface: `Open`, `Close`, `Read`, `GetSize`, `GetSectorSize` |
| [ByteReader.h](file:///home/ishaan/dev/sihv3/Secure-data-erasure-and-recovery/Recovery/Core/ByteReader.h) | 101 | Sector-alignment wrapper: `ReadBytes`, `ReadStruct<T>` |
| [WindowsReadOnlyStorage.h](file:///home/ishaan/dev/sihv3/Secure-data-erasure-and-recovery/Recovery/Acquisition/WindowsReadOnlyStorage.h) | 55 | Win32 implementation header |
| [WindowsReadOnlyStorage.cpp](file:///home/ishaan/dev/sihv3/Secure-data-erasure-and-recovery/Recovery/Acquisition/WindowsReadOnlyStorage.cpp) | 125 | Win32 implementation (CreateFileA, IOCTL, ReadFile) |
| [main.cpp (test)](file:///home/ishaan/dev/sihv3/Secure-data-erasure-and-recovery/Recovery/Tests/Phase1_ReadOnlyStorageTest/main.cpp) | 133 | Phase 1 demo/test |

## Files Modified

| File | Change |
|---|---|
| [.gitignore](file:///home/ishaan/dev/sihv3/Secure-data-erasure-and-recovery/.gitignore) | Added `*.exe` to exclude build artifacts |

**Zero Erasure files were modified.**

## Important Code Paths

### 1. Opening a device (read-only)

[WindowsReadOnlyStorage::Open()](file:///home/ishaan/dev/sihv3/Secure-data-erasure-and-recovery/Recovery/Acquisition/WindowsReadOnlyStorage.cpp#L15-L51) calls `CreateFileA` with `GENERIC_READ` (compare to Erasure's `GENERIC_READ | GENERIC_WRITE`). On success, it calls `UpdateGeometry()` to populate sector size and total bytes via `IOCTL_DISK_GET_DRIVE_GEOMETRY_EX`, with a fallback chain to `IOCTL_DISK_GET_LENGTH_INFO` + `IOCTL_DISK_GET_DRIVE_GEOMETRY`.

[LinuxReadOnlyStorage::Open()](file:///home/ishaan/dev/sihv3/Secure-data-erasure-and-recovery/Recovery/Acquisition/LinuxReadOnlyStorage.cpp#L18-L43) calls `::open()` with `O_RDONLY` (no write access). On success, it calls `UpdateGeometry()` to populate sector size and total bytes via `ioctl` calls.

### 2. Raw reading

[WindowsReadOnlyStorage::Read()](file:///home/ishaan/dev/sihv3/Secure-data-erasure-and-recovery/Recovery/Acquisition/WindowsReadOnlyStorage.cpp#L109-L124) takes a **byte offset** and byte count (not sector-based like the Erasure side). Uses `SetFilePointerEx` + `ReadFile`. The caller must provide sector-aligned parameters at this level.

[LinuxReadOnlyStorage::Read()](file:///home/ishaan/dev/sihv3/Secure-data-erasure-and-recovery/Recovery/Acquisition/LinuxReadOnlyStorage.cpp#L57-L69) takes a **byte offset** and byte count. Uses `pread()` which reads at a given offset without changing the file position. The caller must provide sector-aligned parameters at this level.

### 3. Arbitrary byte reads via ByteReader

[ByteReader::ReadBytes()](file:///home/ishaan/dev/sihv3/Secure-data-erasure-and-recovery/Recovery/Core/ByteReader.h#L51-L73) handles the alignment:
- Rounds offset down to nearest sector boundary
- Rounds end up to next sector boundary
- Reads the full aligned range into a temp buffer
- Copies only the requested sub-range back

This means future phases (partition parsers, MFT parsers) can read at any byte offset without worrying about sector alignment.

### 4. Closing

[WindowsReadOnlyStorage::Close()](file:///home/ishaan/dev/sihv3/Secure-data-erasure-and-recovery/Recovery/Acquisition/WindowsReadOnlyStorage.cpp#L53-L58) calls `CloseHandle` only — no `UnlockVolume` because we never lock the volume.

## Build Command

```bash
x86_64-w64-mingw32-g++ -std=c++17 -Wall -Wextra -O2 \
    -o Recovery/Tests/Phase1_ReadOnlyStorageTest/Phase1Test.exe \
    Recovery/Tests/Phase1_ReadOnlyStorageTest/main.cpp \
    Recovery/Acquisition/WindowsReadOnlyStorage.cpp \
    -static
```

**Result**: Clean build, zero warnings, zero errors. Produces a 2.4 MB statically-linked PE64 executable.

## Run Command

On a Windows machine with Administrator privileges:

```cmd
Phase1Test.exe
```

Then enter a device path like `\\.\E:` or `\\.\PhysicalDrive0`.

### Expected Output

```
=============================================
  Recovery Module — Phase 1 Test
  Read-Only Storage Foundation
=============================================

This test opens a device in READ-ONLY mode.
No data will be written. No volume locks.

Path Formats:
  Physical drive: \\.\PhysicalDrive0
  Volume:         \\.\E:

Enter device path: \\.\E:

[1] Opening device in READ-ONLY mode...
[OK] Device opened successfully.

[2] Device Geometry:
    Sector Size:  512 bytes
    Total Size:   8053063680 bytes (7.50 GB)
    Total Sectors: 15728640

[3] Reading Sector 0 (first 512 bytes) via IReadOnlyStorage::Read()...
[OK] First 64 bytes of Sector 0:
  00000000  eb 76 90 45 58 46 41 54  20 20 20 00 00 00 00 00  |.v.EXFAT   .....|
  00000010  00 00 00 00 00 00 00 00  ...                       |................|
  ...

[4] Reading 48 bytes at offset 0x100 via ByteReader (arbitrary alignment)...
[OK] Data at offset 0x100:
  00000100  xx xx xx xx ...

[5] Safety Verification:
    IReadOnlyStorage exposes: Open, Close, Read, GetSize, GetSectorSize
    NO WriteSectors, NO LockVolume, NO DismountVolume, NO SendDeviceCommand
    Win32 handle opened with GENERIC_READ only (no GENERIC_WRITE)
    [OK] Write operations are impossible at both API and OS kernel level.

[6] Closing device...
[OK] Device closed. No modifications were made.

=============================================
  Phase 1 Test Complete
=============================================
```

> [!NOTE]
> The exact hex bytes depend on the target device's contents. The output structure will match the above.

## Known Limitations

1. **Requires Administrator** — Raw device access on Windows requires elevation.
2. **No image file support** — Only physical drives and mounted volumes are supported. A future `ImageFileSource` could implement `IReadOnlyStorage` for `.dd` / `.raw` images.
3. **No partition awareness** — This phase reads raw byte offsets. It doesn't know where partitions start. That's Phase 2.
4. **Large unaligned reads** — `ByteReader` uses a single `std::vector` for the aligned buffer. Very large unaligned reads could allocate significant temporary memory. For Phase 1 this is fine; later phases may want chunked reads.

---

# Phase 2 — Partition Detection: Walkthrough

## What Was Implemented

A read-only partition-detection layer that can discover MBR and GPT partitions on any storage source exposed through `IReadOnlyStorage` + `ByteReader`.

### New Core Type

| File | Purpose |
|---|---|
| [PartitionInfo.h](file:///home/ishaan/dev/sihv3/Secure-data-erasure-and-recovery/Recovery/Core/PartitionInfo.h) | Filesystem-independent partition descriptor (`index`, `startLBA`, `sectorCount`, `startOffset`, `sizeBytes`, `scheme`, MBR-specific fields, GPT-specific fields incl. GUIDs & UTF-8 name) |

`PartitionScheme` enum distinguishes `MBR` vs `GPT` origin.

### Partition Parser Interface & Implementations

| File | Purpose |
|---|---|
| [IPartitionParser.h](file:///home/ishaan/dev/sihv3/Secure-data-erasure-and-recovery/Recovery/Partitions/IPartitionParser.h) | Abstract interface: `CanParse()` (signature check) + `Parse()` (returns `vector<PartitionInfo>`) |
| [MBRParser.h/.cpp](file:///home/ishaan/dev/sihv3/Secure-data-erasure-and-recovery/Recovery/Partitions/MBRParser.h) | Reads LBA 0, validates `0x55AA` boot signature, extracts up to 4 primary entries from the 64-byte partition table at offset `0x1BE`. Detects protective MBR (`0xEE`) and exposes `HasProtectiveMBR()` to signal GPT fallback. Maps type bytes to human-readable descriptions. |
| [GPTParser.h/.cpp](file:///home/ishaan/dev/sihv3/Secure-data-erasure-and-recovery/Recovery/Partitions/GPTParser.h) | Reads LBA 1 for the GPT header (`"EFI PART"` signature), then walks the partition entry array. Skips zero-GUID entries. Converts UTF-16LE partition names to UTF-8. Maps well-known type GUIDs to descriptions (Microsoft Basic Data, EFI System, Linux filesystem, etc.). |

### Detection Flow

```text
ByteReader
    ↓
MBRParser::CanParse()  →  checks 0x55AA at offset 0x1FE
    ↓
MBRParser::Parse()     →  extracts entries, flags protective MBR
    ↓ if HasProtectiveMBR()
GPTParser::CanParse()  →  checks "EFI PART" at LBA 1
    ↓
GPTParser::Parse()     →  reads header + entry array → PartitionInfo[]
```

### Test

| File | Purpose |
|---|---|
| [test_partitions.cpp](file:///home/ishaan/dev/sihv3/Secure-data-erasure-and-recovery/Recovery/Tests/Phase2_PartitionTest/test_partitions.cpp) | Programmatic synthetic tests — constructs in-memory MBR and GPT disk images via a `MockReadOnlyStorage`, runs the parsers, and asserts correctness without needing a physical disk. |

### Known Limitations

1. **No extended-partition chain walking** — MBR extended entries (0x05, 0x0F, 0x85) are reported but their logical partitions are not recursively enumerated.
2. **No CRC32 validation** — GPT header and entry-array CRC32 fields are not verified in V1.
3. **No backup GPT header fallback** — Only the primary header at LBA 1 is read.
4. **BMP-only UTF-16** — GPT partition names outside the Basic Multilingual Plane are not decoded.
