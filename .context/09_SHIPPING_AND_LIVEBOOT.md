# Shipping Formats & LiveBoot Subsystem Architecture

## 1. Executive Summary & Distribution Strategy

To satisfy varying operational environments, government compliance mandates, and hardware security boundaries, **SanitizeX** is engineered to ship in **three distinct distribution formats**:

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                               3 PRODUCT SHIPPING TARGETS                               │
├──────────────────────────┬─────────────────────────────┬───────────────────────────────┤
│ FORMAT 1: WINDOWS APP    │ FORMAT 2: LINUX APP         │ FORMAT 3: LIVE BOOT OS (ISO)  │
│ - Portable .exe / MSI    │ - AppImage / .deb / .rpm    │ - Standalone RAM-disk Live OS │
│ - Electron + Win32 C++   │ - Electron + POSIX C++      │ - Custom Linux Live (toram)   │
│ - Wipes secondary drives │ - Wipes secondary drives    │ - 100% UNLOCKED HARDWARE      │
│ - Restricted on C:\      │ - Restricted on root (/)    │ - Wipes C:\ & System Boot M.2 │
└──────────────────────────┴─────────────────────────────┴───────────────────────────────┘
```

---

## 2. Format 1: Windows Native Desktop Application

* **Target OS**: Windows 10, Windows 11, Windows Server 2016/2019/2022 (x64 / ARM64).
* **Packaging**: Single-file portable `.exe` or standard `.msi` Windows Installer built via `electron-builder`.
* **Privilege Elevation**:
  - Embedded application manifest: `<requestedExecutionLevel level="requireAdministrator" uiAccess="false" />`.
  - Triggers a standard User Account Control (UAC) prompt upon launch.
* **Operational Capabilities**:
  - Full surgical erasure and file recovery on secondary drives, external USB thumb drives, memory cards, and secondary partitions (`D:`, `E:`, `X:`).
* **The OS Restriction**:
  - Under running Windows, the operating system kernel holds active file locks on `C:\pagefile.sys`, registry hives, and system binaries.
  - Windows rejects volume locking (`FSCTL_LOCK_VOLUME`) and raw sector overwrites on the active system drive (`C:\`).

---

## 3. Format 2: Linux Native Desktop Application

* **Target OS**: Ubuntu, Debian, RHEL, Fedora, Arch Linux (x86_64).
* **Packaging**: Standalone AppImage, `.deb` package, or `.rpm` package.
* **Privilege Elevation**: Launched via `pkexec` or `sudo`.
* **Operational Capabilities**:
  - Direct I/O (`O_DIRECT | O_SYNC`) against secondary physical disks (`/dev/sdb`, `/dev/nvme1n1`) and mounted partitions.
  - Just like in Windows, Linux prevents unmounting or obliterating the active root filesystem (`/`) while the kernel is running from it.

---

## 4. Format 3: Dedicated RAM-Disk LiveBoot OS (Wiping C: and System NVMe)

### 4.1 The Fundamental Engineering Problem
How can an organization securely decommission an entire laptop or server — completely wiping the primary Windows `C:\` drive or Linux root NVMe — without disassembling the chassis or removing soldered M.2 SSDs?

A running operating system cannot destroy itself from within.

### 4.2 The LiveBoot Solution: 100% RAM Execution (`toram`)
SanitizeX Format 3 is a custom, minimalist, hardened Linux distribution (based on customized Alpine/Debian) designed to boot from a USB flash drive or PXE network boot into system memory:

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                        FORMAT 3: LIVE BOOT ENVIRONMENT ARCHITECTURE                    │
├────────────────────────────────────────────────────────────────────────────────────────┤
│                                                                                        │
│   USB Flash Drive / ISO Boot (UEFI / Legacy BIOS)                                      │
│         │                                                                              │
│         ▼                                                                              │
│   Linux Kernel boots with 'toram' parameter                                            │
│         │                                                                              │
│         ▼                                                                              │
│   Entire OS (Kernel + initramfs + Alpine base + Electron GUI + C++ Engine)             │
│   is copied into Host RAM (tmpfs)                                                      │
│         │                                                                              │
│         ▼                                                                              │
│   USB Drive can be safely unplugged!                                                   │
│         │                                                                              │
│         ▼                                                                              │
│   HOST STORAGE DRIVES (NVMe M.2 / SATA SSD / C: Drive) ARE 100% UNMOUNTED & OFFLINE    │
│   ┌────────────────────────────────────────────────────────────────────────────────┐   │
│   │ Exclusive, Unrestricted Raw Hardware Access:                                  │   │
│   │ 1. ATA Secure Erase & Sanitize (Crypto Scramble / Block Erase)                 │   │
│   │ 2. NVMe Format NVM & Sanitize (User Data Erase across all namespaces)          │   │
│   │ 3. 3-Pass DoD 5220.22-M Overwrite on all physical LBA sectors of C: Drive      │   │
│   │ 4. Re-initialization of clean partition tables (GPT/MBR) and fresh filesystems │   │
│   └────────────────────────────────────────────────────────────────────────────────┘   │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

### 4.3 Technical Implementation Details
1. **Zero-Lock Guarantee**:
   - The host computer's internal storage drives appear purely as raw block devices: `/dev/nvme0n1` (the primary Windows M.2 drive) and `/dev/sda` (internal SATA drive).
   - Because Windows is **not running**, zero kernel locks exist, no pagefile is active, and no filesystem drivers hold cache buffers.
   - SanitizeX has exclusive, unrestricted access from Sector 0 (MBR/GPT) through the final LBA of the drive.
2. **Hardware Demolition Capabilities in LiveBoot**:
   - **NVMe Hardware Sanitize**: Issues raw NVMe Admin commands (`Format NVM` with `SES=1` or `Sanitize` block erase) targeting the primary controller, instantly purging flash blocks across all namespaces including over-provisioned areas.
   - **ATA Secure Erase**: Issues pass-through `SECURITY ERASE UNIT` commands to SATA SSDs and HDDs.
   - **DoD 5220.22-M 3-Pass Wipe**: Overwrites the entire physical LBA span ($0 \dots \text{MaxLBA}$) with `0x00` $\to$ `0xFF` $\to$ PRNG noise.
   - **Pristine Partition Creation**: Writes a brand-new GPT table and clean filesystem so the machine is ready for a fresh OS installation or decommissioning.

---

## 5. Comparative Feature Matrix Across All 3 Formats

| Feature / Capability | Format 1: Windows App | Format 2: Linux App | Format 3: LiveBoot OS |
|---|---|---|---|
| **Target OS** | Windows 10 / 11 | Ubuntu / Debian / RHEL | Independent (Hardware Boot) |
| **Packaging** | `.exe` / `.msi` | `.deb` / AppImage | Bootable ISO / USB Image |
| **Execution Medium** | Host Internal Storage | Host Internal Storage | Host RAM (`tmpfs` / `toram`) |
| **Wipe Secondary Disks** | Yes (Full DoD / NIST) | Yes (Full DoD / NIST) | Yes (Full DoD / NIST) |
| **Wipe Active Windows `C:\`**| Blocked by Windows | N/A | **100% Fully Unlocked** |
| **NVMe Bare-Metal Sanitize** | Limited by Win32 driver| Requires unmounted drive| **Direct Controller Access** |
| **Forensic Recovery Engine** | Active | Active | Active |
| **Verification & Certificates**| Active (PDF / JSON) | Active (PDF / JSON) | Active (Saved to USB / Net) |
