# Multi-Filesystem Surgical Erasure Engine

## 1. Overview & Architectural Philosophy

The **Surgical Erasure Engine** represents Layer 3 (`IFileSystemDriver`) of the 3-Layer Decoupled Architecture. It operates directly on raw disk sectors to understand the physical and logical layout of the filesystem, locate the precise sectors holding targeted files and folders, and command Layer 2 (`IHardwareController`) to destroy them.

Unlike naive disk formatters that wipe everything indiscriminately, or OS-level file cleaners that only delete metadata, the SanitizeX engine:
1. Surgically destroys user file data payloads using authentic **DoD 5220.22-M 3-pass overwriting**.
2. Eradicates all lingering on-disk metadata (MFT records, Inodes, Directory entries, Allocation bitmaps).
3. Preserves filesystem consistency, header signatures, and directory linkages so that the host operating system encounters **zero corruptions and passes `chkdsk` or `e2fsck` with Exit Code 0**.

---

## 2. NTFS Driver Internals (`NtfsDriver`)

* **Source Files**: [Erasure/File Systems/NTFS/NTFS.h](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/File%20Systems/NTFS/NTFS.h), [NTFS.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/File%20Systems/NTFS/NTFS.cpp), [NTFS_Structures.h](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/File%20Systems/NTFS/NTFS_Structures.h)

### 2.1 Volume Boot Record (VBR) & Dynamic Geometry
The driver reads Sector 0 (LBA 0) into `NtfsBootSector`:
* Validates OEM identifier `"NTFS    "` and boot signature `0xAA55` at offset 510.
* Decodes `bytesPerSector` (typically 512 or 4096) and `sectorsPerCluster` (typically 8).
* Decodes `$MFT` start cluster (`mftStartLCN`).
* Decodes MFT record size: `clustersPerFileRecord` is encoded as a signed 8-bit integer. If negative (e.g. `-10`), record size is $2^{|-10|} = 1024$ bytes.

```
Sector 0: VBR ("NTFS    ")
  │
  ├──> mftStartLCN = 4  ───> Cluster 4 (Sector 32)
                                │
                                └──> Record 0: $MFT
                                     Record 1: $MFTMirr
                                     Record 2: $LogFile
                                     Record 3: $Volume
                                     Record 5: . (Root Directory)
                                     Record 6: $Bitmap (Allocation status)
```

### 2.2 Master File Table ($MFT) Layout & Extent Mapping
The driver reads Record 0 (`$MFT`), decodes its `$DATA` attribute runlist, and populates `m_mftExtents` (a list of contiguous cluster extents `(lcn, clusterCount)`). This enables the driver to access any MFT record by index, even on heavily fragmented drives where the MFT itself is split across disjoint areas of the disk.

### 2.3 Update Sequence Array (Fixup / USN Protection)
NTFS applies a fixup sequence to every 512-byte sector of an MFT record to detect torn writes:
* The last 2 bytes of each 512-byte block contain an Update Sequence Number (USN).
* `ApplyFixup()` verifies that the USN matches the header and restores the original 2 bytes from the fixup array before interpreting attributes.

### 2.4 Runlist Decoding Engine (`DecodeRunList`)
Non-resident file data is stored in variable-length, compressed byte streams called "runlists". `DecodeRunList()` unpacks these runs into physical cluster extents:
* Byte 0: Low nibble = length field size (bytes); High nibble = offset field size (bytes).
* Offset bytes are signed relative offsets (can be negative due to fragmentation).
* The driver sign-extends offsets using 64-bit masks (`~0ULL << (offsetSize * 8)`) and tracks cumulative LCNs: $\text{currentLCN} = \text{previousLCN} + \text{deltaLCN}$.

### 2.5 Single File Erasure Pipeline (`EraseFile`)
When `EraseFile("passwords.txt")` is called:
1. **Directory Traversal**: Traverses the directory B-tree starting from Root Record 5 (`.`), resolving path tokens through `$INDEX_ROOT` and non-resident `"INDX"` blocks (`$INDEX_ALLOCATION`).
2. **Data Sanitization**:
   - **Resident Data**: If data is stored directly inside the MFT record (file size $< 700$ bytes), zeroes the payload bytes in-place.
   - **Non-Resident Data**: Decodes the `$DATA` runlist, converts all LCNs to physical disk sectors, and commands `m_hardware->SecureEraseSectors()` to perform the 3-Pass DoD overwrite across all allocated clusters.
3. **Allocation Bitmap Release**: Locates the cluster bits in `$Bitmap` (Record 6) and clears them from `1` (allocated) to `0` (free).
4. **MFT Record Obliteration (`WipeMftRecordOnDisk`)**: Completely zeroes the 1024-byte MFT record on physical disk using `m_hardware->WriteSectors()`, wiping all timestamps, file attributes, security IDs, and data pointers.
5. **Parent Directory Scrubbing (`ScrubDirectoryEntry`)**: Removes the index entry from the parent directory's `$INDEX_ROOT` or `"INDX"` block, updates node count headers, and preserves B-tree balance.

