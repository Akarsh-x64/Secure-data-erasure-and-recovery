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

---

# Phase 3 — ext4 Filesystem Engine: Walkthrough

## What Was Implemented

Support for Linux `ext4` filesystems across both the **Erasure** and **Recovery** modules.

### 1. Erasure Module (`Erasure/File Systems/ext4/`)

- **`ext4_Structures.h`**: Strictly packed (`#pragma pack(push, 1)`) on-disk structures matching the Linux kernel:
  - `Ext4Superblock`: 1024-byte superblock at offset 1024, magic `0xEF53`, block sizes, group counts, feature incompat flags (`EXTENTS`, `64BIT`, `FILETYPE`).
  - `Ext4GroupDesc` / `Ext4GroupDesc64`: Block group descriptor tables (bitmaps, inode table offsets).
  - `Ext4Inode`: 128/256-byte inode structures, timestamps, link counts, deletion time (`i_dtime`), extent roots in `i_block`.
  - `Ext4ExtentHeader`, `Ext4Extent`, `Ext4ExtentIdx`: Extent tree parsing (magic `0xF30A`) for multi-level index and leaf blocks.
  - `Ext4DirEntry2`: Variable-length directory entries with name length and file type.
- **`ext4.h` / `ext4.cpp` (`Ext4Driver`)**: Implements `Core::IFileSystemDriver`:
  - `Mount()`: Reads superblock, validates `0xEF53`, loads GDT into memory, calculates dynamic geometries.
  - `EraseFile()`: Traverses path tokens from root inode 2, resolves extent trees to physical blocks, overwrites data sectors with `SecureEraseSectors`, clears block and inode allocation bitmap bits, zeroes inode record in inode table, and zeroes directory entry in parent directory block.
  - `WipeVolume()`: Quarantines vital structures (Superblock, GDT, bitmaps, inode tables, root inode 2), erases all user-allocated blocks, resets bitmaps, and zeroes user directory records.
  - `PrintSuperblockInfo()`: Diagnostic summary of mounted ext4 volume.

### 2. Recovery Module (`Recovery/Filesystems/`)

- **`Ext4Detector.h` / `Ext4Detector.cpp`**: Forensic read-only detector using `ByteReader` and `StorageRegion`:
  - Probes offset `1024` for the ext4 magic `0xEF53`.
  - Extracts forensic metadata: Volume Name, UUID, Block Size, Total Blocks, Free Blocks, Total Inodes, Free Inodes, Extents flag, and 64-bit flag.

### 3. Inode Sanitization & Inline Data Handling

- **Inline Data (`EXT4_INLINE_DATA_FL = 0x10000000`) & Fast Symlinks:**
  - When files are small (< 60 bytes), ext4 does not allocate external data blocks (`i_blocks_lo == 0`). Data is stored directly inside the inode's 60-byte `i_block` array, with potential overflow into the 128..255 byte extended attribute space of 256-byte inodes.
  - `GetInodeAllocatedBlocks()` explicitly recognizes inline data and zero-block inodes, returning 0 external blocks to avoid treating inline character bytes as block pointers.
- **`WipeInodeOnDisk()`:**
  - Rather than zeroing only the 128-byte base inode struct in memory, `WipeInodeOnDisk()` reads the physical Inode Table sector and zeroes all `m_inodeSize` bytes (all 256 bytes) directly on disk.
  - Obliterates:
    1. Inode payload in `i_block[60]` (for inline files).
    2. Extent tree root headers (`0xF30A`) and physical block pointers (`ee_start_lo`) (for regular files).
    3. Extended security attributes (xattrs) and inline overflow in bytes 128..255.
    4. File mode, link count (`i_links_count = 0`), and size (`i_size = 0`), while setting the deletion timestamp `i_dtime`.
### 4. Verification & Testing Suites

- **Automated Synthetic Unit Tests (`Tests/test_ext4.cpp`)**:
  - In-memory disk image (`MemoryDiskDevice`), running 27 assertions covering mount, traversal, single file erasure, recursive directory erasure, bitmap bit clearing, and volume wipe.
- Disk-Backed Demonstration & Byte-Level Inspection (`Tests/demo_erasure_xxd.cpp`):
  - Constructs a 4MB on-disk ext4 image (`demo_test_disk.img`) with mixed file hierarchies (inline data, regular extent files, nested directories).
  - Uses `xxd` to verify exact before-and-after byte states of inodes, data blocks, directory blocks, and allocation bitmaps.

---

# Phase 4 — NTFS Filesystem Secure Deletion & Forensic Verification: Walkthrough

## What Was Implemented

Comprehensive forensic-grade Secure Deletion, Recursive Folder Obliteration, Volume/Drive Formatting, and Byte-Level `xxd` Hex Dump Verification for the **NTFS** (New Technology File System) inside the `Erasure` module.

### 1. Erasure Module Architecture (`Erasure/File Systems/NTFS/`)

Strictly adheres to the decoupled 3-Layer Architecture (`IStorageDevice` -> `IHardwareController` -> `IFileSystemDriver`) and follows the exact file structure of the `Erasure` folder. Utilizes **exclusively** the existing `Core::IHardwareController` methods (`ReadSectors`, `WriteSectors`, `SecureEraseSectors`, `SecureEraseDrive`, `GetGeometry`) with **zero direct Win32/POSIX syscalls**:

- **`NTFS_Structures.h`**:
  - Strictly packed (`#pragma pack(push, 1)`) on-disk structures matching raw NTFS layouts:
    - `NtfsBootSector`: Sector 0 Volume Boot Record (VBR) with `"NTFS    "` OEM identifier, bytes per sector, sectors per cluster, total sectors, `$MFT` start LCN, `$MFTMirr` start LCN, clusters per MFT record (negative power of 2: `-10` -> 1024 bytes), clusters per index buffer (`-12` -> 4096 bytes), serial number, and `0xAA55` boot signature.
    - `NtfsRecordHeader`: 1024-byte MFT record header (`"FILE"` magic, update sequence / fixup offset & size, LSN, sequence number, link count, attribute offset, flags `0x0001` in-use / `0x0002` directory, used/allocated bytes).
    - `NtfsAttributeHeader`: Common attribute header, plus resident header (`valueLength`, `valueOffset`) and non-resident header (`startingVCN`, `highestVCN`, `dataRunsOffset`, allocated/real/initialized sizes).
    - Attribute Types: `ATTR_STANDARD_INFORMATION` (`0x10`), `ATTR_ATTRIBUTE_LIST` (`0x20`), `ATTR_FILE_NAME` (`0x30`), `ATTR_DATA` (`0x80`), `ATTR_INDEX_ROOT` (`0x90`), `ATTR_INDEX_ALLOCATION` (`0xA0`), `ATTR_BITMAP` (`0xB0`), `ATTR_END` (`0xFFFFFFFF`).
    - B-Tree Directory Structures: `NtfsIndexRootHeader`, `NtfsIndexHeader`, `NtfsIndexEntry` (48-bit record ref, entry length, key length, flags, UTF-16 filename, child VCN), and 4096-byte `"INDX"` blocks (`NtfsIndexBlock`).
    - Standard System Records: Record 0 (`$MFT`), 1 (`$MFTMirr`), 2 (`$LogFile`), 3 (`$Volume`), 5 (`$Root`), 6 (`$Bitmap`), 7 (`$Boot`), 16+ (User Files/Directories).
    - Helper Types: `NtfsExtent { uint64_t lcn; uint64_t clusterCount; }` and `TargetLocations`.

- **`NTFS.h` / `NTFS.cpp` (`NtfsDriver`)**:
  - Implements `Core::IFileSystemDriver`:
    - `Mount()`: Reads Sector 0, validates `"NTFS    "` and `0xAA55`, decodes dynamic geometry without hardcoded offsets, reads Record 0 (`$MFT`), and decodes its runlist to build dynamic MFT extent mappings for navigating fragmented MFT records.
    - `ApplyFixup()`: Applies NTFS update sequence (fixup array) to protected 512-byte sectors.
    - `DecodeRunList()`: Decompresses variable-length runlists into contiguous `(LCN, clusterCount)` extents.
    - `EraseFile(relativePath)`: Traverses directory B-trees from Root Record 5.
      - If resident `$DATA`: zeroes data payload bytes in-place inside the MFT record.
      - If non-resident `$DATA`: converts runlist LCNs to sectors, calls `m_hardware->SecureEraseSectors()`, and clears cluster bits in `$Bitmap` (Record 6).
      - Completely zeroes the 1024-byte MFT record on disk (`WipeMftRecordOnDisk`).
      - Scrubs the entry from the parent directory's `$INDEX_ROOT` or `"INDX"` blocks (`ScrubDirectoryEntry`).
      - Seamlessly delegates to `EraseDirectory` if target is a folder.
    - `EraseDirectory(relativePath)` & `EraseDirectoryRecursive()`:
      - Recursively parses directory index entries, eradicates all child files, nested subdirectories, and directory index blocks (`$INDEX_ALLOCATION`).
      - Zeroes directory MFT records on disk and scrubs parent directory index entries.
    - `WipeVolume()`:
      - Quarantines system metadata records (0–15).
      - Scans all active user records (16+) in `$MFT`, sanitizes all allocated data extents via `SecureEraseSectors`, clears bitmap bits, zeroes on-disk MFT records, and scrubs user entries from Root directory.
    - `FormatDrive(fullDriveSanitize)`:
      - Sanitizes the drive via `m_hardware->SecureEraseDrive()` (or sweeping zero overwrite).
      - Writes a pristine, valid NTFS VBR at Sector 0 and initializes clean MFT system records (Records 0-15: `$MFT`, `$MFTMirr`, `$Volume`, `$Root` Record 5 with empty `$INDEX_ROOT`, and `$Bitmap` Record 6) using only `m_hardware->WriteSectors()`.

### 2. Forensic `xxd`-Style Hex Verification & Semantic Byte Breakdown Engine

Built directly into `NtfsDriver` (`VerifyAndErase` and `VerifyAndFormatDrive`) supporting **File**, **Folder**, and **Disk / Volume** inputs:

- **Before Deletion Inspection**:
  - Resolves exact physical disk sectors and byte offsets for:
    1. Target MFT Record (1024 bytes)
    2. Data Sectors (resident payload or non-resident cluster extents)
    3. Parent Directory Index Entry
    4. Cluster Allocation Bitmap byte (`$Bitmap` Record 6)
  - Outputs standard `xxd`-compatible hex dumps (`[Offset] [16 Hex Bytes] |[ASCII]|`).
  - Outputs an annotated **Byte-by-Byte Semantic Breakdown** explaining:
    - MFT Header: Magic `"FILE"`, Update sequence, LSN, Sequence number, Flags (`0x0001` In-Use, `0x0002` Directory).
    - Attributes: `$STANDARD_INFORMATION` (timestamps), `$FILE_NAME` (parent ref, length, UTF-16 characters), `$DATA` (payload or runlist LCNs).
    - Directory entries: File reference, key length, child VCNs.
    - `$Bitmap` byte: bit value `1` denoting allocated status.

- **After Deletion Re-Inspection**:
  - Re-reads the exact same physical byte offsets from disk.
  - Outputs post-deletion `xxd` hex dumps.
  - Explains the forensic difference:
    - Data sectors: all `0x00` (wiped).
    - MFT record: zeroed on disk (`0x00000000`).
    - Parent directory entry: scrubbed.
    - `$Bitmap` byte: bit cleared from `1` (allocated) to `0` (free).

### 3. Verification Runner (`Tests/test_ntfs.cpp`)

