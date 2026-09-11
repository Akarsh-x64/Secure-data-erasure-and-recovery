# System Architecture

Enterprise- and Defense-Grade Multi-Filesystem Data Sanitization, Forensic Recovery, and Verification Engine.

---

## High-Level Flow

```text
User / Security Operator
          │
          │ User Interaction & Configuration
          ▼
Electron Desktop Frontend (React 19 + TypeScript + Tailwind CSS)
          │
          │ Typed ContextBridge IPC / Secure WebSocket Events
          ▼
Backend API & Orchestration (Python 3.14.7 + Flask + Flask-SocketIO)
          │
          ├── Privileged Gatekeeper (Windows RunAs / POSIX root)
          ├── Asynchronous Worker Threads (Drive Erase, File Erase, Forensic Scan)
          │
          ├────────────────────────────────────────┬────────────────────────────────────────┐
          │ Dynamic Execution                      │ Audit Persistence                      │
          ▼                                        ▼                                        ▼
pybind11 Native Extension Bridge          Cryptographic Audit Ledger              OS & Storage Hardware
(osdevice, hdd, ntfs, ext4,                (audit_logs.json with SHA-256           (Win32 DeviceIoControl,
 fat32, exfat, verification, recovery)     pre/post digests & timestamps)          POSIX O_DIRECT, TRIM)
          │
          ▼
Core C++17 Engines & Subsystems
  ├── Surgical Filesystem Drivers (NTFS, ext4, exFAT, FAT32, XFS)
  ├── Hardware Erasure Protocols (DoD 5220.22-M, NIST SP 800-88, NVMe Sanitize)
  ├── Adversarial Verification Loop (Shannon Entropy, Chi-Square, Monte Carlo Pi, 120+ Carvers)
  └── Read-Only Forensic Recovery Engine (MBR/GPT Parsers, MFT Reconstructor, Signature Carver)
          │
          │ Mathematical Verdicts, Progress Metrics & Carved Artifacts
          ▼
Backend Event Hub (Socket.IO Broadcast)
          │
          │ Real-Time Streaming Telemetry
          ▼
Electron Desktop Frontend (Mission Control Dashboard)
          │
          ▼
User / Verification Report Export
```

---

## Architecture Design Principles

1. **Least-Privilege Process Isolation**: The presentation layer (React renderer) is fully sandboxed within Electron. It never directly accesses disk handles, raw block devices, or privileged kernel boundaries.
2. **Decoupled 3-Tier Separation**:
   - Presentation Layer (Electron + React 19)
   - Application & Inter-Process Communication Layer (Python 3.14.7 + Flask + Socket.IO)
   - Native Bare-Metal Core (C++17 + pybind11)
3. **Zero-Bypass Hardware Erasure**: Filesystem-aware wiping directly overrides physical metadata tables (NTFS MFT records, FAT directory clusters, ext4 inodes) and physical sectors, eliminating remanence across slack spaces and unallocated clusters.
4. **Adversarial Post-Wipe Verification**: Overwritten sectors undergo immediate statistical entropy testing, Chi-Square goodness-of-fit calculations, serial correlation checks, and multi-format signature carving to mathematically prove irrecoverability.
5. **Tamper-Evident Accountability**: Every sanitization and forensic task produces an immutable audit record bearing operator IDs, device hardware serials, pre-wipe and post-wipe SHA-256 hashes, and deterministic pass/fail verdicts.

---

## Detailed Component Breakdown

### 1. Presentation & Control Layer (Frontend)

- **Technologies**: Electron 44, React 19, TypeScript 5, Vite 7, Tailwind CSS, Lucide Icons.
- **Responsibilities**:
  - Provides a mission-control desktop interface for system administrators and forensic investigators.
  - Enforces client-side request validation using structured models.
  - Renders live progress telemetry (bytes transferred, sector offsets, current pass index, throughput in MB/s, and calculated ETA).
  - Visualizes statistical entropy curves and forensic verification matrices.
- **Primary Modules**:
  - `DriveEraseTab`: Full physical disk and volume wiping selection, geometry inspections, and standard configuration (DoD 5220.22-M vs. NIST SP 800-88).
  - `FileEraseTab`: Interactive filesystem tree navigation for surgical file and directory eradication.
  - `RecoveryTab`: Read-only forensic image scanning, partition enumeration, and discovered artifact exports.
  - `AuditLogsTab`: Filterable, searchable audit trail inspector with cryptographic verification details.

