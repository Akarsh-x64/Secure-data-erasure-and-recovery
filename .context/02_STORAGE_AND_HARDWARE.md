# Storage & Hardware Abstraction Subsystem

## 1. Overview

The Storage and Hardware layer decouples OS-specific block device I/O from hardware-specific sanitization commands. This ensures that the upper filesystem drivers (`IFileSystemDriver`) interact exclusively with clean, abstract sector operations without embedding operating system or device bus logic.

---

## 2. Layer 1: OS Storage Pipes (`IStorageDevice`)

### 2.1 Interface Definition
Defined in [Erasure/Core/IStorageDevice.h](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/Core/IStorageDevice.h):
```cpp
namespace Erasure::Core {
    class IStorageDevice {
    public:
        virtual ~IStorageDevice() = default;
        virtual bool Open(const std::string& devicePath) = 0;
        virtual void Close() = 0;
        virtual bool ReadSectors(uint64_t startSector, uint32_t sectorCount, uint8_t* outBuffer) = 0;
        virtual bool WriteSectors(uint64_t startSector, uint32_t sectorCount, const uint8_t* inBuffer) = 0;
        virtual bool SendDeviceCommand(uint32_t commandCode, void* inBuffer, size_t inSize, void* outBuffer, size_t outSize) = 0;
        virtual bool LockVolume() = 0;
        virtual bool UnlockVolume() = 0;
        virtual bool DismountVolume() = 0;
        virtual DeviceGeometry GetGeometry() const = 0;
        virtual bool IsOpen() const = 0;
    };
}
```

### 2.2 Windows Implementation (`WindowsStorageDevice`)
* **File**: [Erasure/OS/Windows/WindowsStorageDevice.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/OS/Windows/WindowsStorageDevice.cpp)
* **Handle Acquisition**: Uses `CreateFileA` with `GENERIC_READ | GENERIC_WRITE`, `FILE_SHARE_READ | FILE_SHARE_WRITE`, and `OPEN_EXISTING`.
* **Path Formats**:
  - Raw physical disks: `\\.\PhysicalDrive0`, `\\.\PhysicalDrive1`
  - Logical volumes: `\\.\C:`, `\\.\E:`
* **Volume Locking & Dismounting**:
  - `LockVolume()` sends `FSCTL_LOCK_VOLUME` via `DeviceIoControl`.
  - `DismountVolume()` sends `FSCTL_DISMOUNT_VOLUME`. This invalidates existing OS filesystem caches and prevents Windows from writing cached dirty buffers over freshly wiped sectors.
  - `UnlockVolume()` sends `FSCTL_UNLOCK_VOLUME`.
* **Dynamic Geometry Detection Cascade**:
  1. Primary: Queries `IOCTL_DISK_GET_DRIVE_GEOMETRY_EX` (`DISK_GEOMETRY_EX`).
  2. Volume Fallback: Since `IOCTL_DISK_GET_DRIVE_GEOMETRY_EX` frequently fails on volume handles (e.g. `\\.\E:`), the driver executes a fallback cascade querying `IOCTL_DISK_GET_LENGTH_INFO` (for total partition capacity) and `IOCTL_DISK_GET_DRIVE_GEOMETRY` (for sector size).
  3. Safe Default: If queries fail, falls back safely to standard 512-byte sector geometry.
* **Sector Reading & Writing**:
  - Multiplies `startSector` by `bytesPerSector` using 64-bit arithmetic to determine the absolute file pointer offset (`LARGE_INTEGER`).
  - Calls `SetFilePointerEx` followed by `ReadFile` / `WriteFile`.

### 2.3 Linux Implementation (`LinuxStorageDevice`)
* **File**: [Erasure/OS/Linux/LinuxStorageDevice.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/OS/Linux/LinuxStorageDevice.cpp)
* **Direct I/O**: Opens devices using `open(devicePath.c_str(), O_RDWR | O_DIRECT | O_SYNC)`.
  - `O_DIRECT`: Bypasses the Linux page cache, guaranteeing that reads and writes hit physical flash/platters directly without kernel cache pollution.
  - `O_SYNC`: Blocks until the storage controller acknowledges that bytes are committed to persistent media.
* **Geometry Detection**: Uses `ioctl(fd, BLKGETSIZE64, &totalBytes)` and `ioctl(fd, BLKSSZGET, &sectorSize)`.
* **Aligned Buffering**: Linux `O_DIRECT` requires buffers to be aligned to physical sector boundaries. `LinuxStorageDevice` manages aligned memory using `posix_memalign`.

---

## 3. Layer 2: Hardware Demolition Controllers (`IHardwareController`)