- Statically linked test executable: `Tests/test_ntfs_runner.exe`.
- Simulates an in-memory disk device (`MemoryDiskDevice`) coupled with `HDDController` and `NtfsDriver`.
- Demonstrates:
  - File verification & erasure (`--file passwords.txt`).
  - Folder recursive verification & erasure (`--folder Finance`).
  - Disk format verification (`--disk`).

---

# Phase 5 — XFS Filesystem Secure Deletion & Recursive Folder Eradication: Walkthrough

## What Was Implemented

Support for the Silicon Graphics **XFS** (Extents & B+Trees) filesystem inside the `Erasure` module, covering both single file destruction, nested file erasure, and complete recursive folder eradication.

### 1. Architecture & On-Disk Structures (`Erasure/File Systems/XFS/`)

- **`XFS_Structures.h`**:
  - Strictly packed (`#pragma pack(push, 1)`) on-disk structures matching raw XFS v4 and v5 (CRC) layouts.
  - Endianness conversion helpers: `be16_to_cpu`, `be32_to_cpu`, `be64_to_cpu` converting Big-Endian disk records to host CPU order with zero-overhead compiler intrinsics.
  - `XfsSuperblock`: Magic `0x58465342` ("XFSB"), block size, AG blocks, AG count, root inode (`sb_rootino`), and log geometry.
  - `XfsDinodeCore`: On-disk inode structure (v2 and v3 CRC), mode, format (`LOCAL`, `EXTENTS`, `BTREE`), timestamps, sizes, and fork offsets.
  - Extent & B+Tree Records: 128-bit packed `XfsBmbtRec` records, `XfsBmdrBlock`, `XfsBtreeBlock` (magic `0x424D4150` "BMAP").
  - Directory Structures: `XfsDir2SfHdr` (Shortform directories), `XfsDir2DataHdr` / `XfsDir3DataHdr` (Block/Extent directories), and `XfsDir2BlockTail` (leaf hash arrays).

- **`XFS.h` / `XFS.cpp` (`XfsDriver`)**:
  - Implements `Core::IFileSystemDriver`:
    - `Mount()`: Validates `"XFSB"`, unpacks primary superblock at Sector 0, computes AG bitshifts and block addressing geometries.
    - `EraseFile(relativePath)`: Traverses directory hierarchy from root inode `m_rootIno`. If the target is a directory, automatically delegates to `EraseDirectory`. If a file: resolves extents, zeroes data blocks via `m_hardware->SecureEraseSectors()`, zeroes indirect B+Tree blocks, wipes the on-disk Inode structure (`WipeInodeOnDisk`), scrubs the directory entry from the parent directory (`WipeDirectoryEntry`), and cleans Intent Log transactions (`ScrubJournalForInode`).
    - `EraseDirectory(relativePath)` & `EraseDirectoryRecursive(dirIno)`:
      - Traverses Shortform (`LOCAL`) and Block/Extent (`EXTENTS`/`BTREE`) directories.
      - Recursively eradicates all child files, nested subdirectories, directory extent blocks, on-disk inodes, and parent directory records.
    - `WipeVolume()`: Quarantines allocation group headers (Superblock, AGF, AGI, AGFL across all AGs) and root inode, while zeroing all user data blocks and resetting directory tables.

### 2. Testing & Verification Suite (`Tests/test_xfs.cpp`)

- Synthetic in-memory XFS disk image (`MemoryDiskDevice` + `HDDController`).
- Comprehensive assertions covering:
  - Mount & superblock parsing (Block size 4096, 1 AG, root inode 64).
  - Test 1: Single file erasure (`secret.txt` in Extent format).
  - Test 2: B+Tree file erasure (`archive.bin` with indirect B+Tree metadata block 20 and data block 30).
  - Test 3: Recursive Folder Erasure (`docs` directory containing nested file `report.txt` pointing to data block 35).
  - Test 4: Surgical Volume Wipe (`WipeVolume()`).
- Statically linked test executable: `Tests/test_xfs_runner.exe`.

---

# Phase 6 — Unified Multi-Filesystem Master Test Runner (`Tests/main.cpp`): Walkthrough

## What Was Implemented

Consolidated all individual filesystem test runners (`test_ntfs.cpp`, `test_xfs.cpp`, `test_ext4.cpp`) into **one single unified test runner** (`Tests/main.cpp`), adhering to `Core::IFileSystemDriver` across all four supported filesystems: **NTFS, XFS, ext4, and exFAT**.

### 1. Key Capabilities & Forensic Verification Features
- **All 4 Filesystems Supported**:
  - `Erasure::FileSystems::NtfsDriver` (`NTFS.h` / `NTFS.cpp`)
  - `Erasure::FileSystems::XfsDriver` (`XFS.h` / `XFS.cpp`)
  - `Erasure::FileSystems::Ext4Driver` (`ext4.h` / `ext4.cpp`)
  - `Erasure::FileSystems::ExFatDriver` (`exFAT.h` / `exFAT.cpp`): Implements Microsoft exFAT spec-compliant checksum and hash algorithms (`ComputeNameHash`, `ComputeEntrySetChecksum`, `ComputeBootChecksum`, `VerifyBootChecksum`, `VerifyEntrySetChecksum`) directly within the driver class.
- **Intake Targets**:
  - **File Input**: Surgically deletes single files, sanitizes on-disk metadata (Inodes/MFT records/Directory entries), and wipes data runs/extents/clusters.
  - **Folder Input**: Recursively traverses folder hierarchies, eradicating all nested child files, child directories, directory blocks/indexes, and unlinking from parent structures.
  - **Disk Input**: Provisions complete volume formatting (`FormatDrive`) and surgical volume-wide sanitization (`WipeVolume`).
