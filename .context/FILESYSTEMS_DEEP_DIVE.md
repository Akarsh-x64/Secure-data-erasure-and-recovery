# Multi-Filesystem Secure Erasure Engine — Comprehensive Deep Dive & Architectural Manual

## Executive Summary & System Philosophy

The **Multi-Filesystem Secure Erasure Engine** is an advanced, dependency-free C++17 system designed for the forensic eradication of data and metadata across all major modern storage filesystems:
1. **NTFS** (New Technology File System — Microsoft Windows)
2. **XFS** (Silicon Graphics High-Performance 64-bit Journaled File System — Enterprise Linux)
3. **ext4** (Fourth Extended File System — Linux Default)
4. **exFAT** (Extensible File Allocation Table — Flash & Removable Media)

### The Decoupled 3-Layer Architecture
The engine strictly enforces a zero-leakage, modular 3-tier boundary:

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                        CORE ENGINE ARCHITECTURAL LAYERS                                │
├──────────────────────────┬─────────────────────────────────────────────────────────────┤
│ 1. OS LAYER              │ WindowsStorageDevice (Win32) / LinuxStorageDevice (POSIX)   │
│    (IStorageDevice)      │ Direct sector read/write, physical drive handles, locking   │
├──────────────────────────┼─────────────────────────────────────────────────────────────┤
│ 2. HARDWARE LAYER        │ HDDController (Magnetic/VDisk), NVMeController, ATAController│
│    (IHardwareController) │ Physical wipe execution: 3-Pass DoD 5220.22-M, TRIM, Sanitize│
├──────────────────────────┼─────────────────────────────────────────────────────────────┤
│ 3. FILESYSTEM LAYER      │ NtfsDriver, XfsDriver, Ext4Driver, ExFatDriver             │
│    (IFileSystemDriver)   │ Structural parsing, extent resolution, metadata sanitization│
└──────────────────────────┴─────────────────────────────────────────────────────────────┘
```

### The Universal Sanitization Standard (DoD 5220.22-M 3-Pass)
Whenever data blocks, clusters, or entire disks are sanitized, the hardware layer enforces a 3-pass physical overwrite cycle:
* **Pass 1 (0x00 / Binary 00000000)**: Satures all magnetic domains to ground/zero polarity.
* **Pass 2 (0xFF / Binary 11111111)**: Reverses all magnetic domains to maximum positive saturation.
* **Pass 3 (PRNG Noise / Gibberish)**: Leverages a 64-bit Mersenne Twister PRNG (`std::mt19937_64`) to randomize domains, eliminating magnetic hysteresis (remanence) and preventing Magnetic Force Microscopy (MFM) recovery.

---

# 1. NTFS Deep Dive (`NtfsDriver`)

## 1.1 Architecture & On-Disk Geometry
NTFS structures everything as a file within the **Master File Table ($MFT)**.

### Volume Boot Record (VBR) — Sector 0
The first sector (LBA 0) contains the `NtfsBootSector` (512 bytes):
* `oemId`: `"NTFS    "` (8 bytes).
* `bytesPerSector`: Typically 512 (or 4096 on 4Kn native drives).
* `sectorsPerCluster`: Cluster size multiplier (typically 8 sectors = 4096 bytes).
* `totalSectors`: 64-bit integer specifying total partition capacity.
* `mftStartLCN`: 64-bit Logical Cluster Number (LCN) where `$MFT` begins.
* `mftMirrStartLCN`: 64-bit LCN of `$MFTMirr` (redundant backup of records 0–3).
* `clustersPerFileRecord`: Encoded as a signed 8-bit integer. When negative (e.g. `-10`), record size is $2^{|-10|} = 1024$ bytes.
* `bootSignature`: `0xAA55` at byte offset `0x1FE`.

```
LBA 0                       LBA Cluster 4 (mftStartLCN)
┌───────────────────────┐   ┌───────────────────────────────────────────────────────────┐
│ NtfsBootSector (VBR)  │   │ Master File Table ($MFT) Record Sequence                  │
│  "NTFS    "           │──>│  [Record 0: $MFT]     [Record 1: $MFTMirr] [Record 2: $LogFile]│
│  mftStartLCN = 4      │   │  [Record 3: $Volume]  [Record 4: $AttrDef] [Record 5: . (Root)]│
│  RecSize = 1024 B     │   │  [Record 6: $Bitmap]  [Record 7: $Boot]    [Record 8: $BadClust]│
└───────────────────────┘   └───────────────────────────────────────────────────────────┘
```

### The Update Sequence Array (USN / Fixup)
To protect against partial sector writes (torn writes), NTFS replaces the last 2 bytes of every 512-byte sector in an MFT record or Index (`INDX`) block with a 2-byte Update Sequence Number (USN). The original 2 bytes are stored in the USN array in the record header.
* **Fixup Verification (`ApplyFixup`)**: The driver validates that the sector-end bytes match the USN, replaces them with the original bytes from the fixup array in RAM, and validates structural integrity before parsing.

## 1.2 MFT Record Structure (1024 Bytes)
Every MFT record begins with `NtfsRecordHeader`:
* `magic`: `0x454C4946` (`"FILE"`). If corrupted, `"BAAD"`. If sanitized, `0x00000000`.
* `updateSequenceOffset`: Offset to USN array (typically `0x30`).
* `updateSequenceSize`: Number of USN words ($1 + \text{sectors}$).
* `hardLinkCount`: Number of directory references pointing to this record.
* `firstAttributeOffset`: Byte offset to the first attribute (typically `0x38`).
* `flags`:
  * `0x0000`: Unallocated / Free record.
  * `0x0001`: `FILE_RECORD_IN_USE` (Active file).
  * `0x0002`: `FILE_RECORD_DIRECTORY` (Directory).
* `usedBytes`: Actual bytes occupied by attributes.
* `allocatedBytes`: Total record buffer size (1024 bytes).

## 1.3 Attribute Processing & Runlist Decoding
Attributes follow the header sequentially, terminated by `0xFFFFFFFF` (`ATTR_END`):
1. **`$STANDARD_INFORMATION` (`0x10`)**: Holds timestamps (MACB: Modified, Accessed, Created, MFT modified) and DOS flags.
2. **`$FILE_NAME` (`0x30`)**: Holds parent directory MFT record index, real & allocated file sizes, namespace, and UTF-16LE filename.
3. **`$DATA` (`0x80`)**:
   * **Resident (`nonResidentFlag == 0`)**: Payload is stored directly inside the 1024-byte MFT record at `res->valueOffset`. Max payload $\approx 700$ bytes.
   * **Non-Resident (`nonResidentFlag == 1`)**: Payload is allocated in external clusters described by compressed **Data Runs**.
4. **`$INDEX_ROOT` (`0x90`) & `$INDEX_ALLOCATION` (`0xA0`)**: Implements directory B-Trees.

### Data Run Decoding Algorithm (`DecodeRunList`)
Non-resident extents are packed in variable-length nibbles:
* A header byte `H` has two 4-bit nibbles:
  $$\text{lengthFieldSize} = H \ \& \ \text{0x0F}, \quad \text{offsetFieldSize} = (H \gg 4) \ \& \ \text{0x0F}$$
* `lengthFieldSize` bytes are read as an unsigned integer = **Cluster Count**.
* `offsetFieldSize` bytes are read as a signed two's complement integer = **Relative LCN Offset**.
  * The sign bit is checked: `runlist[offset - 1] & 0x80`.
  * If negative, sign-extended: `lcnOffset |= (~0ULL << (offsetFieldSize * 8))`.
* `currentLcn += lcnOffset`. The extent `(currentLcn, clusterCount)` is appended to the allocation map.

## 1.4 Directory B-Tree Architecture (`$I30`)
NTFS indexes directory entries alphabetically using a B-Tree:
* **Small Directories**: Entries fit entirely within the `$INDEX_ROOT` attribute in the directory's MFT record.
* **Large Directories**: Spills over into `$INDEX_ALLOCATION`, which consists of 4096-byte `'INDX'` (`0x58444E49`) index blocks.
  * Each index block contains an `NtfsIndexBlock` header, an `NtfsIndexHeader`, and a chain of `NtfsIndexEntry` structures containing file references and `$FILE_NAME` attributes.
  * Sub-node references (`outChildVcn`) point to child index blocks down the B-Tree.

## 1.5 Forensic Sanitization Pipeline
### Single File Erasure (`EraseFile`)
1. **LCN Resolution**: `FindAttribute(ATTR_DATA)` extracts resident data OR non-resident data runs.
2. **Physical Sector Obliteration**: For non-resident files, every cluster in every extent is translated to sectors (`ClusterToSector(lcn)`) and wiped via `m_hardware->SecureEraseSectors()` using 3-Pass DoD sanitization.
3. **Cluster Allocation Deallocation**: For every wiped cluster, `ClearClusterBitmapBit(lcn)` flips the allocation bit in `$Bitmap` (Record 6) to `0`.
4. **Metadata Obliteration**: All 1024 bytes of the on-disk MFT record are zero-filled via `WipeMftRecordOnDisk()`, setting magic, flags, timestamps, and runlists to `0x00`.
5. **Directory Unlink**: `ScrubDirectoryEntry()` traverses the parent directory's `$INDEX_ROOT` and `$INDEX_ALLOCATION` blocks, unlinks the entry, zero-fills the filename and MFT pointer, and updates index used bounds.
6. **Journal Sweep**: Clears matching log records in `$LogFile` and `$UsnJrnl`.

### Recursive Folder Erasure (`EraseDirectory`)
1. Traverses parent directory B-Tree and locates directory MFT record.
2. Calls `ListDirectoryContents()` to enumerate all child MFT record references.
3. Recursively invokes `EraseDirectoryRecursive()`:
   * Recursively descends into subdirectories (Depth-First).
   * Eradicates all leaf files (data clusters + MFT records).
   * Eradicates all sub-folder index allocation cluster runs (`$INDEX_ALLOCATION`).
   * Zeroes the directory's own 1024-byte MFT record.
   * Cleans parent entry pointer.

### Drive Formatting (`FormatDrive`)
1. If `fullDriveSanitize` is true: Performs 3-pass DoD sanitization across the entire LBA capacity of the disk.
2. Reads `$Bitmap` runlist and zero-fills the entire volume allocation bitmap.
3. Re-initializes system records 0 through 15:
   * Records 0 (`$MFT`), 1 (`$MFTMirr`), 2 (`$LogFile`), 3 (`$Volume`), 4 (`$AttrDef`), 5 (`.` Root Dir), 6 (`$Bitmap`) are given valid initialized headers with `FILE_RECORD_IN_USE`.
   * Records 16 through `MFT_REC_USER_START` are marked unallocated (`flags = 0`).
4. Re-mounts the freshly initialized volume.

---

# 2. XFS Deep Dive (`XfsDriver`)

## 2.1 Architecture & Multi-AG Geometry
XFS is a high-performance 64-bit journaled filesystem divided into autonomous chunks called **Allocation Groups (AGs)**:
* `sb_agcount`: Total number of AGs across the partition (1 to thousands).
* `sb_agblocks`: Size of each AG in filesystem blocks.
* Each AG contains its own independent headers:
  * **Sector 0**: Superblock (`XfsSuperblock`, magic `"XFSB"` / `0x58465342`).
  * **Sector 1**: AG Free Space Info (`XfsAgf`, magic `"XAGF"` / `0x58414746`).
  * **Sector 2**: AG Inode Info (`XfsAgi`, magic `"XAGI"` / `0x58414749`).
  * **Sector 3**: AG Free Inode B+Tree (`XfsAgfl`, magic `"XAFL"` / `0x5841464C`).

```
Partition Layout across Allocation Groups:
┌────────────────────────┬────────────────────────┬────────────────────────┐
│  Allocation Group 0    │  Allocation Group 1    │  Allocation Group N    │
├────┬────┬────┬────┬────┼────┬────┬────┬────┬────┼────┬────┬────┬────┬────┤
│SB  │AGF │AGI │AGFL│DATA│SB  │AGF │AGI │AGFL│DATA│SB  │AGF │AGI │AGFL│DATA│
│sec0│sec1│sec2│sec3│... │sec0│sec1│sec2│sec3│... │sec0│sec1│sec2│sec3│... │
└────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┘
```

### Big-Endian to CPU Host Unpacking
All on-disk XFS structures are Big-Endian. The driver converts all fields using `be16_to_cpu`, `be32_to_cpu`, `be64_to_cpu`.

### FSB to Disk Block Address Translation (`FsbToBlock`)
XFS encodes the AG number into the high bits of an extent block number (`fsbno`):
$$\text{agno} = \text{fsbno} \gg \text{sb\_agblklog}$$
$$\text{agbno} = \text{fsbno} \ \& \ ((1\text{ULL} \ll \text{sb\_agblklog}) - 1)$$
$$\text{linear\_block} = (\text{agno} \times \text{sb\_agblocks}) + \text{agbno}$$

### 64-Bit Inode Number Translation (`InoToByteOffset`)
An XFS inode number packs the AG index, AG block number, and block-relative offset:
$$\text{agno} = \text{ino} \gg (\text{inopblog} + \text{agblklog})$$
$$\text{agbno} = (\text{ino} \gg \text{inopblog}) \ \& \ ((1\text{ULL} \ll \text{sb\_agblklog}) - 1)$$
$$\text{offsetInBlock} = (\text{ino} \ \& \ ((1\text{ULL} \ll \text{sb\_agblklog}) - 1)) \times \text{sb\_inodesize}$$
$$\text{absBlock} = (\text{agno} \times \text{sb\_agblocks}) + \text{agbno}$$
$$\text{physicalByteOffset} = (\text{absBlock} \times \text{blockSize}) + \text{offsetInBlock}$$

## 2.2 Inode Core (`XfsDinodeCore`) & Data Fork Formats
Every inode begins with `XfsDinodeCore` (100 bytes for v2, 176 bytes for v3 CRC):
* `di_magic`: `0x494E` (`"IN"`).
* `di_mode`: POSIX file mode (`S_IFREG`, `S_IFDIR`, permissions).
* `di_version`: `1`, `2`, or `3`.
* `di_format`: Format of the data fork immediately following the core:
  * **`XFS_DINODE_FMT_LOCAL` (`1`)**: Inline payload (Shortform directory or fast symlink).
  * **`XFS_DINODE_FMT_EXTENTS` (`2`)**: Array of 16-byte packed extent records (`XfsBmbtRec`).
  * **`XFS_DINODE_FMT_BTREE` (`3`)**: Multi-level B+Tree. Root header `XfsBmdrBlock` is stored in the data fork, pointing to external B+Tree blocks.
* `di_size`: 64-bit real file size in bytes.
* `di_nextents`: 32-bit count of data extents.
* `di_forkoff`: Offset to attribute fork in 8-byte units.

### BMBT Extent Record Unpacking (`XfsBmbtRec`)
Extent records are 128-bit bitpacked structures:
```
Bits [127]    : State (0 = written, 1 = unwritten)
Bits [126..73]: Logical file block offset (54 bits)
Bits [72..21] : Physical start block number / fsbno (52 bits)
Bits [20..0]  : Block count (21 bits)
```
The driver unpacks `l0` and `l1` into `ExtentInfo { startoff, startblock, blockcount, state }`.

## 2.3 Directory Formats: Shortform, Extent, and Node B-Tree
1. **Shortform (`LOCAL`)**: Entries fit inside the inode data fork. Begins with `XfsDir2SfHdr` (count, parent inode), followed by entries containing name length, name string, and 4-byte/8-byte inode numbers.
2. **Block / Extent (`EXTENTS`)**: Directory spans external filesystem blocks. Header `XfsDir2DataHdr` (`0x58443242` / `"XD2B"`), followed by `XfsDir2DataEntry` structs and `XfsDir2DataUnused` free space tags.
3. **Node B-Tree (`BTREE`)**: Name hashes (`XfsDaIntnode`, magic `0xFEBE`) index into leaf directory blocks.

## 2.4 Forensic Sanitization Pipeline
### File Erasure (`EraseFile`)
1. Decodes data extents via `GetInodeExtents()`. If B+Tree format, traverses BMBT pointers and queues intermediate leaf blocks in `btreeBlocks`.
2. Physically wipes all allocated data blocks using 3-Pass DoD sanitization.
3. Physically wipes all intermediate B+Tree metadata blocks.
4. **Inode Destruction (`WipeInodeOnDisk`)**: Reads the sector containing the target inode, overwrites all `m_inodeSize` bytes with `0x00` (zeroing magic `"IN"`, mode, size, timestamps, and data fork), and writes it back to metal.
5. **Parent Directory Sanitization (`WipeDirectoryEntry`)**:
   * For Shortform directories: Removes entry, compacts remaining entries, zeroes tail space, decrements count.
   * For Block/Extent directories: Converts active entry to `XfsDir2DataUnused`, sets magic tag, zeroes filename bytes, updates free index tags.
6. **AGI Unlinked Bucket Purge (`ScrubAgiUnlinkedBucket`)**: Reads Sector 2 of the target AG (`XfsAgi`), scans all 64 `agi_unlinked[]` hash buckets, and replaces any references to the erased inode with `XFS_AGI_UNLINKED_NULL` (`0xFFFFFFFF`).
7. **Intent Log (Journal) Sanitization (`ScrubJournalForInode`)**: Reads `sb_logblocks` starting at `sb_logstart`, scans for log operation headers matching the inode number or filename, and overwrites transaction buffers with zeros.

### Recursive Folder Erasure (`EraseDirectory`)
1. Reads folder inode and parses entries (Shortform, Extents, or B-Tree).
2. Recursively traverses all subdirectories in Depth-First order.
3. Eradicates all child files (extents + on-disk inodes).
4. Eradicates directory block extents allocated to subdirectories.
5. Zeroes on-disk directory inodes and unlinks the folder from its parent directory.

### Volume-Wide Surgical Wipe (`WipeVolume`)
1. Maps and quarantines all critical structural zones:
   * Sector 0..3 (Superblock, AGF, AGI, AGFL) across every AG.
   * Root Inode (Inode 64 / 128) and its directory extents.
   * Journal log blocks (`sb_logstart`).
2. Carpet-bombs all non-quarantined blocks across the volume with 3-Pass DoD sanitization.
3. Re-initializes clean empty root directory and scrubs transaction logs.

---

# 3. ext4 Deep Dive (`Ext4Driver`)

## 3.1 Architecture & Block Group Layout
ext4 divides partition capacity into **Block Groups** (typically 32,768 blocks = 128 MB per group):
* **Superblock (1024 Bytes)**: Located at byte offset 1024 from partition start:
  * `s_magic`: `0xEF53`.
  * `s_log_block_size`: Block size $= 1024 \ll \text{s\_log\_block\_size}$ (typically 4096 bytes).
  * `s_blocks_per_group`: Blocks in each group (e.g. 32768).
  * `s_inodes_per_group`: Inodes per group (e.g. 8192).
  * `s_inode_size`: Size of on-disk inode (standard is 256 bytes).
  * `s_feature_incompat`: Flags such as `EXT4_FEATURE_INCOMPAT_64BIT`, `EXT4_FEATURE_INCOMPAT_EXTENTS`, `EXT4_FEATURE_INCOMPAT_FILETYPE`.
* **Group Descriptor Table (GDT)**: Located immediately after the superblock (Block 1 or 2):
  * Describes each block group via `Ext4GroupDesc` (32 bytes) or `Ext4GroupDesc64` (64 bytes).
  * `bg_block_bitmap`: Block allocation bitmap address.
  * `bg_inode_bitmap`: Inode allocation bitmap address.
  * `bg_inode_table`: Starting block of this group's Inode Table.

```
Block Group Layout:
┌──────────────┬──────────────┬──────────────┬──────────────┬────────────────────────┐
│ Superblock   │ Group Descs  │ Block Bitmap │ Inode Bitmap │ Inode Table            │
│ (1024 B)     │ (GDT Blocks) │ (1 Block)    │ (1 Block)    │ (Inodes 1..N)          │
└──────────────┴──────────────┴──────────────┴──────────────┴────────────────────────┘
```

### Inode Indexing Mathematics
To read or write inode number $N$:
$$\text{group} = (N - 1) / \text{s\_inodes\_per\_group}$$
$$\text{indexInGroup} = (N - 1) \ \% \ \text{s\_inodes\_per\_group}$$
$$\text{byteOffsetInTable} = \text{indexInGroup} \times \text{s\_inode\_size}$$
$$\text{inodeBlock} = \text{itableStart} + (\text{byteOffsetInTable} / \text{blockSize})$$
$$\text{offsetInBlock} = \text{byteOffsetInTable} \ \% \ \text{blockSize}$$

## 3.2 Inode Structure (`Ext4Inode`) & Extent Trees
On-disk inode size is `s_inode_size` (256 bytes). The core struct is 128 bytes, with the remaining 128 bytes used for extended attributes and inline data.
* `i_mode`: File mode (`0x8000` = Regular File, `0x4000` = Directory).
* `i_size_lo` / `i_size_high`: 64-bit file size.
* `i_links_count`: Hard link count.
* `i_dtime`: Deletion timestamp (set upon unlinking).
* `i_flags`: Flags (`0x00080000` = `EXT4_EXTENTS_FL`, `0x10000000` = `EXT4_INLINE_DATA_FL`).
* `i_block[60]`: Payload storage array (15 words $\times$ 4 bytes = 60 bytes).

### The Extent Tree Architecture (`0xF30A`)
When `EXT4_EXTENTS_FL` is active, `i_block[60]` holds the root of an extent tree:
* **Extent Header (`Ext4ExtentHeader`)**:
  * `eh_magic`: `0xF30A`.
  * `eh_entries`: Number of valid extent entries following header.
  * `eh_max`: Capacity of this node.
  * `eh_depth`: `0` for leaf nodes; $>0$ for intermediate index nodes.
* **Leaf Extent (`Ext4Extent` — Depth 0)**:
  * `ee_block`: Logical file block offset.
  * `ee_len`: Number of contiguous blocks (up to 32,768).
  * `ee_start_hi` / `ee_start_lo`: Physical 48-bit block address.
* **Index Node (`Ext4ExtentIdx` — Depth > 0)**:
  * Points to an external filesystem block holding child `Ext4ExtentHeader` structures. The driver recursively traverses index blocks via `CollectExtentBlocks()`.

## 3.3 Directory Structure (`Ext4DirEntry2`)
ext4 directory blocks store a linked list of variable-length directory entries:
* `inode`: 32-bit inode number ($0$ if deleted/unlinked).
* `rec_len`: 16-bit displacement to the start of the next entry.
* `name_len`: 8-bit filename length.
* `file_type`: 8-bit type (`1` = regular, `2` = directory).
* `name[]`: Filename characters.

## 3.4 Forensic Sanitization Pipeline
### Single File Erasure (`EraseFile`)
1. **Extent Traversal**: Evaluates `i_flags`. If `EXT4_EXTENTS_FL`, recursively traverses extent tree and gathers all physical data blocks. If legacy, reads direct block pointers `i_block[0..11]`.
2. **Physical Data Wiping**: Overwrites all allocated data blocks using 3-Pass DoD sanitization.
3. **Block Bitmap Clearing (`ClearBlockBitmapBit`)**:
   $$\text{group} = (\text{block} - \text{s\_first\_data\_block}) / \text{s\_blocks\_per\_group}$$
   $$\text{index} = (\text{block} - \text{s\_first\_data\_block}) \ \% \ \text{s\_blocks\_per\_group}$$
   Reads `bg_block_bitmap` block, flips the bit to `0`, and commits it back to disk.
4. **Inode Bitmap Clearing (`ClearInodeBitmapBit`)**:
   Flips bit $(N - 1) \ \% \ \text{s\_inodes\_per\_group}$ to `0` in `bg_inode_bitmap`.
5. **Inode Scrubbing (`WipeInodeOnDisk`)**:
   Overwrites all 256 bytes of the on-disk inode structure with zeros, eradicating `i_block[60]` inline data, fast symlinks, and extent roots. Stamps current Unix epoch time in `i_dtime`.
6. **Parent Directory Unlink**:
   Locates entry in parent directory block. Sets `entry->inode = 0`, zeroes `name` characters, sets `name_len = 0`, but **preserves `rec_len`** so the directory traversal chain remains continuous.

### Volume-Wide Wipe (`WipeVolume`)
1. Quarantines Group 0 Superblock, GDT blocks, Block/Inode Bitmaps across all groups, and Root Inode (Inode 2).
2. Overwrites all user data blocks across the volume with 3-Pass DoD sanitization.
3. Resets Root Directory entry list to contain only `.` and `..`.

---

# 4. exFAT Deep Dive (`ExFatDriver`)

## 4.1 Architecture & Volume Boot Record (VBR)
exFAT is optimized for flash memory and removable drives:
* **Sector 0 (Main Boot Sector)**:
  * `fileSystemName`: `"EXFAT   "` (8 bytes).
  * `clusterHeapOffsetSectors`: Sector offset where Cluster 2 begins.
  * `clusterCount`: Total data clusters in the heap.
  * `rootDirectoryFirstCluster`: Starting cluster of the root directory.
  * `bytesPerSectorShift`: Exponent $P$ where $\text{BytesPerSector} = 2^P$ ($9 \dots 12 \to 512 \dots 4096$).
  * `sectorsPerClusterShift`: Exponent $S$ where $\text{SectorsPerCluster} = 2^S$.
  * `bootSignature`: `0xAA55`.
* **Sector 1..8**: Extended VBR sectors.
* **Sector 9**: OEM Parameter sector.
* **Sector 11**: **Main Boot Checksum Sector** (Spec §3.1.9): Holds the 32-bit boot checksum repeated 128 times across the 512-byte sector.

```
Cluster to Sector Translation:
Sector = clusterHeapOffsetSectors + (Cluster - 2) * sectorsPerCluster
```

### Microsoft exFAT Specification Checksum Algorithms
1. **Boot Region Checksum (`ComputeBootChecksum`)**:
   Rotates right by 1 bit and accumulates all bytes across Sectors 0 through 10:
   $$\text{checksum} = ((\text{checksum} \gg 1) \mid (\text{checksum} \ll 31)) + \text{byte}$$
   (Sector 11 must match this checksum; verified during `Mount()`).
2. **Directory Entry Set Checksum (`ComputeEntrySetChecksum`)**:
   16-bit rotate-right accumulation across all 32-byte entries in a set, skipping bytes 2 and 3 of the primary entry:
   $$\text{checksum} = ((\text{checksum} \gg 1) \mid (\text{checksum} \ll 15)) + \text{byte}$$
3. **Upcased Filename Hash (`ComputeNameHash`)**:
   Converts UTF-16 characters to uppercase and computes 16-bit hash for the `0xC0` Stream Extension entry.

## 4.2 Directory Entry Sets (32-Byte Records)
A file or folder in exFAT consists of a continuous set of 32-byte directory entries:
```
┌────────────────────────┬────────────────────────┬────────────────────────┐
│ File Directory Entry   │ Stream Extension Entry │ File Name Entry (1..N) │
│ Type: 0x85 (Primary)   │ Type: 0xC0 (Secondary) │ Type: 0xC1 (Secondary) │
│ - Flags / Attributes   │ - Allocation Flags     │ - 15 UTF-16 Characters │
│ - Timestamps           │ - firstCluster         │   per 32-byte record   │
│ - setChecksum          │ - dataLength           │                        │
└────────────────────────┴────────────────────────┴────────────────────────┘
```
* **Primary: File Directory Entry (`0x85`)**:
  * `entryType`: `0x85` (Bit 7 = `InUse`, Bit 5 = `TypeCategory` primary).
  * `secondaryCount`: Number of following secondary entries ($\ge 2$).
  * `setChecksum`: 16-bit checksum over the entry set.
  * `fileAttributes`: `0x10` = Directory, `0x20` = Archive.
* **Secondary: Stream Extension (`0xC0`)**:
  * `generalSecondaryFlags`:
    * Bit 0 (`AllocationPossible = 1`).
    * Bit 1 (`NoFatChain = 1` if contiguous, `0` if fragmented).
  * `firstCluster`: Starting cluster number.
  * `dataLength` & `validDataLength`: 64-bit file size.
* **Secondary: File Name (`0xC1`)**:
  * Stores up to 15 UTF-16 characters per entry. Long names chain multiple `0xC1` entries.

## 4.3 Allocation Tracking: Allocation Bitmap vs FAT Table
exFAT employs a dual allocation mechanism:
1. **Allocation Bitmap (Directory Entry `0x81`)**: 1 bit per cluster (Cluster 2 = bit 0). Used exclusively for cluster allocation status.
2. **FAT Table**:
   * If `NoFatChain == 1` (Bit 1 of flags is set): The cluster allocation is contiguous! The FAT table is **not** traversed; clusters are sequentially numbered `firstCluster` to `firstCluster + count - 1`.
   * If `NoFatChain == 0`: The cluster allocation is fragmented; the driver traverses the 32-bit FAT chain until `0xFFFFFFF8` (EOF).

## 4.4 Forensic Sanitization Pipeline
### Single File Erasure (`EraseFile`)
1. **Cluster Chain Extraction (`GetClusterChain`)**: Evaluates `NoFatChain`. If contiguous, generates sequential cluster list. If fragmented, reads through FAT table.
2. **Physical Sector Wiping**: Every cluster in the chain is converted to physical sectors and wiped with 3-Pass DoD sanitization.
3. **Allocation Bitmap Freeing (`ClearBitmapBit`)**:
   $$\text{bitmapSector} = \text{ClusterToSector}(\text{m\_bitmapFirstCluster}) + ((C - 2) / (\text{bytesPerSec} \times 8))$$
   $$\text{bitOffset} = (C - 2) \ \% \ (\text{bytesPerSec} \times 8)$$
   Clears the bit in the sector buffer and writes back to disk.
4. **FAT Table Freeing**: If not contiguous, overwrites FAT entries with `0x00000000` (`WriteFatEntry`).
5. **Spec-Compliant Directory Entry Deletion**:
   * **Crucial Rule**: In exFAT, setting `entryType = 0x00` represents **EndOfDirectory**! Setting `0x00` terminates directory parsing and makes all subsequent files invisible.
   * **Spec §6.2.1.1 Alignment**: Clears Bit 7 (`InUse = 0`), transforming:
     * `0x85` $\to$ `0x05` (Deleted file entry)
     * `0xC0` $\to$ `0x40` (Deleted stream entry)
     * `0xC1` $\to$ `0x41` (Deleted filename entry)
   * The remaining 31 bytes of each entry are zeroed (wiping names, hashes, sizes, timestamps, and cluster pointers).
   * Result: The slot is marked deleted, all forensic metadata is obliterated, and subsequent active files remain intact and accessible.

### Volume-Wide Surgical Wipe (`WipeVolume`)
1. Quarantines critical system clusters:
   * Root Directory cluster chain.
   * Allocation Bitmap cluster chain (Entry `0x81`).
   * Upcase Table cluster chain (Entry `0x82`).
2. Overwrites all non-quarantined user clusters across the heap with 3-Pass DoD sanitization.
3. Zero-fills the entire FAT table sectors (`m_vbr.fatOffsetSectors`).
4. Re-links FAT table entries for quarantined chains (Root Dir, Bitmap, Upcase).
5. Rebuilds pristine Allocation Bitmap in RAM marking only quarantined clusters as in-use, and flushes to disk.
6. Scrubs all non-system entries from the Root Directory.

---

# 5. Cross-Filesystem Comparative Matrix

| Feature | NTFS | XFS | ext4 | exFAT |
|---|---|---|---|---|
| **Boot Header** | VBR (Sector 0) + Fixup | Superblock (Sector 0) | Superblock (Offset 1024) | VBR (Sector 0) + Sec 11 Checksum |
| **Partition Division** | Flat Cluster Addressing | Allocation Groups (AGs) | Block Groups | Cluster Heap |
| **Endianness** | Little-Endian | Big-Endian (Network) | Little-Endian | Little-Endian |
| **Metadata Record** | 1024-byte MFT Record | 256/512-byte Dinode | 256-byte Inode | 32-byte Entry Set (3+ records) |
| **Data Addressing** | Nibble-packed Data Runs | 128-bit Packed Extents / B+Tree | Extent Tree (`0xF30A`) | Contiguous Flag OR 32-bit FAT |
| **Directory Index** | Alphabetical B-Tree (`$I30`) | Hashed B-Tree / Shortform | Linear Array (`rec_len`) | Sequential 32-byte Records |
| **Allocation Tracking** | `$Bitmap` (Record 6) | AGF B+Trees (`bno_cur`, `cnt_cur`) | Block Bitmap (per group) | Allocation Bitmap (Cluster 2+) |
| **Parent Unlink Method** | B-Tree index entry scrub | Shortform compact / Extent unused | `inode = 0`, `rec_len` preserved | Bit 7 (`InUse`) cleared |
| **Metadata Sanitization**| Full 1024-byte MFT Zeroing | Full `m_inodeSize` Zeroing | Full `m_inodeSize` Zeroing + `i_dtime`| Bit 7 cleared, 31 bytes zeroed |
| **Journal Scrubbing** | `$LogFile` / `$UsnJrnl` purge | Intent Log (`sb_logstart`) purge | JBD2 journal block purge | N/A (No Journal) |

---

# 6. Forensic Verification & Inspection Engine

The engine incorporates forensic inspection capabilities directly into `Tests/main.cpp`:

1. **Canonical `xxd` Hex Dumps (`PrintHexDump`)**:
   Displays 16-byte aligned physical disk hex representations alongside printable ASCII characters, with physical byte offsets.
2. **Semantic Byte Breakdown**:
   Translates raw disk bytes into plain-English forensic structures:
   * **NTFS**: Explains VBR OEM, cluster sizes, MFT magic (`FILE` vs `0x00000000`), link counts, attribute bounds.
   * **XFS**: Explains Superblock magic (`XFSB`), block sizes, Dinode magic (`IN` vs `0x0000`), modes, extent counts.
   * **ext4**: Explains Superblock (`0xEF53`), Inode modes, Extent tree roots (`0xF30A` vs `0x0000`).
   * **exFAT**: Explains VBR shifts, entry types (`0x85`, `0xC0`, `0xC1`), and checksum validation.
3. **Shannon Entropy Analysis**:
   Computes real-time information density:
   $$H(X) = - \sum_{i=0}^{255} P(x_i) \log_2 P(x_i)$$
   * Distinguishes between **Active User Payload** ($H < 3.5$ or repeated patterns) and **DoD 3-Pass PRNG Gibberish** ($H > 7.5\text{ bits/byte}$).
