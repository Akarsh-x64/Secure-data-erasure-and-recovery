# Data Recovery & Forensic Investigation Subsystem

## 1. Executive Summary & Research Foundation

The **Data Recovery Subsystem** (`Recovery/`) serves a dual purpose in SanitizeX:
1. **Disaster Recovery**: Safely reconstructs partitions, unallocated filesystem metadata, and deleted files from damaged or formatted storage media.
2. **Adversarial Auditor**: Acts as the independent verification auditor against the Surgical Erasure Engine, proving that wiped files can never be resurrected by forensic examiners.

### Academic Foundation
The subsystem is engineered according to peer-reviewed forensic literature:
* **Brian Carrier (2005)** — *File System Forensic Analysis*: Inode/MFT unallocated scanning and metadata reconstruction.
* **Simson Garfinkel (2007)** — *Carving Contiguous and Fragmented Files with Fast Object Validation*: Header/footer object boundary heuristics.
* **Golden G. Richard III & Vassil Roussev (2005)** — *Scalpel: A Frugal, High Performance File Carver*: In-place searching and file signature matching.

---

## 2. The Kernel-Enforced Read-Only Barrier (`IReadOnlyStorage`)

A primary rule of forensic science (DOJ / ISO/IEC 27037) is **strict evidence immutability**. Under no circumstances may an analysis tool write to or modify target storage.

```
┌────────────────────────────────────────────────────────────────────────┐
│                        FORENSIC SAFETY BOUNDARY                        │
├────────────────────────────────────────────────────────────────────────┤
│  Recovery Subsystem (Core / Parsers / Carvers)                         │
│     │                                                                  │
│     ▼                                                                  │
│  IReadOnlyStorage Interface (Recovery/Core/IReadOnlyStorage.h)         │
│  • Exposes: Open(), Close(), Read(), GetSize(), GetSectorSize()        │
│  • ZERO write methods exist in this interface hierarchy                │
│     │                                                                  │
│     ▼                                                                  │
│  WindowsReadOnlyStorage (Recovery/Acquisition/WindowsReadOnlyStorage.cpp)│
│  • CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, ...)               │
│  • Explicitly OMITS GENERIC_WRITE                                      │
│     │                                                                  │
│     ▼                                                                  │
│  Windows OS Kernel (NTOSKRNL.EXE)                                      │
│  • OS Kernel physically forbids write syscalls on the handle           │
└────────────────────────────────────────────────────────────────────────┘
```

* **Header**: [Recovery/Core/IReadOnlyStorage.h](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Recovery/Core/IReadOnlyStorage.h)
* **Windows Implementation**: [Recovery/Acquisition/WindowsReadOnlyStorage.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Recovery/Acquisition/WindowsReadOnlyStorage.cpp)
* **Linux Implementation**: [Recovery/Acquisition/LinuxReadOnlyStorage.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Recovery/Acquisition/LinuxReadOnlyStorage.cpp) (opens with `O_RDONLY`).

### Arbitrary Alignment via `ByteReader`
* **File**: [Recovery/Core/ByteReader.h](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Recovery/Core/ByteReader.h)
* Raw disk handles require reads to be sector-aligned (512 or 4096 bytes). 
* `ByteReader` wraps `IReadOnlyStorage`: it automatically rounds offsets down to the nearest sector boundary, rounds lengths up, reads the aligned span into an internal buffer, and extracts the exact sub-range requested.
* This allows partition parsers and MFT engines to read arbitrary structs at byte offsets (e.g. `ReadStruct<Ext4Superblock>(1024)`).

---

## 3. Partition Discovery Layer (`Recovery/Partitions/`)

Before filesystems can be audited or recovered, physical disk images must be parsed for partition tables:

### 3.1 MBR Parser (`MBRParser`)
* **Files**: [Recovery/Partitions/MBRParser.h](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Recovery/Partitions/MBRParser.h), [MBRParser.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Recovery/Partitions/MBRParser.cpp)
* Reads LBA 0, validates boot signature `0x55AA` at offset 510.
* Extracts the 4 primary 16-byte partition records from offset `0x1BE`.
* **Protective MBR Detection**: If partition type is `0xEE`, `HasProtectiveMBR()` returns true, signaling the engine to delegate to the GPT parser.