- **Forensic Verification Output**:
  - **Canonical `xxd` Hex Dumps**: Before-and-after hex dumps showing exact byte values, physical disk offsets, 16-byte aligned hexadecimal representations, and ASCII decodes.
  - **Semantic Byte Breakdowns**: Clear forensic explanations of on-disk structures:
    - Data sectors (active user payload vs `0x00` zero saturation)
    - NTFS VBR, MFT record headers (`FILE`, flags, sequence), attribute records
    - XFS Superblock, dinode core (`IN`, format, mode, size), BMBT extents
    - ext4 Superblock (`0xEF53`), Group Descriptors, Inode (`0xF30A` extent tree), Directory entries
    - exFAT VBR, Allocation Bitmap, File (`0x85`), Stream (`0xC0`), Filename (`0xC1`) entries
- **Operational Execution Modes**:
  - **Automated Synthetic Self-Test** (`.\Tests\main.exe --test`): Hermetic in-memory verification across all 4 filesystems via `MemoryDiskDevice` without requiring physical drive handles or administrative privileges.
  - **Interactive Live Session** (`.\Tests\main.exe`): Prompts user for live physical drives (`\\.\PhysicalDrive1`), mounted volume letters (`\\.\E:`), selects filesystem (1-4), and performs verified erasure/formatting.

### 2. Compilation
- Target binary: `Tests/main.exe`
- Compiler: MinGW-w64 UCRT GCC 14.2 (`g++ -std=c++17 -Wall -Wextra -O2`)
- Dependencies: `WindowsStorageDevice.cpp`, `HDDController.cpp`, `NTFS.cpp`, `XFS.cpp`, `ext4.cpp`, `exFAT.cpp`.
- Status: Build succeeded with 0 errors.

---

# Phase 7 — Comprehensive Bug Audit, Hardening & Spec Alignment Across All Erasure Subsystems: Walkthrough

## Overview of Audit & Fixes

An exhaustive codebase audit was conducted across every subsystem in `Erasure/` (`Core/`, `Hardware/`, `OS/`, and `File Systems/`) to identify and resolve memory safety violations, integer overflows, buffer overruns, division-by-zero vulnerabilities, and specification compliance defects.

### 1. `Erasure/Hardware/Magnetic/HDDController.cpp`
* **Vulnerability Fixed**: In `OverwriteWithPattern(startSector, sectorCount, pattern)`, allocating `sectorCount * sectorSize` directly on the heap caused 32-bit arithmetic overflow on large sector spans (e.g. >= 8,388,608 sectors) and unbounded heap allocation resulting in `std::bad_alloc`.
* **Resolution**: Replaced single-shot allocation with bounded, chunked writes (up to 1024 sectors = 512KB per iteration), processing any arbitrary sector count securely without memory exhaustion.
* **DoD 5220.22-M 3-Pass Overwrite Standard Implemented**:
  - `SecureEraseSectors` and `SecureEraseDrive` upgraded to execute an authentic 3-pass physical overwrite cycle:
    1. **Pass 1**: Fill with all zeros (`0x00` / binary `00000000`).
    2. **Pass 2**: Fill with all ones (`0xFF` / binary `11111111`).
    3. **Pass 3**: Fill with 64-bit Mersenne Twister (`std::mt19937_64`) pseudorandom noise / "gibberish".
  - This eliminates residual magnetic hysteresis (remanence) on spinning magnetic platters and abstracts virtual disk sanitization. Added `OverwriteWithRandom()` helper.

### 2. `Erasure/OS/Windows/WindowsStorageDevice.cpp`
* **Defect Fixed**: `UpdateGeometry()` relied exclusively on `IOCTL_DISK_GET_DRIVE_GEOMETRY_EX`, which routinely fails on mounted volume handles (e.g., `\\.\E:` or `\\.\D:`), preventing volume-level testing and erasure.
* **Resolution**:
  - Implemented fallback cascade to `IOCTL_DISK_GET_DRIVE_GEOMETRY` and `IOCTL_DISK_GET_LENGTH_INFO`.
  - Added fallback default of 512-byte sector size if underlying geometry cannot be directly queried.
  - Zero-initialized Win32 structs via `std::memset` (`#include <cstring>`) to eliminate compiler warnings.
  - Added guards in `ReadSectors` and `WriteSectors` validating `m_geometry.bytesPerSector > 0`.

### 3. `Erasure/File Systems/NTFS/NTFS.cpp`
* **Critical Memory Safety Bug in `FormatDrive`**:
  - *Previous code*: Allocated `secBuf` of size `bytesPerSec` (512 bytes on standard drives), then called `std::memcpy(secBuf.data() + offInSec, cleanRecord.data(), 1024)`.
  - *Impact*: Caused a catastrophic **512-byte heap memory corruption** on every MFT record written and only wrote half of the record to disk.
  - *Resolution*: Calculated `sectorsNeeded = (1024 + offInSec + bytesPerSec - 1) / bytesPerSec`, allocated `sectorsNeeded * bytesPerSec`, and performed full multi-sector reads and writes.
* **Runlist Length Underflow in `$INDEX_ALLOCATION` Decoding**:
  - *Previous code*: Lines 495, 611, and 860 passed `indexAllocAttr[1] - nonRes->dataRunsOffset` to `DecodeRunList()`. Since `indexAllocAttr[1]` accessed byte 1 of the attribute type (`0x00`), `0 - dataRunsOffset` underflowed to a massive positive `size_t`.
  - *Resolution*: Replaced with `(attrHdr->length > nonRes->dataRunsOffset) ? (attrHdr->length - nonRes->dataRunsOffset) : 0`.
* **Signed Shift Undefined Behavior in `DecodeRunList`**:
  - *Previous code*: Used `static_cast<int64_t>(0xFF) << (i * 8)`, shifting into the sign bit of `int64_t`.
  - *Resolution*: Sign-extended cleanly using unsigned bitmask `uint64_t mask = ~0ULL << (offsetFieldSize * 8)` and standard two's complement conversion.
* **Bounds Hardening**:
  - Enforced string offset and length bounds on UTF-16 attribute names in `FindAttribute`.
  - Validated resident attribute value length and offset bounds against both the attribute record and the MFT buffer in `GetFileAllocatedExtents`.

