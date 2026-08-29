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