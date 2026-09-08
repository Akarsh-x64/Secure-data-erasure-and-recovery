# Multi-Filesystem Secure Erasure Engine — Exhaustive Technical Execution Manual

This manual provides an in-depth, code-level, and forensic-grade explanation of the internal operations performed by every filesystem driver in this repository:
1. **NTFS Driver** (`Erasure/File Systems/NTFS/NTFS.h`, `NTFS.cpp`, `NTFS_Structures.h`)
2. **XFS Driver** (`Erasure/File Systems/XFS/XFS.h`, `XFS.cpp`, `XFS_Structures.h`)
3. **ext4 Driver** (`Erasure/File Systems/ext4/ext4.h`, `ext4.cpp`, `ext4_Structures.h`)
4. **exFAT Driver** (`Erasure/File Systems/exFAT/exFAT.h`, `exFAT.cpp`, `exFAT_Structures.h`)

It details the exact on-disk binary layouts, in-memory state tracking, mount routines, extent decoding mathematics, sector-level overwrites, metadata scrubbing, directory unlinking, recursive directory destruction, volume-wide surgical wipes, and anti-forensic safeguards.

---

# Table of Contents
1. [Core Engine Architecture & Universal Overwrite Standard](#1-core-engine-architecture--universal-overwrite-standard)
2. [NTFS Driver Internals (`NtfsDriver`)](#2-ntfs-driver-internals-ntfsdriver)
   - 2.1 [Volume Boot Record (VBR) & Geometry Decoding](#21-volume-boot-record-vbr--geometry-decoding)
   - 2.2 [Update Sequence Array (Fixup/USN) Mechanism](#22-update-sequence-array-fixupusn-mechanism)
   - 2.3 [Master File Table ($MFT) Layout & Multi-Fragment Extent Mapping](#23-master-file-table-mft-layout--multi-fragment-extent-mapping)
   - 2.4 [MFT Record Architecture (1024 Bytes) & Attribute Parsing](#24-mft-record-architecture-1024-bytes--attribute-parsing)
   - 2.5 [Runlist Decoding Engine (`DecodeRunList`)](#25-runlist-decoding-engine-decoderunlist)
   - 2.6 [Directory B-Tree Traversal (`$INDEX_ROOT` & `$INDEX_ALLOCATION`)](#26-directory-b-tree-traversal-index_root--index_allocation)
   - 2.7 [Volume Allocation Bitmap Manipulation (`$Bitmap`)](#27-volume-allocation-bitmap-manipulation-bitmap)
   - 2.8 [Single File Erasure Pipeline (`EraseFile`)](#28-single-file-erasure-pipeline-erasefile)
   - 2.9 [Recursive Directory Erasure (`EraseDirectory`)](#29-recursive-directory-erasure-erasedirectory)
   - 2.10 [Volume-Wide Sanitization & Drive Formatting (`WipeVolume` / `FormatDrive`)](#210-volume-wide-sanitization--drive-formatting-wipevolume--formatdrive)
3. [XFS Driver Internals (`XfsDriver`)](#3-xfs-driver-internals-xfsdriver)
   - 3.1 [Multi-Allocation Group (AG) Architecture & On-Disk Geometry](#31-multi-allocation-group-ag-architecture--on-disk-geometry)
   - 3.2 [Endianness & Big-Endian to CPU Unpacking](#32-endianness--big-endian-to-cpu-unpacking)
   - 3.3 [Address Translation Mathematics (`FsbToBlock` & `InoToByteOffset`)](#33-address-translation-mathematics-fsbtoblock--inotobyteoffset)
   - 3.4 [Inode Core (`XfsDinodeCore`) & Data Fork Architecture](#34-inode-core-xfsdinodecore--data-fork-architecture)
   - 3.5 [BMBT Extent Unpacking & Multi-Level Extent B+Trees](#35-bmbt-extent-unpacking--multi-level-extent-btrees)
   - 3.6 [Directory Formats: Shortform, Block/Extent, and Node B-Tree](#36-directory-formats-shortform-blockextent-and-node-b-tree)
   - 3.7 [Single File Erasure Pipeline (`EraseFile`)](#37-single-file-erasure-pipeline-erasefile)
   - 3.8 [Parent Directory Entry Sanitization (`WipeDirectoryEntry`)](#38-parent-directory-entry-sanitization-wipedirectoryentry)
   - 3.9 [Anti-Forensic Purges: AGI Unlinked Buckets & Intent Log (Journal)](#39-anti-forensic-purges-agi-unlinked-buckets--intent-log-journal)
   - 3.10 [Volume-Wide Surgical Wipe (`WipeVolume`)](#310-volume-wide-surgical-wipe-wipevolume)
4. [ext4 Driver Internals (`Ext4Driver`)](#4-ext4-driver-internals-ext4driver)
   - 4.1 [Block Group Architecture & Superblock Decoding](#41-block-group-architecture--superblock-decoding)
   - 4.2 [Group Descriptor Table (GDT) Management (32-bit vs 64-bit)](#42-group-descriptor-table-gdt-management-32-bit-vs-64-bit)
   - 4.3 [Inode Table Addressing Mathematics](#43-inode-table-addressing-mathematics)
   - 4.4 [Inode Architecture (`Ext4Inode`) & Extent Tree Hierarchy (`0xF30A`)](#44-inode-architecture-ext4inode--extent-tree-hierarchy-0xf30a)
   - 4.5 [Directory Traversal (`Ext4DirEntry2`) & Linked Record Traversal](#45-directory-traversal-ext4direntry2--linked-record-traversal)
   - 4.6 [Allocation Bitmap Manipulation (Block & Inode Bitmaps)](#46-allocation-bitmap-manipulation-block--inode-bitmaps)
   - 4.7 [Single File Erasure Pipeline (`EraseFile`)](#47-single-file-erasure-pipeline-erasefile)
   - 4.8 [Parent Directory Unlinking & `rec_len` Preservation](#48-parent-directory-unlinking--rec_len-preservation)
   - 4.9 [Volume-Wide Wipe (`WipeVolume`)](#49-volume-wide-wipe-wipevolume)
5. [exFAT Driver Internals (`ExFatDriver`)](#5-exfat-driver-internals-exfatdriver)
   - 5.1 [Volume Boot Record (VBR) Layout & Exponent Decoding](#51-volume-boot-record-vbr-layout--exponent-decoding)
   - 5.2 [Microsoft exFAT Specification Checksum Algorithms](#52-microsoft-exfat-specification-checksum-algorithms)
   - 5.3 [Cluster Heap Addressing Mathematics](#53-cluster-heap-addressing-mathematics)
   - 5.4 [32-Byte Directory Entry Sets (Primary `0x85`, Secondary `0xC0`, Secondary `0xC1`)](#54-32-byte-directory-entry-sets-primary-0x85-secondary-0xc0-secondary-0xc1)
   - 5.5 [Dual Allocation Tracking: Allocation Bitmap vs FAT Table](#55-dual-allocation-tracking-allocation-bitmap-vs-fat-table)
   - 5.6 [Single File Erasure Pipeline (`EraseFile`)](#56-single-file-erasure-pipeline-erasefile)
   - 5.7 [Spec §6.2.1.1 Directory Deletion (`0x85` $\to$ `0x05`) vs `0x00` EndOfDirectory](#57-spec-6211-directory-deletion-0x85-to-0x05-vs-0x00-endofdirectory)
   - 5.8 [Volume-Wide Surgical Wipe (`WipeVolume`)](#58-volume-wide-surgical-wipe-wipevolume)
6. [Cross-Filesystem Comparative Matrix](#6-cross-filesystem-comparative-matrix)
7. [Forensic Verification & Inspection Engine](#7-forensic-verification--inspection-engine)

---

# 1. Core Engine Architecture & Universal Overwrite Standard

## 1.1 The Decoupled 3-Layer Architecture

The engine strictly enforces architectural boundaries to guarantee that filesystem drivers are completely portable and decoupled from operating system APIs and hardware device commands:

```
┌──────────────────────────────────────────────────────────────────────────────────────────┐
│                                 LAYER 3: FILESYSTEM DRIVER                               │
│       NtfsDriver        │        XfsDriver        │       Ext4Driver    │  ExFatDriver   │
│  - VBR / Superblock     │  - Allocation Groups    │  - Block Groups     │  - VBR / Shifts│
│  - Runlists / Extents   │  - BMBT Extent Trees    │  - Extent Trees     │  - FAT Chains  │
│  - Directory B-Trees    │  - Shortform / Leaf Dir │  - Dir Linked Lists │  - Entry Sets  │
│  - MFT Record Wiping    │  - Dinode Obliteration  │  - Inode Zeroing    │  - Bit 7 Clear │
└─────────────────────────────────────────┬────────────────────────────────────────────────┘
                                          │ Directs physical LBA sector operations
┌─────────────────────────────────────────▼────────────────────────────────────────────────┐
│                               LAYER 2: HARDWARE CONTROLLER                               │
│      HDDController (Magnetic/VDisk)     │     NVMeController     │     ATAController     │
│  - 3-Pass DoD 5220.22-M Overwrite       │  - NVMe Dataset Mgmt   │  - ATA Secure Erase   │
│  - Pass 1: 0x00 (Zero Ground)           │    (Deallocate / TRIM) │  - Sanitize Command   │
│  - Pass 2: 0xFF (Positive Saturation)   │  - Format NVM Command  │  - Crypto Scramble    │
│  - Pass 3: PRNG Gibberish (mt19937_64)  │  - Wear-leveling bypass│                       │
└─────────────────────────────────────────┬────────────────────────────────────────────────┘
                                          │ Issues raw sector read/write commands
┌─────────────────────────────────────────▼────────────────────────────────────────────────┐
│                                  LAYER 1: OS STORAGE PIPE                                │
│                WindowsStorageDevice               │           LinuxStorageDevice         │
│  - CreateFileA ("\\\\.\\PhysicalDriveX")          │  - open ("/dev/sdX", O_DIRECT|O_SYNC)│
│  - SetFilePointerEx + ReadFile / WriteFile        │  - pread64 / pwrite64                │
│  - FSCTL_LOCK_VOLUME / FSCTL_DISMOUNT_VOLUME      │  - BLKFLSBUF / ioctl BLKRRPART       │
│  - IOCTL_DISK_GET_DRIVE_GEOMETRY_EX               │  - BLKGETSIZE64                      │
└──────────────────────────────────────────────────────────────────────────────────────────┘
```

## 1.2 The Universal Sanitization Standard: 3-Pass DoD 5220.22-M Overwrite

Whenever data clusters, block extents, or whole drives are erased, `HDDController` executes an authenticated 3-pass DoD overwrite sequence:

```
Sector Buffer (Bounded to 1024 sectors / 512 KB chunks)
Pass 1: [ 0x00, 0x00, 0x00, 0x00, 0x00 ... ]  -->  Forces magnetic domains to baseline zero polarity
Pass 2: [ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF ... ]  -->  Inverts magnetic domains to maximum positive saturation
Pass 3: [ PRNG Gibberish via std::mt19937_64] -->  Eliminates magnetic remanence (hysteresis) & MFM recovery
```

### Chunked I/O Buffer Mechanics
To prevent system out-of-memory crashes when sanitizing multi-gigabyte files or multi-terabyte drives, `HDDController::SecureEraseSectors` streams writes in chunks bounded to `CHUNK_SECTORS = 1024` sectors (512 KB):
```cpp
const uint32_t CHUNK_SECTORS = 1024;
std::vector<uint8_t> buffer(CHUNK_SECTORS * sectorSize);
uint64_t sectorsRemaining = count;
uint64_t currentSector = startSector;

while (sectorsRemaining > 0) {
    uint32_t toWrite = static_cast<uint32_t>(std::min<uint64_t>(sectorsRemaining, CHUNK_SECTORS));
    // Pass 1: 0x00
    std::memset(buffer.data(), 0x00, toWrite * sectorSize);
    m_device->WriteSectors(currentSector, toWrite, buffer.data());
    // Pass 2: 0xFF
    std::memset(buffer.data(), 0xFF, toWrite * sectorSize);
    m_device->WriteSectors(currentSector, toWrite, buffer.data());
    // Pass 3: PRNG Gibberish
    for (size_t i = 0; i < (toWrite * sectorSize) / sizeof(uint64_t); ++i) {
        reinterpret_cast<uint64_t*>(buffer.data())[i] = rng();
    }
    m_device->WriteSectors(currentSector, toWrite, buffer.data());
    sectorsRemaining -= toWrite;
    currentSector += toWrite;
}
```

---

# 2. NTFS Driver Internals (`NtfsDriver`)

## 2.1 Volume Boot Record (VBR) & Geometry Decoding

NTFS stores its boot record at Sector 0 (LBA 0). The driver validates the signature and dynamically extracts all geometric parameters:

```
LBA 0: NtfsBootSector (512 Bytes)
┌────────────┬─────────────┬─────────────┬─────────────┬──────────────┬──────────────┬──────────────┬──────────────┐
│ Jump Code  │ OEM ID      │ Bytes / Sec │ Sec / Clust │ TotalSectors │ mftStartLCN  │ mftMirrLCN   │ Boot Magic   │
│ 0xEB 0x52  │ "NTFS    "  │ 512 (0x200) │ 8 (0x08)    │ 64-bit int   │ 64-bit int   │ 64-bit int   │ 0xAA55       │
│ [Bytes 0-2]│ [Bytes 3-10]│[Bytes 11-12]│ [Byte 13]   │[Bytes 40-47] │[Bytes 48-55] │[Bytes 56-63] │[Bytes 510-511│
└────────────┴─────────────┴─────────────┴─────────────┴──────────────┴──────────────┴──────────────┴──────────────┘
```

### Cluster Size & Record Size Calculation
* **Cluster Size**:
  $$\text{m\_bytesPerCluster} = \text{bytesPerSector} \times \text{sectorsPerCluster}$$
* **MFT Record Size (`clustersPerMftRecord`)**:
  * Stored as a signed 8-bit integer (`int8_t`).
  * If positive ($> 0$): $\text{RecordSize} = \text{clustersPerMftRecord} \times \text{m\_bytesPerCluster}$.
  * If negative ($< 0$, standard NTFS is `-10`):
    $$\text{RecordSize} = 2^{|\text{clustersPerMftRecord}|} = 2^{|-10|} = 1024\text{ bytes}$$
* **Index Buffer Size (`clustersPerIndexBuffer`)**:
  * If negative (standard NTFS is `-12`):
    $$\text{IndexSize} = 2^{|\text{clustersPerIndexBuffer}|} = 2^{|-12|} = 4096\text{ bytes}$$

## 2.2 Update Sequence Array (Fixup/USN) Mechanism

To detect torn writes caused by power failures during sector commits, NTFS replaces the last two bytes of every 512-byte sector in an MFT record (at offset 510 and 1022) with a 2-byte Update Sequence Number (USN). The original bytes are saved in the record header's fixup array.

```
MFT Record (1024 Bytes)
┌───────────────────────────────────────────────┬────────────┬──────────────────────────────────────┬────────────┐
│ Sector 0 Data (Bytes 0..509)                  │ USN Check  │ Sector 1 Data (Bytes 512..1021)      │ USN Check  │
│                                               │ (Offset 510│                                      │ (Offset 102│
└───────────────────────────────────────────────┴─────┬──────┴──────────────────────────────────────┴─────┬──────┘
                                                      │                                                   │
  Header Fixup Array [Offset 0x30]:                   │                                                   │
  [ USN Word: 0x1234 ] ───────────────────────────────┴───────────────────────────────────────────────────┘
  [ Saved Word 0: 0xABCD ] ---> Replaces offset 510 during read (ApplyFixup)
  [ Saved Word 1: 0xEF01 ] ---> Replaces offset 1022 during read (ApplyFixup)
```

### The `ApplyFixup()` Algorithm
Before any MFT record or Index block can be parsed:
1. Validates magic is `"FILE"` (`0x454C4946`) or `"INDX"` (`0x58444E49`).
2. Reads the 2-byte `usn` at `hdr->updateSequenceOffset`.
3. For each sector $i \in [0, \text{usnCount} - 2]$:
   * Calculates $\text{endOffset} = (i + 1) \times 512 - 2$.
   * Asserts `*(uint16_t*)(buffer + endOffset) == usn`.
   * Restores original bytes: `*(uint16_t*)(buffer + endOffset) = fixupArray[i]`.

## 2.3 Master File Table ($MFT) Layout & Multi-Fragment Extent Mapping

Everything in NTFS is tracked in `$MFT`. The first 16 records are reserved for core filesystem metadata:

| Record # | System File | Purpose & Contents |
|---|---|---|
| **0** | `$MFT` | Master File Table itself (contains its own extent runlist) |
| **1** | `$MFTMirr` | Redundant backup of records 0, 1, 2, 3 |
| **2** | `$LogFile` | Transactional journal logging metadata updates |
| **3** | `$Volume` | Volume label, filesystem version, dirty bit |
| **4** | `$AttrDef` | Attribute definitions table |
| **5** | `.` (Root) | Root directory B-tree index anchor |
| **6** | `$Bitmap` | Cluster allocation bitmap (1 bit per volume cluster) |
| **7** | `$Boot` | Volume boot sector duplication |
| **8** | `$BadClust` | List of unreadable/damaged disk clusters |
| **9** | `$Secure` | Access Control Lists (ACLs) and security descriptors |
| **10** | `$UpCase` | Unicode uppercase character translation table |
| **11** | `$Extend` | Extension directory for quota, USN journals, object IDs |
| **16+** | User Files | Regular user files, directories, and data streams |

### Multi-Fragment Extent Mapping (`m_mftExtents`)
Because the `$MFT` can grow and fragment across non-contiguous clusters during the disk's lifetime, `NtfsDriver` cannot assume `$MFT` is contiguous:
1. During `Mount()`, it temporarily maps Record 0 using `mftStartLCN` from the VBR.
2. It parses Record 0's `$DATA` attribute and extracts its full extent runlist into `m_mftExtents`.
3. Subsequent MFT record reads translate `recordNum * 1024` through `m_mftExtents`, guaranteeing accurate LBA sector resolution even across thousands of MFT fragments.

## 2.4 MFT Record Architecture (1024 Bytes) & Attribute Parsing

Every MFT record begins with `NtfsRecordHeader`:
* `magic`: `"FILE"` (`0x454C4946`). When sanitized, set to `0x00000000`.
* `firstAttributeOffset`: Byte offset where the attribute stream begins (typically `0x38`).
* `flags`: `0x0001` (`FILE_RECORD_IN_USE`), `0x0002` (`FILE_RECORD_DIRECTORY`). Set to `0x0000` when deleted.

### Attribute Sequence Processing (`FindAttribute`)
Attributes are sequentially packed and terminated by `0xFFFFFFFF` (`ATTR_END`):
* **Common Attribute Header (`NtfsAttributeHeader`)**:
  * `type`: Type code (`0x10` = `$STANDARD_INFORMATION`, `0x30` = `$FILE_NAME`, `0x80` = `$DATA`, `0x90` = `$INDEX_ROOT`, `0xA0` = `$INDEX_ALLOCATION`).
  * `length`: Total byte length of this attribute record.
  * `nonResidentFlag`: `0` = Resident (stored in MFT record), `1` = Non-Resident (stored in external clusters).

### Resident vs Non-Resident `$DATA`
* **Resident (`nonResidentFlag == 0`)**:
  * Followed by `NtfsResidentAttributeHeader`.
  * Data payload begins at `attrPtr + res->valueOffset` and spans `res->valueLength` bytes (typically $\le 700$ bytes).
* **Non-Resident (`nonResidentFlag == 1`)**:
  * Followed by `NtfsNonResidentAttributeHeader`.
  * External clusters are described by variable-length **Data Runs** located at `attrPtr + nonRes->dataRunsOffset`.

## 2.5 Runlist Decoding Engine (`DecodeRunList`)

NTFS compresses non-resident cluster allocations into packed byte streams. Each run has a 1-byte header:
$$\text{lenFieldSize} = \text{header} \ \& \ \text{0x0F}, \quad \text{offsetFieldSize} = (\text{header} \gg 4) \ \& \ \text{0x0F}$$

```
Runlist Byte Stream Example: [ 0x32, 0x01, 0x04, 0x80, 0x12, 0x00 ]
  Header Byte 0x32:
    - lenFieldSize    = 0x2 (Next 2 bytes are cluster count)
    - offsetFieldSize = 0x3 (Following 3 bytes are relative LCN offset)
  Length: 0x0401 (1025 clusters)
  Offset: 0x001280 (Relative LCN = +4736)
```

### Signed Two's Complement Extension
NTFS cluster offsets are relative to the previous run's starting cluster and can be **negative** (fragment allocated backwards on disk). The decoder tests the highest bit of the offset byte:
```cpp
if (offsetFieldSize > 0 && (runlist[offset - 1] & 0x80)) {
    uint64_t mask = ~0ULL << (offsetFieldSize * 8);
    lcnOffset = static_cast<int64_t>(static_cast<uint64_t>(lcnOffset) | mask);
}
currentLcn += lcnOffset;
outExtents.push_back({ static_cast<uint64_t>(currentLcn), clusterCount });
```

## 2.6 Directory B-Tree Traversal (`$INDEX_ROOT` & `$INDEX_ALLOCATION`)

NTFS organizes directory entries into a balanced B-Tree:
* **Small Directories**: Entries reside wholly inside `$INDEX_ROOT` (`0x90`) within the directory's MFT record.
* **Large Directories**: Entries spill into `$INDEX_ALLOCATION` (`0xA0`), a stream of 4096-byte `'INDX'` (`0x58444E49`) blocks indexed by Virtual Cluster Numbers (VCNs).

```
Directory MFT Record
┌───────────────────────────────────────┐
│ $INDEX_ROOT (0x90)                    │
│   NtfsIndexRootHeader                 │
│   NtfsIndexHeader                     │
│     NtfsIndexEntry [File: "Alpha.txt"]│
│     NtfsIndexEntry [SubNode VCN: 0]   │──┐
└───────────────────────────────────────┘  │
                                           │
  $INDEX_ALLOCATION (0xA0) Runlist         │
  ┌────────────────────────────────────────▼──────────────────────────────┐
  │ LCN 1000: INDX Block 0 (4096 Bytes)                                   │
  │   NtfsIndexBlock ("INDX") + Fixup Array                               │
  │   NtfsIndexHeader                                                     │
  │     NtfsIndexEntry [File: "Beta.docx",  MFT Record: 34]               │
  │     NtfsIndexEntry [File: "Gamma.pdf",  MFT Record: 35]               │
  │     NtfsIndexEntry [SubNode VCN: 1,     Flag: INDEX_ENTRY_LAST]       │
  └───────────────────────────────────────────────────────────────────────┘
```

## 2.7 Volume Allocation Bitmap Manipulation (`$Bitmap`)

NTFS tracks cluster allocation in Record 6 (`$Bitmap`):
* Cluster $C$ corresponds to byte index $B = \lfloor C / 8 \rfloor$ and bit mask $M = 1 \ll (C \pmod 8)$.
* `ClearClusterBitmapBit(lcn)`:
  1. Reads `$Bitmap`'s `$DATA` runlist to locate the physical LBA sector holding byte $B$.
  2. Reads the sector into RAM.
  3. Clears the bit: `sectorBuf[offsetInSector] &= ~(1 << (lcn % 8))`.
  4. Flushes the updated sector back to disk.

## 2.8 Single File Erasure Pipeline (`EraseFile`)

The execution pipeline for `EraseFile(relativePath)` operates as follows:

```
[Target Path] ──> TokenizePath() ──> Directory B-Tree Traversal from Record 5 (.)
                        │
                        ▼
            Target MFT Record Located
                        │
       ┌────────────────┴────────────────┐
       ▼                                 ▼
Non-Resident Data Runs?           Resident Payload?
  - Extract Extents                 - Stored inside 1024-byte MFT Record
  - ClusterToSector(LCN)            - (Obliterated during MFT Wipe)
  - 3-Pass DoD Overwrite
    (0x00 -> 0xFF -> PRNG)
  - ClearClusterBitmapBit(LCN)
       │                                 │
       └────────────────┬────────────────┘
                        │
                        ▼
   WipeMftRecordOnDisk(recordNum)
     - Physical Sector Overwrite of all 1024 bytes with 0x00
     - Magic "FILE" -> 0x00000000
     - Flags, Timestamps, Runlists -> Zeroed
                        │
                        ▼
   ScrubDirectoryEntry(parentRecordNum, fileName)
     - Traverses parent $INDEX_ROOT and $INDEX_ALLOCATION
     - Zeroes file reference, filename characters, and attributes
     - Updates index entries used length
                        │
                        ▼
   Journal & USN Sweep
     - Purges references in $LogFile and $UsnJrnl
```

## 2.9 Recursive Directory Erasure (`EraseDirectory`)

1. Resolves directory MFT record number via B-Tree traversal.
2. Calls `ListDirectoryContents(dirRecordNum)` to enumerate all child records.
3. Recursively executes `EraseDirectoryRecursive(dirRecordNum)`:
   * **Depth-First Descent**: Descends into child directories first.
   * **Leaf File Eradication**: For each child file, wipes non-resident cluster extents with 3-Pass DoD, zeroes cluster bitmap bits, and overwrites the 1024-byte MFT record with `0x00`.
   * **Index Allocation Freeing**: Locates and sanitizes the directory's own `$INDEX_ALLOCATION` non-resident cluster runs.
   * **Directory Inode Obliteration**: Overwrites the directory's own 1024-byte MFT record with `0x00`.
   * **Parent Entry Unlink**: Removes the directory reference from its parent's B-Tree index.

## 2.10 Volume-Wide Sanitization & Drive Formatting (`WipeVolume` / `FormatDrive`)

* **`WipeVolume()` (Surgical Wipe)**:
  1. Quarantines critical system records: Records 0 through 15 ($MFT, $MFTMirr, $LogFile, $Volume, $AttrDef, Root `.`, $Bitmap, $Boot, etc.).
  2. Scans `$Bitmap` and commands `SecureEraseSectors()` (3-pass DoD) on all allocated user clusters.
  3. Clears user allocation bits in `$Bitmap`.
  4. Resets the root directory index to an empty state.
* **`FormatDrive(fullDriveSanitize)`**:
  1. If `fullDriveSanitize` is requested: Executes 3-pass DoD overwriting across the **entire LBA capacity of the disk**.
  2. Re-writes pristine `NtfsBootSector` at Sector 0 with valid OEM signature `"NTFS    "` and `0xAA55`.
  3. Zero-initializes `$Bitmap` runlist.
  4. Writes freshly initialized standard MFT records 0 through 15 with valid `"FILE"` headers, and sets records 16 through `MFT_REC_USER_START` to unallocated (`flags = 0`).
  5. Re-mounts the freshly formatted volume.

---

# 3. XFS Driver Internals (`XfsDriver`)

## 3.1 Multi-Allocation Group (AG) Architecture & On-Disk Geometry

XFS splits partition storage into autonomous, parallel regions called **Allocation Groups (AGs)**:
* `sb_agcount`: Total number of AGs.
* `sb_agblocks`: Size of each AG in filesystem blocks.
* Every AG contains its own independent structural headers in its first 4 sectors:

```
Allocation Group N Structure (Sectors 0..3)
┌──────────────────────┬──────────────────────┬──────────────────────┬──────────────────────┬────────────────────────┐
│ Sector 0: Superblock │ Sector 1: AGF        │ Sector 2: AGI        │ Sector 3: AGFL       │ Remaining Blocks:      │
│ "XFSB" (0x58465342)  │ "XAGF" (0x58414746)  │ "XAGI" (0x58414749)  │ "XAFL" (0x5841464C)  │ B+Tree Nodes, Inodes,  │
│ Magic, geometry, logs│ Free space B+Trees   │ Inode B+Trees &      │ Free list blocks     │ Data Extents           │
│                      │ (bno_cur, cnt_cur)   │ unlinked hash buckets│                      │                        │
└──────────────────────┴──────────────────────┴──────────────────────┴──────────────────────┴────────────────────────┘
```

## 3.2 Endianness & Big-Endian to CPU Unpacking

All on-disk XFS structures are formatted in **Big-Endian (Network Byte Order)**. The driver runs on little-endian host CPUs and converts every field using inline byte-swapping:
* `be16_to_cpu(val)`: `((val >> 8) & 0xFF) | ((val & 0xFF) << 8)`
* `be32_to_cpu(val)`: Bit-shift reversal of 4 bytes
* `be64_to_cpu(val)`: Bit-shift reversal of 8 bytes

## 3.3 Address Translation Mathematics (`FsbToBlock` & `InoToByteOffset`)

### FSB to Disk Block Translation (`FsbToBlock`)
In XFS extent records and B+Tree pointers, a Filesystem Block Number (`fsbno`) packs the AG index into its high bits:
$$\text{agno} = \text{fsbno} \gg \text{sb\_agblklog}$$
$$\text{agbno} = \text{fsbno} \ \& \ ((1\text{ULL} \ll \text{sb\_agblklog}) - 1)$$
$$\text{linear\_block} = (\text{agno} \times \text{sb\_agblocks}) + \text{agbno}$$

### 64-Bit Inode Number Translation (`InoToByteOffset`)
An XFS inode number encodes the AG index, AG block number, and offset within the block:
$$\text{agno} = \text{ino} \gg (\text{sb\_inopblog} + \text{sb\_agblklog})$$
$$\text{agbno} = (\text{ino} \gg \text{sb\_inopblog}) \ \& \ ((1\text{ULL} \ll \text{sb\_agblklog}) - 1)$$
$$\text{offsetInBlock} = (\text{ino} \ \& \ ((1\text{ULL} \ll \text{sb\_inopblog}) - 1)) \times \text{sb\_inodesize}$$
$$\text{absBlock} = (\text{agno} \times \text{sb\_agblocks}) + \text{agbno}$$
$$\text{physicalByteOffset} = (\text{absBlock} \times \text{blockSize}) + \text{offsetInBlock}$$

## 3.4 Inode Core (`XfsDinodeCore`) & Data Fork Architecture

Every XFS inode begins with `XfsDinodeCore` (100 bytes for v2, 176 bytes for v3 CRC):
* `di_magic`: `"IN"` (`0x494E`).
* `di_mode`: POSIX file type (`0x8000` = Regular, `0x4000` = Directory) and permissions.
* `di_format`: Defines how data is stored in the data fork immediately following the core:
  * **`XFS_DINODE_FMT_LOCAL` (`1`)**: Payload stored inline (Shortform directory or fast symlink).
  * **`XFS_DINODE_FMT_EXTENTS` (`2`)**: Array of 16-byte packed extent records (`XfsBmbtRec`).
  * **`XFS_DINODE_FMT_BTREE` (`3`)**: Extent list exceeds inode data fork capacity. Root of B+Tree (`XfsBmdrBlock`) resides in data fork; points to external B+Tree blocks.

## 3.5 BMBT Extent Unpacking & Multi-Level Extent B+Trees

Extent records (`XfsBmbtRec`) are 128-bit bitpacked structures:

```
XfsBmbtRec: 128 Bits
┌───────┬───────────────────────────────────┬─────────────────────────────────────┬──────────────────┐
│ Bit 127│ Bits [126..73] (54 Bits)          │ Bits [72..21] (52 Bits)             │ Bits [20..0] (21)│
│ State │ Logical File Block Offset (startoff) Physical Block Number / FSB (startblock) Block Count   │
└───────┴───────────────────────────────────┴─────────────────────────────────────┴──────────────────┘
```

### Multi-Level B+Tree Traversal
If `di_format == XFS_DINODE_FMT_BTREE`:
1. Reads `XfsBmdrBlock` from the inode's data fork.
2. Extracts level depth `bb_level` and pointer array `XfsBmdrPtr32`.
3. Recursively reads child B+Tree blocks from disk, queuing all intermediate B+Tree metadata block addresses in a tracking list `btreeBlocks`.
4. Decodes leaf extent records to obtain all user data block allocations.

## 3.6 Directory Formats: Shortform, Block/Extent, and Node B-Tree

1. **Shortform (`LOCAL`)**: Entries packed directly in the inode data fork. Begins with `XfsDir2SfHdr` (count, parent inode), followed by entries with name length, name characters, and 4-byte or 8-byte inode numbers.
2. **Block / Extent (`EXTENTS`)**: Directory spans 1 or more external filesystem blocks:
   * Header `XfsDir2DataHdr` (`"XD2B"` / `0x58443242`).
   * Chain of `XfsDir2DataEntry` structs and `XfsDir2DataUnused` free space tags.
3. **Node B-Tree (`BTREE`)**: 32-bit hashes of filenames (`XfsDaIntnode`, magic `0xFEBE`) index into leaf directory blocks.

## 3.7 Single File Erasure Pipeline (`EraseFile`)

The erasure pipeline for XFS obliterates both data blocks and B+Tree structural metadata:

```
[Target Path] ──> Directory Search (Shortform / Block / Node B-Tree) ──> Target Inode
                                                                                │
                                                                                ▼
                                                                 ReadInode(ino, core, fork)
                                                                                │
                                                                                ▼
                                                                 GetInodeExtents(ino, extents, btreeBlocks)
                                                                                │
                                   ┌────────────────────────────────────────────┴────────────────────────────────────────────┐
                                   ▼                                                                                         ▼
               Physical Data Extent Obliteration                                                         Intermediate B+Tree Metadata Block Obliteration
               For each extent in extents:                                                               For each block in btreeBlocks:
                 - block = FsbToBlock(startblock)                                                          - sector = BlockToSector(block)
                 - sector = BlockToSector(block)                                                           - 3-Pass DoD Overwrite
                 - 3-Pass DoD Overwrite                                                                      (0x00 -> 0xFF -> PRNG)
                   (0x00 -> 0xFF -> PRNG)                                                                (Eradicates all B+Tree branch nodes)
                                   │                                                                                         │
                                   └────────────────────────────────────────────┬────────────────────────────────────────────┘
                                                                                │
                                                                                ▼
                                                                 WipeInodeOnDisk(ino)
                                                                   - Reads sector containing inode
                                                                   - Overwrites all m_inodeSize bytes with 0x00
                                                                   - Destroys "IN" magic, mode, timestamps, and data fork
                                                                                │
                                                                                ▼
                                                                 WipeDirectoryEntry(parentIno, fileName)
                                                                   - Shortform: shifts subsequent entries, decrements count
                                                                   - Block/Extent: converts entry to XfsDir2DataUnused, zeroes name
                                                                                │
                                                                                ▼
                                                                 ScrubAgiUnlinkedBucket(ino)
                                                                   - Purges inode from Sector 2 agi_unlinked[64] hash table
                                                                                │
                                                                                ▼
                                                                 ScrubJournalForInode(ino, fileName)
                                                                   - Scans circular log buffer at sb_logstart
                                                                   - Zeroes matching transaction records
```

## 3.8 Parent Directory Entry Sanitization (`WipeDirectoryEntry`)

* **Shortform Directories**:
  * Locates entry inside the inode data fork.
  * Shifts all subsequent entries forward to overwrite the target entry.
  * Zero-fills the vacated trailing bytes.
  * Decrements `count8` or `count4` in `XfsDir2SfHdr`.
  * Writes the modified inode back to disk.
* **Block / Extent Directories**:
  * Converts the entry into an `XfsDir2DataUnused` structure.
  * Sets `freetag = 0xFFFF`.
  * Overwrites all filename characters with `0x00`.
  * Preserves length tags so directory parsers can skip over the free space without corruption.

## 3.9 Anti-Forensic Purges: AGI Unlinked Buckets & Intent Log (Journal)

* **AGI Unlinked Bucket Purge (`ScrubAgiUnlinkedBucket`)**:
  * In XFS, unlinked files with open handles are placed in Sector 2's `agi_unlinked[64]` hash buckets.
  * Forensic tools inspect these buckets to resurrect deleted inodes.
  * `ScrubAgiUnlinkedBucket` scans all 64 buckets and replaces any references to the deleted inode with `XFS_AGI_UNLINKED_NULL` (`0xFFFFFFFF`).
* **Intent Log (Journal) Scrubbing (`ScrubJournalForInode`)**:
  * Reads `sb_logblocks` starting at `sb_logstart`.
  * Scans log transaction headers for matching inode numbers or filename strings.
  * Overwrites matching transaction payload buffers with zeros, preventing journal replay or timeline reconstruction via tools like `xfs_logprint`.

## 3.10 Volume-Wide Surgical Wipe (`WipeVolume`)

1. **Quarantine Mapping**:
   * Quarantines Sectors 0 through 3 (Superblock, AGF, AGI, AGFL) across every AG.
   * Quarantines Root Inode (Inode 64 or 128) and its directory blocks.
   * Quarantines Journal blocks starting at `sb_logstart`.
2. **Carpet-Bombing**: Overwrites every non-quarantined block across the volume with 3-Pass DoD sanitization.
3. **Pristine State Restoration**: Re-initializes a clean empty root directory and zero-fills transaction log blocks.

---

# 4. ext4 Driver Internals (`Ext4Driver`)

## 4.1 Block Group Architecture & Superblock Decoding

ext4 organizes storage into contiguous **Block Groups** (typically 32,768 blocks = 128 MB per group):
* **Superblock (1024 Bytes)**: Located at byte offset 1024 (Sector 2 on 512-byte disks):
  * `s_magic`: `0xEF53`.
  * `s_log_block_size`: Block size $= 1024 \ll \text{s\_log\_block\_size}$ (typically 4096 bytes).
  * `s_blocks_per_group`: Blocks per group (typically 32768).
  * `s_inodes_per_group`: Inodes per group (typically 8192).
  * `s_inode_size`: Size of on-disk inode structure (standard is 256 bytes).
  * `s_first_data_block`: 0 if block size $>1024$, 1 if block size $=1024$.

```
Partition Layout across Block Groups:
┌────────────────────────┬────────────────────────┬────────────────────────┐
│  Block Group 0         │  Block Group 1         │  Block Group N         │
├──────┬──────┬────┬────┬┤──────┬──────┬────┬────┬┤──────┬──────┬────┬────┬┤
│Boot  │Super │GDT │BB  ││Super │GDT   │BB  │IB  ││Super │GDT   │BB  │IB  │
│(1KB) │(1KB) │    │    ││Backup│Backup│    │    ││Backup│Backup│    │    │
└──────┴──────┴────┴────┴┴──────┴──────┴────┴────┴┴──────┴──────┴────┴────┴┘
  BB = Block Bitmap (1 block), IB = Inode Bitmap (1 block), IT = Inode Table
```

## 4.2 Group Descriptor Table (GDT) Management (32-bit vs 64-bit)

Immediately following the superblock (Block 1 or 2), the Group Descriptor Table describes every block group:
* If `EXT4_FEATURE_INCOMPAT_64BIT` is set: Descriptors are 64 bytes (`Ext4GroupDesc64`).
* Otherwise: Descriptors are 32 bytes (`Ext4GroupDesc`).
* Each descriptor provides:
  * `bg_block_bitmap`: Block number of the Block Allocation Bitmap.
  * `bg_inode_bitmap`: Block number of the Inode Allocation Bitmap.
  * `bg_inode_table`: Starting block of this group's Inode Table.

## 4.3 Inode Table Addressing Mathematics

To read or wipe on-disk inode number $N$:
$$\text{group} = (N - 1) / \text{s\_inodes\_per\_group}$$
$$\text{indexInGroup} = (N - 1) \ \% \ \text{s\_inodes\_per\_group}$$
$$\text{byteOffsetInTable} = \text{indexInGroup} \times \text{s\_inode\_size}$$
$$\text{itableStart} = \text{GetInodeTableBlock}(\text{group})$$
$$\text{inodeBlock} = \text{itableStart} + (\text{byteOffsetInTable} / \text{blockSize})$$
$$\text{offsetInBlock} = \text{byteOffsetInTable} \ \% \ \text{blockSize}$$

## 4.4 Inode Architecture (`Ext4Inode`) & Extent Tree Hierarchy (`0xF30A`)

On-disk inode size is `s_inode_size` (256 bytes). The core header is 128 bytes, and the remaining 128 bytes store extended attributes or inline data (`EXT4_INLINE_DATA_FL`).
* `i_flags`: If `0x00080000` (`EXT4_EXTENTS_FL`) is set, `i_block[60]` stores the root of an Extent Tree.

```
Extent Tree Node Header (Ext4ExtentHeader - 12 Bytes)
┌──────────────────────┬──────────────────────┬──────────────────────┬──────────────────────┐
│ eh_magic             │ eh_entries           │ eh_max               │ eh_depth             │
│ 0xF30A               │ Valid count          │ Max entries capacity │ 0 = Leaf, >0 = Index │
└──────────────────────┴──────────────────────┴──────────────────────┴──────────────────────┘
```

### Depth 0: Leaf Extent (`Ext4Extent` - 12 Bytes)
* `ee_block`: Logical block offset in the file.
* `ee_len`: Contiguous block count (if $\le 32768$, initialized; if $>32768$, unwritten extent).
* `ee_start_hi` & `ee_start_lo`: 48-bit physical block address on disk.

### Depth > 0: Index Node (`Ext4ExtentIdx` - 12 Bytes)
* Points to an external filesystem block containing a child `Ext4ExtentHeader`. The driver recursively traverses index blocks via `CollectExtentBlocks()`.

## 4.5 Directory Traversal (`Ext4DirEntry2`) & Linked Record Traversal

Directories in ext4 store a linked list of variable-length directory entries across their allocated data blocks:

```
Directory Block (4096 Bytes)
┌───────────────────────────────────────┬───────────────────────────────────────┬──────────────────────┐
│ Ext4DirEntry2: "."                    │ Ext4DirEntry2: ".."                   │ Ext4DirEntry2: "Doc" │
│   inode = 2                           │   inode = 2                           │   inode = 14         │
│   rec_len = 12                        │   rec_len = 12                        │   rec_len = 4072     │
│   name_len = 1, file_type = 2         │   name_len = 2, file_type = 2         │   name_len = 3       │
│   name = "."                          │   name = ".."                         │   name = "Doc"       │
└───────────────────────────────────────┴───────────────────────────────────────┴──────────────────────┘
```
* `rec_len`: Displacement in bytes to the start of the next entry. The last entry's `rec_len` spans to the end of the block.

## 4.6 Allocation Bitmap Manipulation (Block & Inode Bitmaps)

* **Block Bitmap Freeing (`ClearBlockBitmapBit`)**:
  $$\text{group} = (\text{block} - \text{s\_first\_data\_block}) / \text{s\_blocks\_per\_group}$$
  $$\text{index} = (\text{block} - \text{s\_first\_data\_block}) \ \% \ \text{s\_blocks\_per\_group}$$
  Reads `bg_block_bitmap`, flips bit `index` to `0`, and commits the sector to disk.
* **Inode Bitmap Freeing (`ClearInodeBitmapBit`)**:
  $$\text{group} = (N - 1) / \text{s\_inodes\_per\_group}, \quad \text{index} = (N - 1) \ \% \ \text{s\_inodes\_per\_group}$$
  Reads `bg_inode_bitmap`, flips bit `index` to `0`, and commits the sector to disk.

## 4.7 Single File Erasure Pipeline (`EraseFile`)

```
[Target Path] ──> TokenizePath() ──> Directory Traversal from EXT4_ROOT_INO (Inode 2)
                                                │
                                                ▼
                                    Target Inode Located
                                                │
                                                ▼
                                    ReadInode(inodeNum, inode)
                                                │
                                                ▼
                                    GetInodeAllocatedBlocks(inode)
                                      - If EXT4_EXTENTS_FL: Extent Tree Traversal (0xF30A)
                                      - If Legacy: Direct/Indirect block pointers i_block[0..11]
                                                │
                                                ▼
                                    BlockToSector(block) & 3-Pass DoD Overwrite
                                      - Overwrites all allocated data blocks
                                      - Pass 1: 0x00, Pass 2: 0xFF, Pass 3: PRNG Gibberish
                                                │
                                                ▼
                                    ClearBlockBitmapBit(block) for each block
                                                │
                                                ▼
                                    ClearInodeBitmapBit(inodeNum)
                                                │
                                                ▼
                                    WipeInodeOnDisk(inodeNum)
                                      - Overwrites all 256 bytes of inode with 0x00
                                      - Zeroes mode, size, timestamps, extent tree root (0xF30A -> 0x0000)
                                      - Stamps i_dtime with current epoch timestamp
                                                │
                                                ▼
                                    Directory Unlink (Crucial rec_len preservation)
                                      - Sets entry->inode = 0
                                      - Zeroes entry->name, sets name_len = 0
                                      - PRESERVES rec_len to maintain directory chain
```

## 4.8 Parent Directory Unlinking & `rec_len` Preservation

In ext4, entries in a directory block form a forward singly-linked list defined by `rec_len`:
* **The Pitfall**: Zeroing out the entire directory entry or resetting `rec_len = 0` causes directory parsers (`ls`, `e2fsck`, kernel) to immediately halt, corrupting directory traversal for all following files.
* **The Solution**: The driver sets `entry->inode = 0`, zeroes the `name` characters, sets `name_len = 0`, but **leaves `rec_len` completely unmodified**. Subsequent file lookups skip over the deleted entry seamlessly.

## 4.9 Volume-Wide Wipe (`WipeVolume`)

1. **Quarantines Critical System Metadata**:
   * Block Group 0 Superblock and backup superblocks.
   * Group Descriptor Table (GDT) blocks across all groups.
   * Block allocation bitmaps and Inode allocation bitmaps.
   * Reserved system inodes (Inode 1: bad blocks, Inode 2: root directory, Inode 7: resize inode, Inode 8: journal).
2. **Carpet-Bombing**: Executes 3-Pass DoD sanitization across all user data blocks.
3. **Root Directory Reset**: Resets the root directory block to contain only `.` (inode 2) and `..` (inode 2).

---

# 5. exFAT Driver Internals (`ExFatDriver`)

## 5.1 Volume Boot Record (VBR) Layout & Exponent Decoding

The exFAT Volume Boot Record begins at Sector 0 (LBA 0):

```
Sector 0: ExFatBootSector (512 Bytes)
┌────────────┬─────────────┬──────────────────────────┬──────────────┬──────────────┬──────────────┬──────────────┐
│ Jump Code  │ OEM Name    │ ClusterHeapOffsetSectors │ ClusterCount │ RootDirClust │ SecShift/Clus│ Boot Magic   │
│ 0xEB 0x76  │ "EXFAT   "  │ 32-bit sector offset     │ 32-bit count │ 32-bit clust │ 8-bit shifts │ 0xAA55       │
│ [Bytes 0-2]│ [Bytes 3-10]│ [Bytes 64-67]            │ [Bytes 68-71]│ [Bytes 88-91]│ [Bytes 108..]│ [Bytes 510..]│
└────────────┴─────────────┴──────────────────────────┴──────────────┴──────────────┴──────────────┴──────────────┘
```

### Exponent Shift Decoding
exFAT stores sector and cluster sizes as powers of 2 ($2^N$):
$$\text{m\_bytesPerSector} = 1 \ll \text{bytesPerSectorShift} \quad (9 \le \text{Shift} \le 12 \implies 512 \dots 4096\text{ bytes})$$
$$\text{m\_sectorsPerCluster} = 1 \ll \text{sectorsPerClusterShift} \quad (\text{Shift} \le 25 \implies \le 32\text{ MB clusters})$$

## 5.2 Microsoft exFAT Specification Checksum Algorithms

exFAT mandates three distinct mathematical checksums (implemented in `ExFatDriver`):

### 1. Boot Region Checksum (`ComputeBootChecksum` — Spec §3.1.9)
Calculates a 32-bit rotate-right accumulation over all bytes in Sectors 0 through 10:
$$\text{checksum} = ((\text{checksum} \gg 1) \mid (\text{checksum} \ll 31)) + \text{byte}$$
Sector 11 (the Boot Checksum Sector) contains this 32-bit checksum repeated 128 times across 512 bytes. `VerifyBootChecksum` validates this sector during `Mount()`.

### 2. Directory Entry Set Checksum (`ComputeEntrySetChecksum` — Spec §6.3.3.1)
Calculates a 16-bit rotate-right accumulation across all 32-byte entries in a directory entry set:
$$\text{checksum} = ((\text{checksum} \gg 1) \mid (\text{checksum} \ll 15)) + \text{byte}$$
* **Critical Exception**: Bytes 2 and 3 of the primary `0x85` entry (where `setChecksum` itself is stored) are skipped during accumulation.

### 3. Upcased File Name Hash (`ComputeNameHash` — Spec §6.3.5.1)
Converts UTF-16 filename characters to uppercase and computes a 16-bit hash stored in the `0xC0` Stream Extension entry.

## 5.3 Cluster Heap Addressing Mathematics

In exFAT, Clusters 0 and 1 are reserved. The first usable data cluster is **Cluster 2**:
$$\text{Sector} = \text{m\_vbr.clusterHeapOffsetSectors} + ((C - 2) \times \text{m\_sectorsPerCluster})$$

## 5.4 32-Byte Directory Entry Sets (Primary `0x85`, Secondary `0xC0`, Secondary `0xC1`)

A file or folder in exFAT is represented by a contiguous set of 32-byte directory records:

```
exFAT Directory Entry Set (Minimum 3 x 32-Byte Entries = 96 Bytes)
┌────────────────────────────────────────────────────────────────────────────────────────┐
│ 1. File Directory Entry (0x85 - Primary)                                               │
│    - entryType = 0x85 (Bit 7: InUse=1, Bit 5: Primary=1)                              │
│    - secondaryCount (Total secondary entries following, minimum 2)                     │
│    - setChecksum (16-bit checksum over entire set)                                     │
│    - fileAttributes (0x10 = Directory, 0x20 = Archive)                                 │
│    - Create / Access / Modify Timestamps                                               │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ 2. Stream Extension Entry (0xC0 - Secondary)                                           │
│    - entryType = 0xC0                                                                  │
│    - generalSecondaryFlags:                                                            │
│        Bit 0: AllocationPossible = 1                                                   │
│        Bit 1: NoFatChain = 1 (Contiguous) / 0 (Fragmented FAT chain)                   │
│    - firstCluster (Starting cluster in heap)                                           │
│    - dataLength & validDataLength (64-bit real file size in bytes)                     │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ 3. File Name Entries (0xC1 - Secondary, 1 to 17 entries)                               │
│    - entryType = 0xC1                                                                  │
│    - fileName[15] (Up to 15 UTF-16LE characters per entry)                             │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

## 5.5 Dual Allocation Tracking: Allocation Bitmap vs FAT Table

exFAT uses a dual allocation architecture:
1. **Allocation Bitmap (Entry Type `0x81`)**:
   * Stored in the cluster heap (located during `Mount()` by scanning the root directory).
   * 1 bit per cluster (Cluster 2 = bit 0).
   * Represents actual allocation state.
2. **FAT Table (`fatOffsetSectors`)**:
   * If `NoFatChain == 1`: The file is stored contiguously on disk! The FAT table is **completely bypassed**. Clusters are sequentially numbered:
     $$C_i = \text{firstCluster} + i \quad \text{for } i \in [0, \text{clusterCount} - 1]$$
   * If `NoFatChain == 0`: The file is fragmented. The driver reads 32-bit FAT entries via `ReadFatEntry(cluster)` until reaching `0xFFFFFFF8` (EOF).

## 5.6 Single File Erasure Pipeline (`EraseFile`)

```
[Target Path] ──> Directory Search across Entry Sets (0x85, 0xC0, 0xC1)
                                    │
                                    ▼
                         Target Entry Set Located
                                    │
                                    ▼
                         GetClusterChain(firstCluster, dataLength, noFatChain)
                           - If noFatChain == 1: sequential cluster generation
                           - If noFatChain == 0: FAT table traversal
                                    │
                                    ▼
                         ClusterToSector(C) & 3-Pass DoD Overwrite
                           - Overwrites all clusters with 0x00 -> 0xFF -> PRNG
                                    │
                                    ▼
                         ClearBitmapBit(C)
                           - Clears cluster bit in Allocation Bitmap (0x81)
                                    │
                                    ▼
                         WriteFatEntry(C, 0x00000000) (if fragmented)
                                    │
                                    ▼
                         Spec §6.2.1.1 Directory Deletion
                           - Clears Bit 7 (InUse = 0)
                           - 0x85 -> 0x05 (Deleted file entry)
                           - 0xC0 -> 0x40 (Deleted stream entry)
                           - 0xC1 -> 0x41 (Deleted filename entry)
                           - Zeroes remaining 31 bytes of each entry
```

## 5.7 Spec §6.2.1.1 Directory Deletion (`0x85` $\to$ `0x05`) vs `0x00` EndOfDirectory

* **The Catastrophic Bug of Zeroing Directory Entries**:
  In exFAT, `entryType = 0x00` is defined as **EndOfDirectory**! If a driver overwrites a deleted directory entry with `0x00`, any directory parser that encounters this byte immediately ceases parsing. All active files located deeper in that directory become completely invisible and inaccessible!
* **Microsoft Specification §6.2.1.1 Alignment**:
  To delete a directory entry set:
  1. Clears Bit 7 (`InUse = 0`), transforming:
     * `0x85` (`10000101b`) $\to$ `0x05` (`00000101b`)
     * `0xC0` (`11000000b`) $\to$ `0x40` (`01000000b`)
     * `0xC1` (`11000001b`) $\to$ `0x41` (`01000001b`)
  2. Overwrites the remaining 31 bytes of each entry with `0x00` (zeroing timestamps, sizes, hashes, cluster pointers, and filename characters).
  3. Result: The directory entry is permanently sanitized and freed for reuse, while subsequent active entries remain 100% accessible.

## 5.8 Volume-Wide Surgical Wipe (`WipeVolume`)

1. **Quarantines Critical System Clusters**:
   * Root Directory cluster chain.
   * Allocation Bitmap cluster chain (Entry `0x81`).
   * Upcase Table cluster chain (Entry `0x82`).
2. **Carpet-Bombing**: Overwrites all user clusters across the heap with 3-Pass DoD sanitization.
3. **FAT Table Reset**: Overwrites all non-quarantined FAT table entries with `0x00000000`.
4. **Allocation Bitmap Reconstruction**: Rebuilds the allocation bitmap in RAM with only quarantined system clusters marked in-use, and flushes it to disk.
5. **Root Directory Scrub**: Eradicates all user directory entry sets from the root directory.

---

# 6. FAT32 Driver Internals (`Fat32Driver`)

## 6.1 Architecture & Volume Boot Record (VBR / Extended BPB)
FAT32 stores its boot sector at Sector 0 (LBA 0):
* **`Fat32BootSector` (512 Bytes)**:
  * `jmpBoot`: Jump instruction (`0xEB 0x58 0x90` or `0xE9 ...`).
  * `oemName`: OEM Identifier (e.g. `"MSWIN4.1"`).
  * `bytesPerSector`: Sector size (512, 1024, 2048, 4096).
  * `sectorsPerCluster`: Cluster size multiplier ($1, 2, 4, 8, 16, 32, 64, 128$).
  * `reservedSectorCount`: Typically 32 sectors reserved before the first FAT.
  * `numFATs`: Count of FAT tables (typically 2).
  * `rootEntryCount`: Must be 0 for FAT32.
  * `totalSectors16`: Must be 0 for FAT32.
  * `fatSize16`: Must be 0 for FAT32.
  * `totalSectors32`: Total partition capacity in sectors.
  * `fatSize32`: Sectors occupied by each FAT table.
  * `extFlags`: Active FAT index and mirroring flags (Bit 7 = 0: mirroring enabled).
  * `rootCluster`: Starting cluster of the Root Directory (typically Cluster 2).
  * `fsInfoSector`: Sector location of the FSInfo structure (typically Sector 1).
  * `backupBootSector`: Sector location of the backup VBR (typically Sector 6).
  * `signature`: `0xAA55` at byte offset `0x1FE`.

```
LBA 0: Fat32BootSector (VBR)         LBA 1: FSInfo Sector
┌────────────────────────────────┐   ┌────────────────────────────────┐
│ Jump Code (0xEB 0x58 0x90)     │   │ LeadSig: 0x41615252 ("RRaA")   │
│ OEM: "MSWIN4.1"                │   │ StrucSig: 0x61417272 ("rrAa")  │
│ BytsPerSec: 512, SecPerClus: 8 │   │ Free_Count: Remaining Free     │
│ RsvdSec: 32, NumFATs: 2        │   │ Nxt_Free: Next Allocation Hint │
│ FATSz32: 32, RootClus: 2       │   │ TrailSig: 0xAA550000           │
│ Boot Signature: 0xAA55         │   └────────────────────────────────┘
└────────────────────────────────┘
```

### Addressing Mathematics
$$\text{firstDataSector} = \text{reservedSectorCount} + (\text{numFATs} \times \text{fatSize32})$$
$$\text{ClusterToSector}(C) = \text{firstDataSector} + ((C - 2) \times \text{sectorsPerCluster})$$
$$\text{FatSector}(C) = \text{fatStartSector} + ((C \times 4) / \text{bytesPerSector})$$
$$\text{FatOffsetInSector}(C) = (C \times 4) \pmod{\text{bytesPerSector}}$$

## 6.2 28-Bit Cluster Addressing & Dual FAT Synchronization
In FAT32, each FAT entry is a 32-bit word, but only the lower **28 bits** encode the cluster number. The upper 4 bits are reserved:
* `FAT32_CLUSTER_FREE` (`0x00000000`): Unallocated / sanitized cluster.
* `FAT32_CLUSTER_BAD` (`0x0FFFFFF7`): Damaged hardware cluster.
* `FAT32_CLUSTER_EOC_MIN` (`0x0FFFFFF8` .. `0x0FFFFFFF`): End of Cluster Chain (EOC).

### Dual FAT Mirroring
Unless `extFlags & 0x0080` disables mirroring, all sanitization updates write to **both primary FAT1 and mirror FAT2**, guaranteeing that no forensic recovery tool can inspect the mirror table to reconstruct cluster extents.

## 6.3 Short (8.3) & Long File Name (VFAT LFN) Directory Architecture
FAT32 stores directory entries as 32-byte records inside directory cluster chains:
* **Short File Name (SFN / `Fat32DirEntry`)**:
  * `name[11]`: 8 characters filename + 3 characters extension (space-padded).
  * `name[0] == 0xE5`: Entry is deleted / unallocated.
  * `name[0] == 0x00`: Entry is free and terminates directory parsing.
  * `fstClusHI` (Bytes 20..21) & `fstClusLO` (Bytes 26..27): 32-bit starting cluster $(C_{hi} \ll 16) \mid C_{lo}$.
  * `fileSize` (Bytes 28..31): 32-bit file size in bytes.
* **Long File Name (LFN / `Fat32LfnEntry`)**:
  * Attribute byte `attr = 0x0F` (`FAT32_ATTR_LONG_NAME`).
  * Precedes the SFN entry in reverse order (Order byte masked with `0x40` on first physical entry).
  * Encodes up to 13 UTF-16LE characters per 32-byte record.
  * Holds an 8-bit checksum matching the SFN entry's name.

## 6.4 Forensic Sanitization Pipeline
### Single File Erasure (`EraseFile`)
1. **Path Resolution**: Traverses directory cluster chains from `rootCluster` matching both SFN and LFN accumulated names.
2. **Cluster Chain Extraction (`GetClusterChain`)**: Follows 32-bit FAT entries until reaching EOC (`>= 0x0FFFFFF8`).
3. **Physical Sector Obliteration**: For every cluster in the chain, translates cluster to LBA sectors and executes 3-Pass DoD 5220.22-M sanitization (`0x00` $\to$ `0xFF` $\to$ PRNG gibberish via `std::mt19937_64`).
4. **FAT Table Deallocation**: Clears entries across both FAT1 and FAT2 tables to `0x00000000`, preserving reserved high 4 bits.
5. **Metadata Obliteration (`SanitizeDirectoryEntry`)**:
   * Preceding LFN entries: marks order byte with `0xE5` and zeroes all remaining 31 bytes.
   * Primary SFN entry: marks `name[0] = 0xE5`, zeroes `name[1..10]`, zeroes `fstClusHI` and `fstClusLO`, zeroes `fileSize = 0`, and zeroes all creation/access/modification timestamps.
6. **FSInfo Synchronization**: Updates `freeCount` by incrementing by the freed cluster count.

### Recursive Folder Erasure (`EraseDirectory`)
1. Locates directory starting cluster and parses child entries (`ListDirectoryContents`).
2. Recursively descends depth-first, eradicating all leaf files and nested subdirectories.
3. Overwrites the directory's own cluster chain with 3-Pass DoD sanitization.
4. Clears FAT entries for the directory cluster chain.
5. Marks the directory entry in the parent directory with `0xE5` and zeroes metadata.

### Volume-Wide Sanitization (`WipeVolume`)
1. Quarantines reserved sectors ($0 \dots \text{reservedSectorCount} - 1$), FAT1 and FAT2 tables, and the Root Directory cluster chain.
2. Carpet-bombs all allocated user data clusters across the volume with 3-Pass DoD sanitization.
3. Resets all user FAT entries to `0x00000000`.
4. Cleans all non-system entries from the root directory cluster.
5. Synchronizes FSInfo with updated free cluster count.

---

# 7. Cross-Filesystem Comparative Matrix

| Feature / Metric | NTFS (`NtfsDriver`) | XFS (`XfsDriver`) | ext4 (`Ext4Driver`) | exFAT (`ExFatDriver`) | FAT32 (`Fat32Driver`) |
|---|---|---|---|---|---|
| **Boot Header** | VBR (Sector 0) + Fixup | Superblock (Sector 0) | Superblock (Offset 1024) | VBR (Sector 0) + Checksum (Sec 11) | VBR (Sector 0) + FSInfo (Sec 1) |
| **Partition Structure** | Flat Cluster Addressing | Allocation Groups (AGs) | Block Groups | Cluster Heap | Cluster Heap |
| **Endianness** | Little-Endian | Big-Endian (Network) | Little-Endian | Little-Endian | Little-Endian |
| **Primary Metadata** | 1024-byte MFT Record | 256/512-byte Dinode Core | 256-byte Inode Table Entry | 32-byte Directory Entry Sets | 32-byte SFN Entry + LFNs |
| **Data Extents** | Nibble-packed Runlists | 128-bit Packed Extents / B+Tree | Extent Tree (`0xF30A`) | `NoFatChain` Flag OR 32-bit FAT | 28-bit FAT Chain (32-bit words) |
| **Directory Index** | Alphabetical B-Tree (`$I30`) | Shortform / Block / Node B+Tree | Linear Linked List (`rec_len`) | Sequential 32-byte Records | Sequential SFN + VFAT LFNs |
| **Allocation Tracker** | `$Bitmap` (Record 6) | AGF B+Trees (`bno_cur`, `cnt_cur`) | Block Bitmap (per group) | Allocation Bitmap (Cluster 2+) | Dual FAT Tables (FAT1 & FAT2) |
| **Directory Unlinking** | B-Tree Entry Scrubbing | Entry Compaction / `XfsDir2DataUnused` | `inode = 0`, `rec_len` preserved | Bit 7 cleared (`0x85` $\to$ `0x05`) | `name[0] = 0xE5` + LFN purge |
| **Metadata Wipe** | Full 1024 bytes zeroed | Full `m_inodeSize` bytes zeroed | Full 256 bytes zeroed + `i_dtime` | Bit 7 cleared, 31 bytes zeroed | Stamped `0xE5`, 31 bytes zeroed |
| **Journal Scrubbing** | `$LogFile` / `$UsnJrnl` sweep | Circular Intent Log sweep (`sb_logstart`)| JBD2 journal block sweep | N/A (No Journal) | N/A (No Journal) |
| **Torn-Write Guard** | Update Sequence Array (Fixup) | CRC32c Metadata (v5) | Checksum fields in GDT / Superblock | Sector 11 VBR Checksum | Backup VBR at Sector 6 |

---

# 8. Forensic Verification & Inspection Engine

The engine incorporates forensic inspection capabilities directly into `Tests/main.cpp`:

## 7.1 Canonical `xxd` Hex Dumps (`PrintHexDump`)
Outputs 16-byte aligned hex representations with physical byte offsets and printable ASCII characters, formatted identically to Linux `xxd`:
```
00004000: 46 49 4C 45 30 00 03 00 9A 12 00 00 00 00 00 00  FILE0...........
00004010: 01 00 01 00 38 00 01 00 A0 01 00 00 00 04 00 00  ....8...........
```

## 7.2 Semantic Byte Explanations
Translates raw sector bytes into human-readable forensic analyses:
* **NTFS**: Explains OEM signature, cluster size, MFT magic (`FILE` vs `0x00000000`), link counts, attribute bounds.
* **XFS**: Explains Superblock magic (`XFSB`), block sizes, Dinode magic (`IN` vs `0x0000`), modes, extent counts.
* **ext4**: Explains Superblock (`0xEF53`), Inode modes, Extent tree roots (`0xF30A` vs `0x0000`).
* **exFAT**: Explains VBR shifts, entry types (`0x85`, `0xC0`, `0xC1`), and checksum validation.

## 7.3 Shannon Entropy Computation ($H(X)$)
Measures the information density of physical sectors using a 256-bin probability distribution:
$$H(X) = - \sum_{i=0}^{255} P(x_i) \log_2 P(x_i)$$

```
┌──────────────────────────────────────┬────────────────────────────────────────────────────────┐
│ Entropy Range                        │ Forensic Semantic Interpretation                       │
├──────────────────────────────────────┼────────────────────────────────────────────────────────┤
│ H = 0.00 bits/byte                   │ Perfectly Uniform Zero-Fill (Clean / Metadata Wiped)   │
│ 0.00 < H < 3.50 bits/byte            │ Low-Density Text / Uncompressed User Data (Recoverable)│
│ 3.50 <= H < 7.20 bits/byte           │ Structured Code / Formatted Documents (Recoverable)    │
│ 7.20 <= H <= 8.00 bits/byte          │ High-Entropy PRNG Gibberish (DoD 3-Pass Overwritten)   │
└──────────────────────────────────────┴────────────────────────────────────────────────────────┘
```
This calculation formally validates that original user payloads are eradicated and replaced with high-entropy magnetic saturation noise.