### 4. `Erasure/File Systems/exFAT/exFAT.cpp`
* **Directory Deletion & Directory Stream Truncation Bug**:
  - *Previous code*: Zeroing the entire 32-byte directory entry set with `std::memset` set `EntryType = 0x00`. Per Microsoft exFAT specification, `0x00` represents **EndOfDirectory**, terminating directory scanning immediately and rendering all subsequent files and folders in that directory inaccessible.
  - *Resolution*: In accordance with Microsoft exFAT spec §6.2.1.1, cleared bit 7 (`InUse = 0`, transforming e.g. `0x85` -> `0x05`, `0xC0` -> `0x40`, `0xC1` -> `0x41`) and zeroed the remaining 31 bytes of each entry. This securely obliterates file names, timestamps, cluster pointers, and data lengths while keeping the directory traversal chain intact.
* **Division-by-Zero Guards**:
  - Added guards against `m_bytesPerSector == 0` or `m_sectorsPerCluster == 0` in `ClearBitmapBit` and `GetClusterChain`.
* **VBR Parameter Validation in `Mount()`**:
  - Validated shift parameters per exFAT spec §3.1.5: `bytesPerSectorShift` must be in the range [9, 12] (512B to 4096B) and `sectorsPerClusterShift <= 25` (max cluster size 32MB), preventing undefined bit shift behavior on corrupted media.

### 5. `Erasure/File Systems/ext4/ext4.cpp`
* **Superblock Validation in `Mount()`**:
  - Enforced `s_log_block_size <= 6` (maximum 64KB block size per Linux ext4 kernel specifications) to prevent bit-shift overflow in `1024 << s_log_block_size`.
  - Verified `m_inodeSize <= m_blockSize` and `(m_blockSize % m_inodeSize) == 0` to prevent buffer overruns when computing inode block offsets.
  - Guarded against underflow in block group counting (`totalBlocks < s_first_data_block`).

### 6. `Tests/main.cpp`
- `NtfsBootSector`: Sector 0 Volume Boot Record (VBR) with `"NTFS    "` OEM identifier, bytes per sector, sectors per cluster, total sectors, `$MFT` start LCN, `$MFTMirr` start LCN, clusters per MFT record (negative power of 2: `-10` -> 1024 bytes), clusters per index buffer (`-12` -> 4096 bytes), serial number, and `0xAA55` boot signature.
    - `NtfsRecordHeader`: 1024-byte MFT record header (`"FILE"` magic, update sequence / fixup offset & size, LSN, sequence number, link count, attribute offset, flags `0x0001` in-use / `0x0002` directory, used/allocated bytes).
    - `NtfsAttributeHeader`: Common attribute header, plus resident header (`valueLength`, `valueOffset`) and non-resident header (`startingVCN`, `highestVCN`, `dataRunsOffset`, allocated/real/initialized sizes).
    - Attribute Types: `ATTR_STANDARD_INFORMATION` (`0x10`), `ATTR_ATTRIBUTE_LIST` (`0x20`), `ATTR_FILE_NAME` (`0x30`), `ATTR_DATA` (`0x80`), `ATTR_INDEX_ROOT` (`0x90`), `ATTR_INDEX_ALLOCATION` (`0xA0`), `ATTR_BITMAP` (`0xB0`), `ATTR_END` (`0xFFFFFFFF`).
    - B-Tree Directory Structures: `NtfsIndexRootHeader`, `NtfsIndexHeader`, `NtfsIndexEntry` (48-bit record ref, entry length, key length, flags, UTF-16 filename, child VCN), and 4096-byte `"INDX"` blocks (`NtfsIndexBlock`).
    - Standard System Records: Record 0 (`$MFT`), 1 (`$MFTMirr`), 2 (`$LogFile`), 3 (`$Volume`), 5 (`$Root`), 6 (`$Bitmap`), 7 (`$Boot`), 16+ (User Files/Directories).
    - Helper Types: `NtfsExtent { uint64_t lcn; uint64_t clusterCount; }` and `TargetLocations`.

- **`NTFS.h` / `NTFS.cpp` (`NtfsDriver`)**:
  - Implements `Core::IFileSystemDriver`:
    - `Mount()`: Reads Sector 0, validates `"NTFS    "` and `0xAA55`, decodes dynamic geometry without hardcoded offsets, reads Record 0 (`$MFT`), and decodes its runlist to build dynamic MFT extent mappings for navigating fragmented MFT records.
    - `ApplyFixup()`: Applies NTFS update sequence (fixup array) to protected 512-byte sectors.
    - `DecodeRunList()`: Decompresses variable-length runlists into contiguous `(LCN, clusterCount)` extents.
    - `EraseFile(relativePath)`: Traverses directory B-trees from Root Record 5.
      - If resident `$DATA`: zeroes data payload bytes in-place inside the MFT record.
      - If non-resident `$DATA`: converts runlist LCNs to sectors, calls `m_hardware->SecureEraseSectors()`, and clears cluster bits in `$Bitmap` (Record 6).
      - Completely zeroes the 1024-byte MFT record on disk (`WipeMftRecordOnDisk`).
      - Scrubs the entry from the parent directory's `$INDEX_ROOT` or `"INDX"` blocks (`ScrubDirectoryEntry`).
      - Seamlessly delegates to `EraseDirectory` if target is a folder.
    - `EraseDirectory(relativePath)` & `EraseDirectoryRecursive()`:
      - Recursively parses directory index entries, eradicates all child files, nested subdirectories, and directory index blocks (`$INDEX_ALLOCATION`).
      - Zeroes directory MFT records on disk and scrubs parent directory index entries.
    - `WipeVolume()`:
      - Quarantines system metadata records (0–15).
      - Scans all active user records (16+) in `$MFT`, sanitizes all allocated data extents via `SecureEraseSectors`, clears bitmap bits, zeroes on-disk MFT records, and scrubs user entries from Root directory.
    - `FormatDrive(fullDriveSanitize)`:
      - Sanitizes the drive via `m_hardware->SecureEraseDrive()` (or sweeping zero overwrite).
      - Writes a pristine, valid NTFS VBR at Sector 0 and initializes clean MFT system records (Records 0-15: `$MFT`, `$MFTMirr`, `$Volume`, `$Root` Record 5 with empty `$INDEX_ROOT`, and `$Bitmap` Record 6) using only `m_hardware->WriteSectors()`.