### 2.6 Recursive Directory Erasure & Volume Formatting
* **`EraseDirectoryRecursive()`**: Post-order depth-first traversal that recurses into all nested subdirectories, destroys all child files, zeroes child directory MFT records, and reclaims index allocation clusters.
* **`WipeVolume()`**: Quarantines critical system records 0–15 (`$MFT`, `$MFTMirr`, `$Volume`, `$Root`), scans all active user records (16+), sanitizes all user data clusters with DoD 3-pass overwriting, zeroes all user MFT records, and resets the root directory.
* **`FormatDrive()`**: Executes a full drive sanitize and writes a pristine, valid NTFS VBR at Sector 0 along with clean MFT system structures.

---

## 3. ext4 Driver Internals (`Ext4Driver`)

* **Source Files**: [Erasure/File Systems/ext4/ext4.h](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/File%20Systems/ext4/ext4.h), [ext4.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/File%20Systems/ext4/ext4.cpp), [ext4_Structures.h](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/File%20Systems/ext4/ext4_Structures.h)

### 3.1 Superblock & Block Group Architecture
* Probes byte offset 1024 for `Ext4Superblock` and validates magic `0xEF53`.
* Computes block size: $\text{blockSize} = 1024 \ll \text{s\_log\_block\_size}$ (typically 4096 bytes).
* Calculates block group count and loads the Group Descriptor Table (GDT) supporting both 32-bit (`Ext4GroupDesc`) and 64-bit (`Ext4GroupDesc64`) layouts.

### 3.2 Inode Architecture & 48-Bit Extent Trees (`0xF30A`)
Ext4 allocates 256-byte (or 128-byte) `Ext4Inode` structures. Modern ext4 files use extent trees rather than indirect block pointers:
* `Ext4ExtentHeader`: Magic `0xF30A` at `i_block[0..1]`.
* If `eh_depth == 0`: Followed by `eh_entries` leaf nodes (`Ext4Extent`), storing logical block (`ee_block`), block count (`ee_len`), and physical block address (`ee_start_hi << 32 | ee_start_lo`).
* If `eh_depth > 0`: Index nodes (`Ext4ExtentIdx`) point to intermediate branch blocks. The driver recursively walks index branches to resolve all physical blocks.

```
Inode i_block:
┌─────────────────────┬────────────────────────────────────────────────────────┐
│ Ext4ExtentHeader    │ Ext4Extent (Leaf 1)       │ Ext4Extent (Leaf 2)        │
│ Magic: 0xF30A       │ Block 0..7 -> LBA 84920   │ Block 8..15 -> LBA 91000   │
└─────────────────────┴────────────────────────────────────────────────────────┘
```

### 3.3 Inline Data Handling (`EXT4_INLINE_DATA_FL`)
For small files, ext4 stores payload bytes directly inside the 60-byte `i_block` array of the inode (`EXT4_INLINE_DATA_FL = 0x10000000`).
* The driver inspects `i_flags`. If inline data is present, it returns 0 external blocks to prevent treating ASCII characters as physical 48-bit block pointers.
* The inline payload is wiped when the 256-byte inode itself is zeroed.

### 3.4 Inode Sanitization & Directory Unlinking
1. **Physical Block Wipe**: Calls `m_hardware->SecureEraseSectors()` on all resolved extent blocks (3-pass DoD).
2. **Allocation Bitmap Clearance**: Clears the corresponding bits in the block allocation bitmap and the inode allocation bitmap, decrementing group free block/inode counters in the GDT.
3. **`WipeInodeOnDisk`**: Overwrites all 256 bytes of the on-disk inode record in the Inode Table with `0x00`, setting the deletion timestamp `i_dtime` to the current Unix epoch.
4. **Parent Directory Preservation**: Directory entries (`Ext4DirEntry2`) form a linked list using `rec_len`. When removing an entry, the driver merges its `rec_len` into the preceding entry. This completely removes the filename from the chain while maintaining exact directory block alignment.

---

## 4. exFAT Driver Internals (`ExFatDriver`)

* **Source Files**: [Erasure/File Systems/exFAT/exFAT.h](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/File%20Systems/exFAT/exFAT.h), [exFAT.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/File%20Systems/exFAT/exFAT.cpp), [exFAT_Structures.h](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/File%20Systems/exFAT/exFAT_Structures.h)

### 4.1 VBR Layout & Exponent Mathematics
ExFAT encodes geometries as powers of two:
* `bytesPerSectorShift`: $2^{\text{shift}}$ bytes per sector (e.g. $2^9 = 512$).
* `sectorsPerClusterShift`: $2^{\text{shift}}$ sectors per cluster (e.g. $2^3 = 8$).
* `clusterHeapOffset`: Sector offset where Cluster 2 begins.
* Cluster-to-sector formula:
  $$\text{LBA}(C) = \text{clusterHeapOffset} + (C - 2) \times \text{sectorsPerCluster}$$

### 4.2 32-Byte Directory Entry Sets
Every file or directory is defined by a contiguous set of 32-byte directory entries:
1. **Primary File Directory Entry (`0x85`)**: File attributes, creation/modification timestamps.
2. **Stream Extension Directory Entry (`0xC0`)**: Flags (`0x01` AllocationPossible, `0x02` NoFatChain), starting cluster, and 64-bit valid data length.
3. **File Name Directory Entries (`0xC1`)**: Stores up to 15 UTF-16LE characters per entry (up to 255 characters total).