### 2. IPC & Application Orchestration Layer (Backend API)

- **Technologies**: Python 3.14.7, Flask, Flask-SocketIO, Pydantic, pybind11 bindings.
- **Responsibilities**:
  - Acts as the central integration bus between the sandboxed desktop GUI and native C++ shared libraries.
  - Validates and deserializes incoming requests through strict Pydantic schemas (`DriveEraseRequest`, `FileEraseRequest`, `RecoveryScanRequest`).
  - Orchestrates background worker threads via `SocketIO.start_background_task` to prevent blocking the HTTP event loop during multi-gigabyte storage operations.
  - Enforces administrative escalation checks (`is_admin()` / `geteuid() == 0`) before executing raw storage I/O.
  - Emits granular real-time progress events over WebSockets (`operationId`, `phase`, `percent`, `currentSector`, `throughputMBps`).

### 3. Native C++17 Core Engines & Drivers

Compiled as static C++ libraries and exposed to Python 3.14.7 via high-throughput zero-copy pybind11 modules:

#### A. Surgical Erasure Engine (`Erasure/`)
- **OS Abstraction**:
  - Windows: Direct Win32 volume manipulation (`CreateFileW` with `FILE_FLAG_NO_BUFFERING` and `FILE_FLAG_WRITE_THROUGH`, `DeviceIoControl` for lock/dismount).
  - Linux: POSIX block device direct I/O (`open` with `O_DIRECT | O_SYNC`, `ioctl` for geometry and flush operations).
- **Filesystem Drivers**:
  - `NTFS`: Parses Volume Boot Record (VBR), locates Master File Table (MFT), parses `$MFT` record attributes (`$DATA`, `$STANDARD_INFORMATION`, `$FILE_NAME`), overwrites non-resident cluster runs, and wipes MFT records.
  - `ext4`: Parses superblock and block group descriptors, traverses inode tables, zeroes direct and extent-mapped blocks, and unlinks directory entry structures.
  - `FAT32 / exFAT`: Traverses File Allocation Tables, follows cluster chains, zeroes cluster data, and purges Directory Entry markers.
  - `XFS`: Traverses allocation groups, inode trees, and extent allocation btrees for atomic space reclamation.
- **Hardware Protocols**:
  - NIST SP 800-88 Rev. 1 Clear (Single-pass logical zero fill `0x00`).
  - DoD 5220.22-M (3-Pass: `0x00`, `0xFF`, and CSPRNG cryptographic pseudo-random pattern with read-verify pass).
  - Native controller sanitization (NVMe Sanitize, ATA Secure Erase, TRIM block deallocation).

#### B. Adversarial Verification & Audit Engine (`Erasure/Verification/`)
- Validates the effectiveness of sanitization through a rigorous mathematical pipeline:
  - **Shannon Entropy**: Calculates information density $H(X) = -\sum_{i=1}^{n} P(x_i) \log_2 P(x_i)$ across wiped sectors.
  - **Chi-Square ($\chi^2$) Goodness-of-Fit**: Tests byte uniformity against an ideal random distribution, computing exact degrees of freedom and $p$-values.
  - **Serial Correlation**: Checks whether consecutive byte pairs exhibit statistical independence.
  - **Monte Carlo $\pi$ Approximation**: Maps adjacent byte pairs to Euclidean coordinate points to measure distribution isotropy.
  - **Forensic Signature Carver**: Sweeps raw block boundaries against a signature database of 120+ known file formats (PDF, DOCX, ZIP, JPEG, ELF, PE) to mathematically confirm zero file fragments remain.

#### C. Read-Only Forensic Recovery Subsystem (`Recovery/`)
- Operates under strict read-only guarantees to prevent evidence spoliation.
- Parses partition layouts across raw disk images or physical disks (MBR, GPT).
- Scans unallocated sectors for residual MFT record artifacts (`FILE` magic signatures).
- Implements deep carving routines to salvage deleted or orphaned files with confidence scores and verification checksums.

### 4. Cryptographic Audit Ledger & State Persistence