### 2. Forensic `xxd`-Style Hex Verification & Semantic Byte Breakdown Engine

Built directly into `NtfsDriver` (`VerifyAndErase` and `VerifyAndFormatDrive`) supporting **File**, **Folder**, and **Disk / Volume** inputs:

- **Before Deletion Inspection**:
  - Resolves exact physical disk sectors and byte offsets for:
    1. Target MFT Record (1024 bytes)
    2. Data Sectors (resident payload or non-resident cluster extents)
    3. Parent Directory Index Entry
    4. Cluster Allocation Bitmap byte (`$Bitmap` Record 6)
  - Outputs standard `xxd`-compatible hex dumps (`[Offset] [16 Hex Bytes] |[ASCII]|`).
  - Outputs an annotated **Byte-by-Byte Semantic Breakdown** explaining:
    - MFT Header: Magic `"FILE"`, Update sequence, LSN, Sequence number, Flags (`0x0001` In-Use, `0x0002` Directory).
    - Attributes: `$STANDARD_INFORMATION` (timestamps), `$FILE_NAME` (parent ref, length, UTF-16 characters), `$DATA` (payload or runlist LCNs).
    - Directory entries: File reference, key length, child VCNs.
    - `$Bitmap` byte: bit value `1` denoting allocated status.

- **After Deletion Re-Inspection**:
  - Re-reads the exact same physical byte offsets from disk.
  - Outputs post-deletion `xxd` hex dumps.
  - Explains the forensic difference:
    - Data sectors: all `0x00` (wiped).
    - MFT record: zeroed on disk (`0x00000000`).
    - Parent directory entry: scrubbed.
    - `$Bitmap` byte: bit cleared from `1` (allocated) to `0` (free).

### 3. Verification Runner (`Tests/test_ntfs.cpp`)

- Statically linked test executable: `Tests/test_ntfs_runner.exe`.
- Simulates an in-memory disk device (`MemoryDiskDevice`) coupled with `HDDController` and `NtfsDriver`.
- Demonstrates:
  - File verification & erasure (`--file passwords.txt`).
  - Folder recursive verification & erasure (`--folder Finance`).
  - Disk format verification (`--disk`).

---

# Phase 5 — XFS Filesystem Secure Deletion & Recursive Folder Eradication: Walkthrough

## What Was Implemented

Support for the Silicon Graphics **XFS** (Extents & B+Trees) filesystem inside the `Erasure` module, covering both single file destruction, nested file erasure, and complete recursive folder eradication.

### 1. Architecture & On-Disk Structures (`Erasure/File Systems/XFS/`)

- **`XFS_Structures.h`**:
  - Strictly packed (`#pragma pack(push, 1)`) on-disk structures matching raw XFS v4 and v5 (CRC) layouts.
  - Endianness conversion helpers: `be16_to_cpu`, `be32_to_cpu`, `be64_to_cpu` converting Big-Endian disk records to host CPU order with zero-overhead compiler intrinsics.
  - `XfsSuperblock`: Magic `0x58465342` ("XFSB"), block size, AG blocks, AG count, root inode (`sb_rootino`), and log geometry.
  - `XfsDinodeCore`: On-disk inode structure (v2 and v3 CRC), mode, format (`LOCAL`, `EXTENTS`, `BTREE`), timestamps, sizes, and fork offsets.
  - Extent & B+Tree Records: 128-bit packed `XfsBmbtRec` records, `XfsBmdrBlock`, `XfsBtreeBlock` (magic `0x424D4150` "BMAP").
  - Directory Structures: `XfsDir2SfHdr` (Shortform directories), `XfsDir2DataHdr` / `XfsDir3DataHdr` (Block/Extent directories), and `XfsDir2BlockTail` (leaf hash arrays).

- **`XFS.h` / `XFS.cpp` (`XfsDriver`)**:
  - Implements `Core::IFileSystemDriver`:
    - `Mount()`: Validates `"XFSB"`, unpacks primary superblock at Sector 0, computes AG bitshifts and block addressing geometries.
    - `EraseFile(relativePath)`: Traverses directory hierarchy from root inode `m_rootIno`. If the target is a directory, automatically delegates to `EraseDirectory`. If a file: resolves extents, zeroes data blocks via `m_hardware->SecureEraseSectors()`, zeroes indirect B+Tree blocks, wipes the on-disk Inode structure (`WipeInodeOnDisk`), scrubs the directory entry from the parent directory (`WipeDirectoryEntry`), and cleans Intent Log transactions (`ScrubJournalForInode`).
    - `EraseDirectory(relativePath)` & `EraseDirectoryRecursive(dirIno)`:
      - Traverses Shortform (`LOCAL`) and Block/Extent (`EXTENTS`/`BTREE`) directories.
      - Recursively eradicates all child files, nested subdirectories, directory extent blocks, on-disk inodes, and parent directory records.
    - `WipeVolume()`: Quarantines allocation group headers (Superblock, AGF, AGI, AGFL across all AGs) and root inode, while zeroing all user data blocks and resetting directory tables.

### 2. Testing & Verification Suite (`Tests/test_xfs.cpp`)

- Synthetic in-memory XFS disk image (`MemoryDiskDevice` + `HDDController`).
- Comprehensive assertions covering:
  - Mount & superblock parsing (Block size 4096, 1 AG, root inode 64).
  - Test 1: Single file erasure (`secret.txt` in Extent format).
  - Test 2: B+Tree file erasure (`archive.bin` with indirect B+Tree metadata block 20 and data block 30).
  - Test 3: Recursive Folder Erasure (`docs` directory containing nested file `report.txt` pointing to data block 35).
  - Test 4: Surgical Volume Wipe (`WipeVolume()`).
