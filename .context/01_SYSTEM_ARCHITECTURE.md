# SanitizeX System Architecture & Core Philosophy

## 1. The Core Problem: The Deletion Illusion

In modern operating systems (Windows, Linux, macOS), issuing a file deletion command (such as calling `unlink()`, `remove()`, or moving a file to the Recycle Bin / Trash) **never erases data from physical disk sectors**. 

Instead, the OS performs a shallow metadata update:
* **NTFS**: Clears the in-use flag (`0x0001`) in the file's MFT record and marks the corresponding cluster bits in `$Bitmap` as unallocated.
* **ext4**: Unlinks the directory entry, sets the deletion epoch `i_dtime`, and clears bits in the block/inode allocation bitmaps.
* **exFAT / FAT32**: Marks the directory entry with `0xE5` (or clears the in-use bit in exFAT) and marks the cluster chain in the FAT table as free.

**The underlying user data payload remains 100% intact on the physical storage media.** Any standard forensic tool (such as PhotoRec, Scalpel, Autopsy, or The Sleuth Kit) can immediately reconstruct the original files.

**SanitizeX** solves this problem by completely bypassing high-level filesystem APIs, acquiring direct physical volume handles, locating the exact disk sectors containing user payloads and metadata, and forensically destroying both.

---

## 2. The 3-Layer Decoupled Erasure Architecture

The erasure engine strictly enforces a zero-leakage, decoupled 3-tier boundary:

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
│ 3. FILESYSTEM LAYER      │ NtfsDriver, XfsDriver, Ext4Driver, ExFatDriver, Fat32Driver │
│    (IFileSystemDriver)   │ Structural parsing, extent resolution, metadata sanitization│
└──────────────────────────┴─────────────────────────────────────────────────────────────┘
```

### Layer 1: OS Layer (`IStorageDevice`) — The Pipe
* **Header**: [Erasure/Core/IStorageDevice.h](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/Core/IStorageDevice.h)
* **Implementations**:
  - [WindowsStorageDevice.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/OS/Windows/WindowsStorageDevice.cpp) (Win32)
  - [LinuxStorageDevice.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/OS/Linux/LinuxStorageDevice.cpp) (POSIX)
* **Role**: Acts as a "dumb pipe". It exposes basic sector read/write capabilities (`ReadSectors`, `WriteSectors`), pass-through device command execution (`SendDeviceCommand`), and OS-level volume locking (`LockVolume`, `DismountVolume`, `UnlockVolume`).
* **Isolation**: This layer has zero knowledge of filesystem structures or overwrite patterns.

### Layer 2: Hardware Layer (`IHardwareController`) — Demolition
* **Header**: [Erasure/Core/IHardwareController.h](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/Core/IHardwareController.h)
* **Implementations**:
  - [HDDController.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/Hardware/Magnetic/HDDController.cpp): Enforces multi-pass overwrites (DoD 5220.22-M 3-pass).
  - Hardware-level controllers for NVMe (`Format NVM`, `Sanitize`, `TRIM`) and ATA (`SECURITY ERASE UNIT`).
* **Role**: Determines *how* sectors are obliterated based on physical media characteristics.
* **Virtual Disks**: VHDs, VMDKs, and VeraCrypt containers reject hardware pass-through commands and transparently fallback to `HDDController` for physical pattern overwrites.

### Layer 3: Filesystem Layer (`IFileSystemDriver`) — Detective
* **Header**: [Erasure/Core/IFileSystemDriver.h](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/Core/IFileSystemDriver.h)
* **Implementations**:
  - `NtfsDriver` ([Erasure/File Systems/NTFS/NTFS.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/File%20Systems/NTFS/NTFS.cpp))
  - `Ext4Driver` ([Erasure/File Systems/ext4/ext4.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/File%20Systems/ext4/ext4.cpp))
  - `ExFatDriver` ([Erasure/File Systems/exFAT/exFAT.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/File%20Systems/exFAT/exFAT.cpp))
  - `Fat32Driver` ([Erasure/File Systems/FAT32/FAT32.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/File%20Systems/FAT32/FAT32.cpp))
  - `XfsDriver` ([Erasure/File Systems/XFS/XFS.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/File%20Systems/XFS/XFS.cpp))
* **Role**: Parses raw disk blocks to reconstruct the logical hierarchy. When given a target path (e.g. `EraseFile("secret.docx")`):
  1. Locates the physical sectors of the file's data payload.
  2. Commands Layer 2 to execute surgical sanitization on those exact sectors.
  3. Updates and zeroes the on-disk metadata records (MFT records, Inodes, Directory entries, Allocation Bitmaps) without corrupting the surrounding filesystem.

---

## 3. Engine Safety & Implementation Rules

### 1. Zero Hardcoded Offsets
Never assume fixed partition boundaries, sector sizes, cluster sizes, or MFT record locations. All geometries are parsed dynamically from the Volume Boot Record (VBR), Superblock, or BPB at runtime:
* Sector sizes may be 512 bytes (512n/512e) or 4096 bytes (4Kn native).
* Cluster sizes range from 512 bytes to 64 KB (or up to 32 MB on exFAT).

### 2. Strict Struct Packing
All on-disk binary representations (boot records, inode headers, MFT attribute headers) are wrapped with:
```cpp
#pragma pack(push, 1)
struct OnDiskStructure {
    // raw layout
};
#pragma pack(pop)
```
This eliminates compiler padding and misalignment across architectures.

### 3. Bounded Memory Allocations
To prevent heap exhaustion (`std::bad_alloc`) or 32-bit arithmetic overflows when wiping multi-gigabyte or multi-terabyte spans, sector overwrites are chunked into bounded blocks (typically 1024 sectors / 512 KB per pass).

### 4. RAII Resource Ownership
All Win32 `HANDLE` objects and POSIX file descriptors are encapsulated in RAII wrappers to guarantee that volume locks (`FSCTL_UNLOCK_VOLUME`) and device handles are automatically released upon function return or exception.

---

## 4. Privilege & Elevation Model

Low-level disk I/O requires elevated administrative rights:
* **Windows**:
  - Accessing raw physical disks (`\\.\PhysicalDriveX`) or raw volume extents (`\\.\E:`) requires Administrator privileges.
  - The application manifest specifies `<requestedExecutionLevel level="requireAdministrator" uiAccess="false" />`.
  - Python scripts (`backend/main.py` and `modules/test.py`) verify elevation via `ctypes.windll.shell32.IsUserAnAdmin()` and automatically trigger a UAC prompt using `ShellExecuteW(None, "runas", ...)`.
* **Linux**:
  - Accessing raw block devices (`/dev/sda`, `/dev/nvme0n1`) requires root privileges (`uid == 0`) or `CAP_SYS_RAWIO` capabilities.