- **File**: `audit_logs.json`
- **Concurrency**: Guarded by multi-threaded synchronization locks (`threading.Lock`).
- **Data Model**:
  - Unique log entry identifier (`log-xxxxxxxx`).
  - RFC 3339 UTC timestamp.
  - Operator identity tag.
  - Action category (`file-erase`, `drive-erase`, `file-recovery`, `system-init`).
  - Pre-wipe SHA-256 cryptographic digest.
  - Post-wipe SHA-256 verification signature.
  - Mathematical verification report metrics (entropy, Chi-Square, signatures checked/found).
  - Deterministic status verdict (`PASSED - ZERO RECOVERY GUARANTEE CONFIRMED`).

---

## Detailed Data Flow Sequences

### Sequence A: Surgical File Erasure Pipeline

```text
[Operator] ──(Selects Target Path & Algorithm)──> [React Frontend]
                                                          │
                                         POST /api/v1/erase/files/validate
                                                          ▼
                                                  [Flask API Server]
                                                          │
                                                  (Pydantic Validation)
                                                          ▼
[Operator] ──(Confirms Warning & Proceeds)─────> [React Frontend]
                                                          │
                                         POST /api/v1/erase/files (Idempotent)
                                                          ▼
                                            [Background Worker Thread]
                                                          │
                                  ┌───────────────────────┴───────────────────────┐
                                  ▼                                               ▼
                       [Read Pre-Wipe SHA-256]                         [Detect Filesystem]
                                  │                                               │
                                  └───────────────────────┬───────────────────────┘
                                                          ▼
                                            [Native C++ Filesystem Driver]
                                            (Targeted Cluster Overwrite + MFT/Inode Zero)
                                                          │
                                                          ▼
                                            [Cryptographic Overwrite Passes]
                                            (DoD 5220.22-M / NIST SP 800-88)
                                                          │
                                                          ▼
                                            [Verification Engine Audit]
                                            (Entropy + Chi-Square + 120+ Carvers)
                                                          │
                                                          ▼
                                            [Generate Post-Wipe SHA-256]
                                                          │
                                                          ▼
                                            [Commit to Audit Ledger]
                                                          │
                                                          ▼
                                           Socket.IO Telemetry Broadcast
                                                          │
                                                          ▼
                                              [React Frontend Dashboard]
```

### Sequence B: Forensic Recovery Pipeline

```text
[Investigator] ──(Selects Raw Image / Disk Handle)──> [React Frontend]
                                                              │
                                            POST /api/v1/recovery/scans
                                                              ▼
                                               [Background Recovery Worker]
                                                              │
                                            [C++ Native Recovery Engine]
                                                              │
                              ┌───────────────────────────────┴───────────────────────────────┐
                              ▼                                                               ▼
                     [Partition Scanner]                                            [Signature Carver]
                 (Parse MBR/GPT Tables)                                     (Scan 120+ Magic Header/Footers)
                              │                                                               │
                              └───────────────────────────────┬───────────────────────────────┘
                                                              ▼
                                            [Structure & Offset Validation]
                                            (Compute Confidence Metric & Hashes)
                                                              │
                                                              ▼
                                             [Generate recovery_result.json]
                                                              │
                                                              ▼
                                             [Commit Scan Action to Audit Log]
                                                              │
                                                              ▼
                                                Socket.IO 'completed' Event
                                                              │
                                                              ▼
[Investigator] <──(Inspects Discovered Files)───────── [Recovery Workspace]
```

---

## Security, Safety, and Defensive Controls

1. **System Drive Protection**: The engine enforces strict safety filters blocking operations targeting active OS root partitions (`C:\Windows`, `/`, `/boot`, `/etc`) unless explicit safety overrides are provided.
2. **Idempotency Protection**: Destructive operations require unique client-generated idempotency keys in request headers (`Idempotency-Key`) preventing accidental double-triggering or duplicate job queuing.
3. **Non-Blocking Execution**: Heavy I/O processing is completely decoupled from the HTTP server loop, ensuring the UI remains responsive and cancellation tokens can be processed cleanly.
4. **Forensic Integrity**: The recovery engine opens all targets with read-only flags (`O_RDONLY` / `GENERIC_READ`), guaranteeing zero sector modification during evidence scanning.