### 4.3 Microsoft Spec §6.2.1.1 Compliant Deletion
A critical flaw in standard erasure tools is writing `0x00` over deleted directory entries. Per Microsoft exFAT specifications, `0x00` denotes **EndOfDirectory**, which immediately halts directory scanning and makes all subsequent files in the folder invisible!

**The SanitizeX Solution**:
In strict compliance with Microsoft exFAT spec §6.2.1.1:
* The driver clears bit 7 (`InUse = 0`), transforming:
  - `0x85` $\to$ `0x05` (File Entry)
  - `0xC0` $\to$ `0x40` (Stream Extension Entry)
  - `0xC1` $\to$ `0x41` (File Name Entry)
* The remaining 31 bytes of each entry are overwritten with `0x00`.
* This securely destroys the filename, timestamps, size, and cluster pointers while preserving the directory traversal chain.

### 4.4 Dual Allocation Tracking
ExFAT tracks cluster allocation in two ways:
* **NoFatChain flag set (`0x02`)**: The file is strictly contiguous; clusters are resolved purely from `firstCluster` and `validDataLength`.
* **FAT Table**: If fragmented, clusters are linked through the File Allocation Table.
* The driver wipes data clusters with 3-Pass DoD, clears bits in the Allocation Bitmap (Cluster 2), and zeroes FAT entries.

---

## 5. FAT32 Driver Internals (`Fat32Driver`)

* **Source Files**: [Erasure/File Systems/FAT32/FAT32.h](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/File%20Systems/FAT32/FAT32.h), [FAT32.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/File%20Systems/FAT32/FAT32.cpp), [FAT32_Structures.h](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/File%20Systems/FAT32/FAT32_Structures.h)

### 5.1 Dynamic Geometry Resolution
* Reads `Fat32BootSector` (512 bytes) and validates `0xAA55`.
* Computes first data sector:
  $$\text{firstDataSector} = \text{reservedSectorCount} + (\text{numFATs} \times \text{fatSize32})$$
* Computes total clusters:
  $$\text{totalClusters} = \frac{\text{totalSectors} - \text{firstDataSector}}{\text{sectorsPerCluster}}$$

### 5.2 28-Bit Cluster Chain Traversal & Dual FAT Synchronization
FAT32 cluster entries are 32 bits wide, but only the lower 28 bits store cluster addresses; the upper 4 bits are reserved for hardware flags:
* End-of-Chain (EOC) marker: Value $\ge 0\text{x0FFFFFF8}$.
* **Dual FAT Synchronization**: When freeing clusters, the driver zeroes entries in **both FAT1 and FAT2** while preserving the upper 4 reserved bits:
  ```cpp
  *entryPtr = (*entryPtr & 0xF0000000) | 0x00000000;
  ```

### 5.3 SFN and VFAT LFN Eradication
* Directory entries are scanned matching both Short Filenames (8.3 SFN) and Long Filenames (VFAT LFN sequences).
* When erasing:
  - Primary SFN entry: `name[0] = 0xE5` (deleted indicator), remaining 31 bytes zero-filled.
  - Preceding LFN entries: `order = 0xE5`, remaining 31 bytes zero-filled.
  - Updates `freeCount` and `nextFree` hints in `Fat32FSInfo` (Sector 1 and backup Sector 7).

---

## 6. XFS Driver Internals (`XfsDriver`)

* **Source Files**: [Erasure/File Systems/XFS/XFS.h](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/File%20Systems/XFS/XFS.h), [XFS.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/File%20Systems/XFS/XFS.cpp), [XFS_Structures.h](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/File%20Systems/XFS/XFS_Structures.h)

### 6.1 Multi-Allocation Group (AG) Architecture
XFS partitions storage into equal-sized Allocation Groups (AGs) for concurrent I/O:
* Superblock validates magic `0x58465342` ("XFSB").
* Big-Endian disk records are decoded to CPU order via `be16_to_cpu`, `be32_to_cpu`, and `be64_to_cpu`.
* Converts filesystem block numbers (`fsbno`) to physical disk blocks using AG block bitshifts:
  $$\text{LBA}(\text{fsbno}) = (\text{agno} \times \text{agblocks} + \text{agbno}) \times \text{sectorsPerBlock}$$

### 6.2 B+Tree Extent Resolution & Journal Scrubbing
* Extents are stored in 128-bit packed `XfsBmbtRec` records within the inode data fork or indirect B+Tree blocks (`XfsBtreeBlock` magic `"BMAP"`).
* When erasing:
  1. Resolves all extents and overwrites data blocks via DoD 3-pass.
  2. Zeroes intermediate B+Tree blocks.
  3. Zeroes the on-disk `XfsDinodeCore` structure.
  4. Scrubs directory entries from Shortform or Block directories.
  5. Cleans Intent Log transactions in the circular journal (`ScrubJournalForInode`).