### 3.2 GPT Parser (`GPTParser`)
* **Files**: [Recovery/Partitions/GPTParser.h](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Recovery/Partitions/GPTParser.h), [GPTParser.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Recovery/Partitions/GPTParser.cpp)
* Reads LBA 1 for the GPT Header (`"EFI PART"` magic).
* Locates the Partition Entry LBA (typically LBA 2) and enumerates partition descriptors:
  - Maps well-known GUIDs (Microsoft Basic Data, EFI System, Linux Filesystem).
  - Translates 72-byte UTF-16LE partition names to standard UTF-8 strings.

---

## 4. Deep NTFS Unallocated Metadata Recovery (`Recovery/Filesystems/NTFS/`)

* **Files**: [MFTParser.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Recovery/Filesystems/NTFS/MFTParser.cpp), [DataRunParser.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Recovery/Filesystems/NTFS/DataRunParser.cpp), [NTFSFileSystem.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Recovery/Filesystems/NTFS/NTFSFileSystem.cpp)

When a file is deleted standardly by Windows, its MFT record is marked inactive (`flags & 0x0001 == 0`), but the record remains in the MFT until overwritten by new files:
1. `MFTParser` scans every 1024-byte record in `$MFT`.
2. Locates inactive records containing valid `"FILE"` headers.
3. Parses `$FILE_NAME` (`0x30`) attribute to recover:
   - Original filename
   - Parent folder MFT record reference (allowing full directory tree reconstruction)
   - Real creation, modification, and access timestamps.
4. Parses `$DATA` (`0x80`) attribute:
   - If resident: immediately extracts file data from the record.
   - If non-resident: `DataRunParser` decodes cluster runs and reads data clusters directly from disk.

---

## 5. The Sleuth Kit (TSK) & File Carving Subsystem

### 5.1 The Sleuth Kit (TSK) C++ Bridge
* **Files**: [Recovery/TSK/TskImageBridge.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Recovery/TSK/TskImageBridge.cpp), [TskFileSystem.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Recovery/TSK/TskFileSystem.cpp)
* Subclasses `TSK_IMG_INFO` to route all TSK read callbacks through our read-only storage abstraction.
* Leverages libtsk for deep inode walking, FAT directory chains, and ISO image parsing.

### 5.2 PhotoRec Integration & Deep Carving
* **Files**: [Recovery/Carving/PhotoRec/PhotoRecCarver.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Recovery/Carving/PhotoRec/PhotoRecCarver.cpp), [PhotoRecProcessRunner.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Recovery/Carving/PhotoRec/PhotoRecProcessRunner.cpp)
* When filesystem metadata is destroyed (or on raw unpartitioned disk images), file carving searches for file headers/footers in unallocated sectors.
* Orchestrates an automated PhotoRec worker process, streaming discovered files into the recovery queue.

### 5.3 Carved Artifact Verification Suite (`Recovery/Carving/Verification/`)
Carved files frequently contain trailing junk or false positives. The recovery subsystem subjects carved files to format-specific integrity verifiers:
* `JpegVerifier.cpp`: Validates `FF D8 FF` start, Exif/JFIF segment markers, and `FF D9` end-of-image.
* `PngVerifier.cpp`: Validates PNG header `89 50 4E 47 0D 0A 1A 0A`, `IHDR` chunk, and `IEND` trailer CRC.
* `PdfVerifier.cpp`: Validates `%PDF-` header and cross-reference table (`xref` / `%%EOF`).
* `ZipVerifier.cpp` / `OoxmlVerifier.cpp`: Validates ZIP central directory structure (DOCX, XLSX, PPTX).
* `IsoBmffVerifier.cpp`: Validates MP4 / MOV `ftyp` box headers.

---

## 6. Chain of Custody & Evidence Manifest

* **File**: [Recovery/Audit/EvidenceManifest.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Recovery/Audit/EvidenceManifest.cpp)
* Every recovered artifact is hashed via SHA-256.
* Generates an immutable, cryptographic JSON Evidence Manifest recording:
  - Source disk device ID and partition offset
  - Acquisition timestamp
  - Original and recovered filenames
  - Sector span and byte size
  - Cryptographic SHA-256 digest