### 3.1 Interface Definition
Defined in [Erasure/Core/IHardwareController.h](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/Core/IHardwareController.h):
```cpp
namespace Erasure::Core {
    class IHardwareController {
    public:
        virtual ~IHardwareController() = default;
        virtual bool SecureEraseSectors(uint64_t startSector, uint64_t sectorCount) = 0;
        virtual bool SecureEraseDrive() = 0;
        virtual bool ReadSectors(uint64_t startSector, uint32_t sectorCount, uint8_t* outBuffer) = 0;
        virtual bool WriteSectors(uint64_t startSector, uint32_t sectorCount, const uint8_t* inBuffer) = 0;
        virtual DeviceGeometry GetGeometry() const = 0;
    };
}
```

### 3.2 Magnetic HDD & Virtual Disk Controller (`HDDController`)
* **File**: [Erasure/Hardware/Magnetic/HDDController.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/Hardware/Magnetic/HDDController.cpp)
* **Target Hardware**: Traditional spinning magnetic HDDs, VHD/VMDK virtual disks, and encrypted VeraCrypt containers.

#### DoD 5220.22-M 3-Pass Overwrite Standard
To guarantee data destruction on magnetic media, `HDDController` executes an authentic 3-pass physical cycle:
1. **Pass 1 (0x00 / Ground Saturation)**: Overwrites all sectors in the target range with all zeroes (`0x00` / binary `00000000`). This drives all magnetic domains to ground polarity.
2. **Pass 2 (0xFF / Positive Saturation)**: Overwrites all sectors in the target range with all ones (`0xFF` / binary `11111111`). This reverses magnetic orientation across all domains.
3. **Pass 3 (PRNG Noise / Domain Randomization)**: Overwrites the target span with high-entropy pseudo-random noise generated by a 64-bit Mersenne Twister engine (`std::mt19937_64` seeded with hardware `std::random_device`).

```
LBA Span: [startSector ... startSector + sectorCount]
Pass 1: 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 ... (Ground Saturation)
Pass 2: 0xFF 0xFF 0xFF 0xFF 0xFF 0xFF 0xFF 0xFF ... (Reverse Saturation)
Pass 3: 0xD4 0x8A 0x19 0xF2 0x7B 0x3E 0x04 0x91 ... (PRNG Domain Scramble)
```

#### Bounded Chunking Architecture
Older sanitization tools frequently crash with `std::bad_alloc` when attempting to allocate `sectorCount * sectorSize` on the heap for large spans (e.g. 50 GB wipes). 

`HDDController` enforces a **chunked streaming model**:
```cpp
const uint32_t CHUNK_SECTORS = 1024; // 512 KB per chunk on 512-byte drives
std::vector<uint8_t> buffer(CHUNK_SECTORS * sectorSize);

uint64_t remaining = sectorCount;
uint64_t currentSector = startSector;

while (remaining > 0) {
    uint32_t toWrite = static_cast<uint32_t>(std::min<uint64_t>(remaining, CHUNK_SECTORS));
    m_device->WriteSectors(currentSector, toWrite, buffer.data());
    currentSector += toWrite;
    remaining -= toWrite;
}
```
This guarantees constant memory usage ($O(1)$ RAM overhead) regardless of whether the wipe targets 1 megabyte or 10 terabytes.

---

## 4. Solid-State Storage (NVMe & SATA SSDs)

### 4.1 The Flash Wear-Leveling Challenge
Solid-state drives (SSDs) use Flash Translation Layers (FTL) to balance wear across NAND flash blocks. When an OS overwrites LBA 100, the FTL does not write to the same physical NAND cell; it writes to a new cell and marks the old cell for garbage collection. Standard pattern overwrites may therefore leave "ghost" data in uncollected NAND blocks.

### 4.2 NVMe Sanitization Commands
For NVMe storage, SanitizeX leverages dedicated hardware pass-through commands via `NVMeController`:
1. **Format NVM (`SES=1` / User Data Erase)**: Commands the NVMe controller to erase user data across all namespaces.
2. **NVMe Sanitize (Block Erase)**: Executes a low-level electrical block erase of all NAND blocks, including over-provisioned areas and bad block retirement tables.
3. **NVMe Sanitize (Crypto Scramble)**: Changes the internal hardware encryption key, rendering all existing flash ciphertext instantaneously unrecoverable.
4. **TRIM / Deallocate**: Marks LBAs as unallocated, signaling the FTL to clear LBA-to-physical block mappings.

### 4.3 The Metadata Limitation & Fallback Strategy
* **Flash Block Size**: SSD erase blocks are typically 1 MB to 8 MB in size.
* **Metadata Granularity**: Filesystem metadata records (such as 32-byte exFAT/FAT32 directory entries or 256-byte ext4 inodes) are far smaller than a flash block. Issuing a TRIM on metadata would destroy adjacent active files.
* **Engine Strategy**:
  - Metadata is overwritten with surgical `WriteSectors` zero/pattern updates.
  - Large user data runs are purged using TRIM or multi-pass overwrites.
  - Full drive decommissioning leverages bare-metal NVMe Sanitize commands (supported in the LiveBoot OS).