- Statically linked test executable: `Tests/test_xfs_runner.exe`.

---

# Phase 6 — Unified Multi-Filesystem Master Test Runner (`Tests/main.cpp`): Walkthrough

## What Was Implemented

Consolidated all individual filesystem test runners (`test_ntfs.cpp`, `test_xfs.cpp`, `test_ext4.cpp`) into **one single unified test runner** (`Tests/main.cpp`), adhering to `Core::IFileSystemDriver` across all four supported filesystems: **NTFS, XFS, ext4, and exFAT**.

### 1. Key Capabilities & Forensic Verification Features
- **All 4 Filesystems Supported**:
  - `Erasure::FileSystems::NtfsDriver` (`NTFS.h` / `NTFS.cpp`)
  - `Erasure::FileSystems::XfsDriver` (`XFS.h` / `XFS.cpp`)
  - `Erasure::FileSystems::Ext4Driver` (`ext4.h` / `ext4.cpp`)
  - `Erasure::FileSystems::ExFatDriver` (`exFAT.h` / `exFAT.cpp`): Implements Microsoft exFAT spec-compliant checksum and hash algorithms (`ComputeNameHash`, `ComputeEntrySetChecksum`, `ComputeBootChecksum`, `VerifyBootChecksum`, `VerifyEntrySetChecksum`) directly within the driver class.
- **Intake Targets**:
  - **File Input**: Surgically deletes single files, sanitizes on-disk metadata (Inodes/MFT records/Directory entries), and wipes data runs/extents/clusters.
  - **Folder Input**: Recursively traverses folder hierarchies, eradicating all nested child files, child directories, directory blocks/indexes, and unlinking from parent structures.
  - **Disk Input**: Provisions complete volume formatting (`FormatDrive`) and surgical volume-wide sanitization (`WipeVolume`).
- **Forensic Verification Output**:
  - **Canonical `xxd` Hex Dumps**: Before-and-after hex dumps showing exact byte values, physical disk offsets, 16-byte aligned hexadecimal representations, and ASCII decodes.
  - **Semantic Byte Breakdowns**: Clear forensic explanations of on-disk structures:
    - Data sectors (active user payload vs `0x00` zero saturation)
    - NTFS VBR, MFT record headers (`FILE`, flags, sequence), attribute records
    - XFS Superblock, dinode core (`IN`, format, mode, size), BMBT extents
    - ext4 Superblock (`0xEF53`), Group Descriptors, Inode (`0xF30A` extent tree), Directory entries
    - exFAT VBR, Allocation Bitmap, File (`0x85`), Stream (`0xC0`), Filename (`0xC1`) entries
- **Operational Execution Modes**:
  - **Automated Synthetic Self-Test** (`.\Tests\main.exe --test`): Hermetic in-memory verification across all 4 filesystems via `MemoryDiskDevice` without requiring physical drive handles or administrative privileges.
  - **Interactive Live Session** (`.\Tests\main.exe`): Prompts user for live physical drives (`\\.\PhysicalDrive1`), mounted volume letters (`\\.\E:`), selects filesystem (1-4), and performs verified erasure/formatting.

### 2. Compilation
- Target binary: `Tests/main.exe`
- Compiler: MinGW-w64 UCRT GCC 14.2 (`g++ -std=c++17 -Wall -Wextra -O2`)
- Dependencies: `WindowsStorageDevice.cpp`, `HDDController.cpp`, `NTFS.cpp`, `XFS.cpp`, `ext4.cpp`, `exFAT.cpp`.
- Status: Build succeeded with 0 errors.

---

# Phase 7 — Comprehensive Bug Audit, Hardening & Spec Alignment Across All Erasure Subsystems: Walkthrough

## Overview of Audit & Fixes

An exhaustive codebase audit was conducted across every subsystem in `Erasure/` (`Core/`, `Hardware/`, `OS/`, and `File Systems/`) to identify and resolve memory safety violations, integer overflows, buffer overruns, division-by-zero vulnerabilities, and specification compliance defects.

### 1. `Erasure/Hardware/Magnetic/HDDController.cpp`
* **Vulnerability Fixed**: In `OverwriteWithPattern(startSector, sectorCount, pattern)`, allocating `sectorCount * sectorSize` directly on the heap caused 32-bit arithmetic overflow on large sector spans (e.g. >= 8,388,608 sectors) and unbounded heap allocation resulting in `std::bad_alloc`.
* **Resolution**: Replaced single-shot allocation with bounded, chunked writes (up to 1024 sectors = 512KB per iteration), processing any arbitrary sector count securely without memory exhaustion.
* **DoD 5220.22-M 3-Pass Overwrite Standard Implemented**:
  - `SecureEraseSectors` and `SecureEraseDrive` upgraded to execute an authentic 3-pass physical overwrite cycle:
    1. **Pass 1**: Fill with all zeros (`0x00` / binary `00000000`).
    2. **Pass 2**: Fill with all ones (`0xFF` / binary `11111111`).
    3. **Pass 3**: Fill with 64-bit Mersenne Twister (`std::mt19937_64`) pseudorandom noise / "gibberish".
  - This eliminates residual magnetic hysteresis (remanence) on spinning magnetic platters and abstracts virtual disk sanitization. Added `OverwriteWithRandom()` helper.

### 2. `Erasure/OS/Windows/WindowsStorageDevice.cpp`
* **Defect Fixed**: `UpdateGeometry()` relied exclusively on `IOCTL_DISK_GET_DRIVE_GEOMETRY_EX`, which routinely fails on mounted volume handles (e.g., `\\.\E:` or `\\.\D:`), preventing volume-level testing and erasure.
* **Resolution**:
  - Implemented fallback cascade to `IOCTL_DISK_GET_DRIVE_GEOMETRY` and `IOCTL_DISK_GET_LENGTH_INFO`.
  - Added fallback default of 512-byte sector size if underlying geometry cannot be directly queried.
  - Zero-initialized Win32 structs via `std::memset` (`#include <cstring>`) to eliminate compiler warnings.
  - Added guards in `ReadSectors` and `WriteSectors` validating `m_geometry.bytesPerSector > 0`.

### 3. `Erasure/File Systems/NTFS/NTFS.cpp`
* **Critical Memory Safety Bug in `FormatDrive`**:
  - *Previous code*: Allocated `secBuf` of size `bytesPerSec` (512 bytes on standard drives), then called `std::memcpy(secBuf.data() + offInSec, cleanRecord.data(), 1024)`.
  - *Impact*: Caused a catastrophic **512-byte heap memory corruption** on every MFT record written and only wrote half of the record to disk.
  - *Resolution*: Calculated `sectorsNeeded = (1024 + offInSec + bytesPerSec - 1) / bytesPerSec`, allocated `sectorsNeeded * bytesPerSec`, and performed full multi-sector reads and writes.
* **Runlist Length Underflow in `$INDEX_ALLOCATION` Decoding**:
  - *Previous code*: Lines 495, 611, and 860 passed `indexAllocAttr[1] - nonRes->dataRunsOffset` to `DecodeRunList()`. Since `indexAllocAttr[1]` accessed byte 1 of the attribute type (`0x00`), `0 - dataRunsOffset` underflowed to a massive positive `size_t`.
  - *Resolution*: Replaced with `(attrHdr->length > nonRes->dataRunsOffset) ? (attrHdr->length - nonRes->dataRunsOffset) : 0`.
* **Signed Shift Undefined Behavior in `DecodeRunList`**:
  - *Previous code*: Used `static_cast<int64_t>(0xFF) << (i * 8)`, shifting into the sign bit of `int64_t`.
  - *Resolution*: Sign-extended cleanly using unsigned bitmask `uint64_t mask = ~0ULL << (offsetFieldSize * 8)` and standard two's complement conversion.
* **Bounds Hardening**:
  - Enforced string offset and length bounds on UTF-16 attribute names in `FindAttribute`.
  - Validated resident attribute value length and offset bounds against both the attribute record and the MFT buffer in `GetFileAllocatedExtents`.

### 4. `Erasure/File Systems/exFAT/exFAT.cpp`
* **Directory Deletion & Directory Stream Truncation Bug**:
  - *Previous code*: Zeroing the entire 32-byte directory entry set with `std::memset` set `EntryType = 0x00`. Per Microsoft exFAT specification, `0x00` represents **EndOfDirectory**, terminating directory scanning immediately and rendering all subsequent files and folders in that directory inaccessible.
  - *Resolution*: In accordance with Microsoft exFAT spec §6.2.1.1, cleared bit 7 (`InUse = 0`, transforming e.g. `0x85` -> `0x05`, `0xC0` -> `0x40`, `0xC1` -> `0x41`) and zeroed the remaining 31 bytes of each entry. This securely obliterates file names, timestamps, cluster pointers, and data lengths while keeping the directory traversal chain intact.
* **Division-by-Zero Guards**:
  - Added guards against `m_bytesPerSector == 0` or `m_sectorsPerCluster == 0` in `ClearBitmapBit` and `GetClusterChain`.
* **VBR Parameter Validation in `Mount()`**:
  - Validated shift parameters per exFAT spec §3.1.5: `bytesPerSectorShift` must be in the range [9, 12] (512B to 4096B) and `sectorsPerClusterShift <= 25` (max cluster size 32MB), preventing undefined bit shift behavior on corrupted media.

### 5. `Erasure/File Systems/ext4/ext4.cpp`
* **Superblock Validation in `Mount()`**:
  - Enforced `s_log_block_size <= 6` (maximum 64KB block size per Linux ext4 kernel specifications) to prevent bit-shift overflow in `1024 << s_log_block_size`.
  - Verified `m_inodeSize <= m_blockSize` and `(m_blockSize % m_inodeSize) == 0` to prevent buffer overruns when computing inode block offsets.
  - Guarded against underflow in block group counting (`totalBlocks < s_first_data_block`).

### 6. `Tests/main.cpp`
* **Synthetic Test Flag Correction**:
  - In `RunExFatSyntheticTest`, `generalSecondaryFlags` was corrected from `0x01` to `0x03` (`AllocationPossible | NoFatChain`) per exFAT spec §6.3.5.2, aligning with `ExFatDriver`'s check `(flags & 0x02) != 0`.

---

## Comprehensive Documentation Index

The `.context/` directory contains specialized, exhaustive architectural manuals for every layer of the product:
1. **Shipping Formats, Electron App & Recovery Architecture**:
   * [SHIPPING_AND_APPLICATION_ARCHITECTURE.md](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/.context/SHIPPING_AND_APPLICATION_ARCHITECTURE.md) — Covers the 3 distribution formats (Windows .exe, Linux .deb/AppImage, Live Boot RAM-disk ISO to sanitize Windows C: and system boot drives), Electron frontend + native C++ backend JSON-RPC IPC architecture, and the complete 4-phase Data Recovery subsystem (`Recovery/`).
2. **Filesystem Execution Intensive Operations Manual**:
   * [FILESYSTEM_EXECUTION_INTENSIVE.md](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/.context/FILESYSTEM_EXECUTION_INTENSIVE.md) — Line-by-line, code-level execution walkthrough of all 4 filesystem drivers (NTFS, XFS, ext4, exFAT), extent decoding, directory B-tree traversal, on-disk metadata wiping, and 3-pass DoD sanitization.
3. **Multi-Filesystem Deep Dive & Forensic Matrix**:
   * [FILESYSTEMS_DEEP_DIVE.md](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/.context/FILESYSTEMS_DEEP_DIVE.md) — Architectural overview, comparative feature tables, and Shannon entropy forensic verification suite.
