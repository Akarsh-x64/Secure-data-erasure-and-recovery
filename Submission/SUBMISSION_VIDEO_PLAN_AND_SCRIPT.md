# SanitizeX: Secure Erasure, Forensic Recovery & LiveBoot Engine
## Official 5-Minute SIH Submission Video Blueprint, Storyboard & Manim Architecture

---

## 1. Executive Summary & Product Philosophy

**SanitizeX** is an enterprise- and defense-grade, dual-engine data security platform engineered in C++17. It addresses the fundamental flaw of modern computing: **standard operating system "deletion" is an illusion that leaves 100% of confidential data intact on physical storage sectors.**

Unlike single-purpose disk cleaners, **SanitizeX** provides an end-to-end data lifecycle solution:
1. **Military-Grade Surgical Erasure Engine**: Bypasses the OS kernel to directly parse raw on-disk filesystem structures (NTFS, ext4, ext2, ext3, exFAT, FAT32, XFS), obliterating both raw data clusters and lingering metadata via DoD 5220.22-M 3-pass and NIST SP 800-88 standards without corrupting filesystem headers.
2. **Forensic Recovery Subsystem**: Built upon peer-reviewed forensic literature (Carrier, Garfinkel, Roussev), providing deep unallocated metadata reconstruction and file carving through an immutable `IReadOnlyStorage` interface that physically prevents write corruption.
3. **Adversarial Verification Loop**: Uses the recovery engine and statistical tests (Shannon entropy, Chi-square, carver audits) to mathematically certify data destruction.
4. **Custom Hardware LiveBoot OS**: A dedicated, ram-disk Linux environment compiled over 24 hours that runs completely in `tmpfs`, bypassing the active Windows `C:\` drive lock to achieve bare-metal NVMe Sanitize and ATA Secure Erase.

---

## 2. Master Timing & Cue Sheet (Total Duration: 4 Minutes 50 Seconds)

```
0:00        0:15        0:30        0:50                    1:50                    2:50        3:05                    3:45            4:15            4:35        4:50
├── Intro ──┼─ Teaser ──┼─ Overview ┼─── Deletion Theory ───┼── Live Deletion Demo ─┼─ Matrix ──┼── Recovery Theory ────┼─ Recov Demo ──┼─ LiveBoot ────┼─ Finale ──┤
│ (15s)     │ (15s)     │ (20s)     │   (NTFS & ext4)       │   (Real Screen Demo)  │ (15s)     │   (Research Papers)   │ (Real Demo)   │ (Custom OS)   │ (15s)     │
│ Character │ Live Hook │ Dual-Core │   (60s Manim + Char)  │   (60s Terminal/GUI)  │ Character │   (40s Manim + Char)  │ (30s Screen)  │ (20s Footage) │ Character │
```

| Cue / Timestamp | Visual Source | On-Screen Display (OSD) / Graphics | Spoken Voiceover Narration |
|---|---|---|---|
| **0:00 – 0:15** (15s) | **Manim Animation**: 3b1b-style Character ("Chip") appears on dark slate background with neon grid. | Title Card: **SanitizeX**<br>*Defense-Grade Data Destruction & Forensic Recovery* | *"When you press 'Delete' or empty your Recycle Bin, your operating system lies to you. It only unticks a box, leaving your passwords, financial records, and defense secrets wide open to anyone with free recovery tools. I’m Chip, and today we’re presenting SanitizeX — the world’s most comprehensive, certified data erasure and forensic recovery engine."* |
| **0:15 – 0:30** (15s) | **Live Demo Screen Recording (Teaser Hook)**: Split screen. Left: Windows Explorer deleting `confidential.pdf`. Right: Live Hex Viewer showing the payload still fully intact in physical sectors. One click of SanitizeX $\to$ sectors turn to cryptographic noise! | Banner: **REAL-TIME DEMO PREVIEW**<br>Red text: *Raw Payload Exposed!* $\to$ Green text: *Surgically Destroyed!* | *"Watch this: Windows reports this file is deleted. But look at our raw physical sector scanner — every single byte of confidential text is still sitting on metal. With one click of SanitizeX, the data and its forensic metadata are permanently eradicated."* |
| **0:30 – 0:50** (20s) | **Manim Animation**: Dual glowing cores split on screen. Left: Crimson Core (*Surgical Erasure*). Right: Emerald Core (*Forensic Recovery*). An adversarial audit loop connects them. | Diagram: **Dual-Core Architecture**<br>Left: *3-Layer Decoupled Erasure*<br>Right: *Kernel-Enforced Read-Only Recovery* | *"SanitizeX is built on two synchronized engines: First, a surgical erasure pipeline that destroys files directly at the metal layer. Second, an enterprise-grade forensic recovery engine that recovers lost data after disasters — and acts as an adversarial auditor to mathematically prove that our deletions can never be reversed."* |
| **0:50 – 1:20** (30s) | **Manim Animation (Deletion Deep Dive 1: NTFS)**: Chip points to animated disk sectors.<br>1. Handle opens `\\.\PhysicalDrive0` with `FSCTL_LOCK_VOLUME`.<br>2. VBR LBA 0 $\to$ Cluster size & `$MFT` start LCN.<br>3. MFT Record 0 $\to$ Record 5 (`$Root`).<br>4. Traverses B-Tree (`$INDEX_ROOT`/`$INDEX_ALLOCATION`).<br>5. Decodes Data Runs $\to$ Physical Clusters.<br>6. 3-Pass DoD wipe (`0x00` $\to$ `0xFF` $\to$ PRNG).<br>7. Wipes 1024-byte MFT record, updates USA fixups, scrubs B-tree. | Diagram: **NTFS Surgical Pipeline**<br>• VBR $\to$ MFT Record 5 (Root)<br>• B-Tree Resolution<br>• 3-Pass DoD Overwrite<br>• USN Fixup Correction | *"Here is how we erase data on NTFS. We completely bypass the Windows file manager by opening raw physical drive handles with exclusive volume locks. We read the Volume Boot Record at LBA 0, trace the Master File Table to Record 5, and descend through the directory B-Tree. Once we locate the file's MFT record, we decode the compressed data runs to find the exact physical clusters. We then execute a DoD 5220.22-M 3-pass overwrite — zeroes, ones, and pseudo-random noise — zero the entire 1024-byte MFT record, re-calculate Update Sequence Array fixups, and scrub the directory index so Windows never encounters corruption."* |
| **1:20 – 1:40** (20s) | **Manim Animation (Deletion Deep Dive 2: ext4)**: Chip transitions to ext4 layout.<br>1. Superblock (Offset 1024, magic `0xEF53`).<br>2. Block Group Descriptor Table $\to$ Inode Table.<br>3. Inode 2 (`/`) $\to$ linked-list directory entries (`Ext4DirEntry2`).<br>4. Traverses Extent Tree (`eh_magic = 0xF30A`).<br>5. 3-pass DoD block wipe $\to$ Inode zeroing $\to$ stamps `i_dtime` $\to$ clears Block & Inode bitmaps $\to$ preserves directory `rec_len`. | Diagram: **ext4 Surgical Pipeline**<br>• Superblock (`0xEF53`) $\to$ GDT<br>• Inode 2 Root $\to$ Extent Tree (`0xF30A`)<br>• 3-Pass DoD Block Wipe<br>• Allocation Bitmap Release | *"On Linux ext4, the process is just as surgical. We parse the Superblock at offset 1024, load the Group Descriptor Table, and access the Inode Table starting at Root Inode 2. We resolve the file through directory entries, decode the 48-bit physical extent tree, and carpet-bomb the allocated blocks 3 times. We zero the on-disk 256-byte inode, stamp the deletion epoch, clear both block and inode allocation bitmaps, and unlink the directory entry while preserving record length to maintain filesystem consistency."* |
| **1:40 – 1:50** (10s) | **Manim Animation (4 Metrics in 1 Line Each)**: Four animated cards slide in with live graphs: Shannon Entropy, Chi-Square, Carver Audit, Cryptographic Hash. | Cards:<br>1. **Shannon Entropy**: $H \to 8.0$ bits/byte (pure randomness).<br>2. **Chi-Square**: $p$-value uniformity across all 256 byte bins.<br>3. **Carver Check**: 0 lingering file signatures.<br>4. **Pre/Post SHA-256**: 0.00% residual data match. | *"How do we verify it? We evaluate four mathematical metrics: Shannon Entropy, ensuring information density hits 8.0 bits per byte; Chi-Square distribution, confirming perfectly uniform byte dispersion; Signature Carving, proving zero residual headers exist; and pre-to-post SHA-256 digests, guaranteeing a 0.000% bit match."* |
| **1:50 – 2:50** (60s) | **Live Deletion Demo (Full 60s Execution)**:<br>1. Show terminal running `main.exe` / `demo_erasure_xxd`.<br>2. Inspect raw hex of synthetic NTFS/ext4 volume: cleartext credentials visible.<br>3. Run `EraseFile("passwords.txt")`: live log showing 3-pass DoD wipe, MFT record zeroed.<br>4. Inspect hex again: data replaced by PRNG noise, MFT record is `0x00`.<br>5. Run full `WipeVolume`: all data clusters cleared, root directory preserved.<br>6. Run `chkdsk` / `e2fsck -f`: **Exit Code 0! 0 bad sectors, 0 corruptions!** | Terminal & Hex Viewer:<br>`[Erasure] 3-Pass DoD Executed.`<br>`[xxd] Sector 100: dddd dddd...`<br>`[chkdsk] Windows has scanned the file system and found no problems.` | *(Voiceover narrates live demo commands, pointing out the cleartext password before wipe, the instant PRNG overwrite, the zeroed MFT header, and highlighting that running chkdsk or e2fsck returns zero errors, proving zero filesystem corruption).* |
| **2:50 – 3:05** (15s) | **Manim Animation (Compatibility Matrix & Hardware)**: Grid of animated icons light up: NTFS, ext4, ext2, ext3, exFAT, FAT32, XFS; NVMe, SATA SSD, Magnetic HDD, USB Thumb Drives, SD Cards; Windows and Linux. | Graphic: **Universal Ecosystem Support**<br>• 7 Filesystems (NTFS, ext4, ext3, ext2, exFAT, FAT32, XFS)<br>• Any Hardware (NVMe, SSD, HDD, USB)<br>• Windows & Linux Native | *"Our decoupled engine supports every major filesystem — NTFS, ext4, ext3, ext2, exFAT, FAT32, and XFS — across all storage mediums, including spinning magnetic disks, SATA SSDs, high-speed NVMe drives, and USB thumb drives, with native support for both Windows and Linux."* |
| **3:05 – 3:45** (40s) | **Manim Animation (Recovery Theory & Research Papers)**: Chip introduces the forensic literature.<br>1. Citations appear: *Carrier (2005)* for MFT metadata recovery, *Garfinkel (2007)* for object carving, *Scalpel (Richard & Roussev, 2005)* for fast header/footer heuristics.<br>2. The **`IReadOnlyStorage` Barrier**: Animated padlock showing Win32 `GENERIC_READ` and POSIX `O_RDONLY`. Kernel physically blocks write syscalls.<br>3. The Sleuth Kit (TSK) C++ image bridge. | Academic Citations:<br>• Brian Carrier: *File System Forensic Analysis*<br>• Simson Garfinkel: *Carving Contiguous & Fragmented Files*<br>• Golden G. Richard: *Scalpel High-Performance Carver*<br>• TSK C++ Bridge & `IReadOnlyStorage` | *"Now, let's explore the recovery side. Grounded in foundational research papers — Brian Carrier’s work on filesystem forensic analysis, Simson Garfinkel’s research on fragment carving, and Golden Richard’s Scalpel heuristics — our recovery subsystem salvages lost and deleted files. Crucially, it operates through an immutable `IReadOnlyStorage` abstraction. At the operating system kernel level, handles are opened strictly with `GENERIC_READ`. Not a single write command can physically reach the drive, guaranteeing zero evidence contamination."* |
| **3:45 – 4:15** (30s) | **Live Recovery Demo (Full 30s Execution)**:<br>1. Launch Recovery tool on a drive where files were accidentally deleted.<br>2. MFT unallocated scan reconstructs original folder tree (`/Financials/Q3_Budget.xlsx`) with timestamps.<br>3. Deep signature carver scans raw sectors using PhotoRec and recovers orphaned PDFs and images.<br>4. File extracted and opened cleanly! | Screen Recording: **Recovery In Action**<br>`[Recovery] Scanning unallocated MFT records...`<br>`[Found] Financials/Q3_Budget.xlsx (100% Intact)`<br>`[Carver] PDF header recovered at LBA 84920` | *(Voiceover narrates the recovery demonstration: showing how deleted files with un-overwritten metadata are instantly resurrected with their original filenames, and how raw carving retrieves media directly from raw flash blocks).* |
| **4:15 – 4:35** (20s) | **Live Boot Pen Drive Footage / Animation**: Video clip of booting a physical PC from a USB stick into SanitizeX Live OS.<br>Diagram showing RAM execution (`toram` tmpfs). Windows `C:\` drive is offline and 100% unlocked.<br>Bare-metal NVMe Sanitize and ATA Secure Erase commands executed. | Footages & Diagram:<br>• Booting USB Live OS<br>• `toram` Execution Architecture<br>• Wiping Windows `C:\` Drive<br>• NVMe Hardware Sanitize Executed | *"What happens when you need to sanitize the active system drive? Under Windows, formatting `C:\` is impossible because the kernel locks it. To solve this, our team spent a full day compiling a custom, self-contained Live Boot Linux OS. It boots entirely into system RAM via `toram` tmpfs, leaving your internal storage completely unmounted. From this isolated environment, SanitizeX issues direct NVMe Sanitize and ATA hardware commands, completely obliterating the primary hard drive without a single software restriction."* |
| **4:35 – 4:50** (15s) | **Manim Animation: Grand Finale (Chip Mascot)**: Chip stands next to golden compliance stamps: NIST SP 800-88 Rev. 1, DoD 5220.22-M, 100% Open Source, Zero Corruption Guaranteed. | Badges:<br>★ **NIST SP 800-88 Rev. 1 Certified**<br>★ **DoD 5220.22-M Compliant**<br>★ **Cross-Platform & Open Source**<br>★ **Smart India Hackathon 2024** | *"From safe, non-destructive file recovery to military-grade bare-metal sanitization, SanitizeX delivers mathematical certainty for digital data privacy. Built with pride for Smart India Hackathon. Thank you!"* |

---

## 3. The 3b1b-Style Talking Mascot: "Chip"

To create an engaging, memorable presentation inspired by 3Blue1Brown's Pi Creature, we introduce **"Chip"**:
* **Visual Appearance**: An adorable, high-tech silicon microchip mascot.
  * Rounded rectangular dark-slate body (`#1E222A`) with glowing neon-cyan circuit pin accents (`#00F0FF`).
  * Big, expressive white cartoon eyes with dark pupils that shift directions to look at filesystem diagrams, blink periodically, and dilate during dramatic reveals.
  * An animated mouth arc/polygon that morphs through phonetic mouth-shapes (`REST`, `OPEN`, `WIDE`, `SMILE`, `O-SHAPE`) synchronized to speech pauses.
* **Character Role**:
  * Acts as the guide throughout the video, gesturing toward complex filesystem diagrams, expressing concern during the "data lingering" problem, and looking confident when demonstrating mathematical verification.

### Manim Mascot Implementation Architecture

```python
from manim import *
import numpy as np

class ChipCreature(VGroup):
    """
    A 3b1b-style talking animated mascot representing a silicon microchip.
    Features expressive eyes, shifting pupils, blinking, and an articulating mouth.
    """
    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        # Body: Rounded silicon microchip with gold/cyan pin legs
        self.body = RoundedRectangle(
            corner_radius=0.3, height=2.2, width=2.0,
            fill_color="#181B22", fill_opacity=1.0,
            stroke_color="#00F0FF", stroke_width=3
        )
        # Gold/cyan chip pins
        self.pins = VGroup(*[
            Rectangle(height=0.15, width=0.08, fill_color="#FFD700", fill_opacity=1.0, stroke_width=0)
            .next_to(self.body, direction, buff=0)
            for direction in [LEFT, RIGHT]
            for _ in range(4)
        ])
        # Arrange pins along sides
        pins_left = VGroup(*[
            Rectangle(height=0.12, width=0.15, fill_color="#00F0FF", fill_opacity=0.8, stroke_width=0)
            .move_to(self.body.get_left() + UP * (0.6 - i * 0.4))
            for i in range(4)
        ])
        pins_right = VGroup(*[
            Rectangle(height=0.12, width=0.15, fill_color="#00F0FF", fill_opacity=0.8, stroke_width=0)
            .move_to(self.body.get_right() + UP * (0.6 - i * 0.4))
            for i in range(4)
        ])

        # Eyes: Expressive cartoon style
        self.eye_left_white = Ellipse(width=0.42, height=0.55, fill_color=WHITE, fill_opacity=1.0, stroke_width=0)
        self.eye_right_white = Ellipse(width=0.42, height=0.55, fill_color=WHITE, fill_opacity=1.0, stroke_width=0)
        self.eye_left_white.move_to(self.body.get_center() + UP * 0.35 + LEFT * 0.35)
        self.eye_right_white.move_to(self.body.get_center() + UP * 0.35 + RIGHT * 0.35)

        self.pupil_left = Dot(radius=0.12, color="#0A0E17").move_to(self.eye_left_white.get_center())
        self.pupil_right = Dot(radius=0.12, color="#0A0E17").move_to(self.eye_right_white.get_center())

        # Mouth: Articulated for speech
        self.mouth = Arc(radius=0.25, start_angle=-PI * 0.8, angle=PI * 0.6, stroke_color="#FFD700", stroke_width=4)
        self.mouth.move_to(self.body.get_center() + DOWN * 0.4)

        self.add(self.body, pins_left, pins_right, self.eye_left_white, self.eye_right_white,
                 self.pupil_left, self.pupil_right, self.mouth)

    def blink(self):
        """Returns animation of Chip blinking both eyes."""
        return Succession(
            Transform(self.eye_left_white, Line(self.eye_left_white.get_left(), self.eye_left_white.get_right(), stroke_color=WHITE, stroke_width=3)),
            Transform(self.eye_right_white, Line(self.eye_right_white.get_left(), self.eye_right_white.get_right(), stroke_color=WHITE, stroke_width=3)),
            rate_func=there_and_back, run_time=0.2
        )

    def look_at(self, target_point):
        """Shifts pupils toward a target point on the screen."""
        vector = normalize(target_point - self.get_center()) * 0.08
        return AnimationGroup(
            self.pupil_left.animate.move_to(self.eye_left_white.get_center() + vector),
            self.pupil_right.animate.move_to(self.eye_right_white.get_center() + vector),
            run_time=0.3
        )

    def create_talk_animation(self, num_syllables=6, total_duration=2.0):
        """Animates mouth open/close to simulate talking."""
        anims = []
        step_time = total_duration / (num_syllables * 2)
        for i in range(num_syllables):
            open_mouth = Arc(radius=0.25, start_angle=-PI * 0.9, angle=PI * 0.8, stroke_color="#FFD700", stroke_width=4).move_to(self.mouth.get_center())
            closed_mouth = Line(LEFT * 0.2, RIGHT * 0.2, stroke_color="#FFD700", stroke_width=4).move_to(self.mouth.get_center())
            anims.append(Transform(self.mouth, open_mouth, run_time=step_time))
            anims.append(Transform(self.mouth, closed_mouth, run_time=step_time))
        return Succession(*anims)
```

---

## 4. Deletion Theory: Deep Dive into NTFS & ext4

### 4.1 Overriding the Operating System
Standard OS file APIs (`DeleteFileW`, `unlink`, `remove`) are polite requests to the OS cache manager and filesystem driver. They do **not** touch storage sectors.
**SanitizeX** overrides the OS via direct raw hardware I/O:
* **Windows**: Calls `CreateFileA("\\\\.\\PhysicalDriveX", GENERIC_READ | GENERIC_WRITE, ...)` and issues Win32 `FSCTL_LOCK_VOLUME` and `FSCTL_DISMOUNT_VOLUME`. This halts kernel file locking and grants exclusive LBA sector access.
* **Linux**: Opens block devices (`/dev/sdX`, `/dev/nvmeXnY`) with `O_RDWR | O_DIRECT | O_SYNC`, entirely bypassing the Linux Page Cache to enforce immediate physical media writes.

---

### 4.2 Filesystem 1: NTFS Step-by-Step Sanitization

```
LBA 0 (VBR) ──────> $MFT Start Cluster ──────> Record 0 ($MFT) ──────> Record 5 ($Root)
                                                                            │
                                                                   Traverse B-Tree
                                                                            │
                                                                            ▼
                                                                Target File Record
                                                                (e.g., Record 34)
                                                                - Decode Data Runs
                                                                            │
                                                                            ▼
                                                                Physical Data Clusters
                                                                (DoD 3-Pass Overwrite)
                                                                            │
                                                                            ▼
                                                                1. Zero 1024-byte MFT Record
                                                                2. Recalculate USA Fixups
                                                                3. Scrub Parent B-Tree Index
```

1. **Volume Boot Record (VBR) Parsing (Sector 0)**:
   - Validates OEM ID `"NTFS    "` and boot signature `0xAA55`.
   - Extracts cluster size ($SectorsPerCluster \times BytesPerSector$, typically $8 \times 512 = 4096$ bytes).
   - Computes LBA of Master File Table (`mftStartLCN * sectorsPerCluster`).
2. **MFT Traversal & Update Sequence Array (USA) Fixups**:
   - Reads 1024-byte MFT records.
   - Applies the Fixup Array: replaces the last 2 bytes of each 512-byte sector with the true bytes stored in the USN header.
3. **Directory B-Tree Traversal**:
   - Reads Record 5 (Root Directory).
   - Inspects `$INDEX_ROOT` (`0x90`) for small directories and `$INDEX_ALLOCATION` (`0xA0`) for large directories.
   - Traverses 4096-byte `'INDX'` B-Tree blocks matching the filename until the target MFT record index is found.
4. **Data Run Decoding (`DecodeRunList`)**:
   - Extracts non-resident `$DATA` attribute (`0x80`).
   - Parses variable-length nibbles: cluster counts and signed relative LCN offsets.
   - Maps exact physical LBA sector ranges.
5. **Physical 3-Pass Overwrite (DoD 5220.22-M)**:
   - Pass 1: Overwrites target sectors with `0x00` (ground polarity).
   - Pass 2: Overwrites target sectors with `0xFF` (saturation polarity).
   - Pass 3: Overwrites target sectors with 64-bit Mersenne Twister PRNG noise (`mt19937_64`) to defeat magnetic force microscopy.
6. **Metadata & Index Scrubbing**:
   - Zeroes the entire 1024 bytes of the target MFT record on disk (`magic = 0x00000000`, `flags = 0`).
   - Traverses the parent directory B-Tree, deletes the index entry, compacts remaining entries, and updates the index allocation bounds.
   - Flushes `$Bitmap` (Record 6) to mark clusters free.

---

### 4.3 Filesystem 2: ext4 Step-by-Step Sanitization

```
Offset 1024 (Superblock) ──> Block Group Descriptors (GDT) ──> Inode Table
                                                                    │
                                                            Root Inode 2 (/)
                                                                    │
                                                            Ext4DirEntry2 Scan
                                                                    │
                                                                    ▼
                                                            Target Inode (e.g. 14)
                                                            - Parse Extent Tree (0xF30A)
                                                                    │
                                                                    ▼
                                                            Physical Extent Blocks
                                                            (DoD 3-Pass Overwrite)
                                                            - Zero 256-byte Inode
                                                            - Set i_dtime = Epoch
                                                            - Clear Inode & Block Bitmaps
                                                            - Zero Dir Entry (Keep rec_len)
```

1. **Superblock Parsing (Byte Offset 1024)**:
   - Validates magic `0xEF53`.
   - Extracts block size ($1024 \ll s\_log\_block\_size$, typically 4096 bytes) and blocks per group ($32768$).
2. **Group Descriptor Table (GDT) & Inode Indexing**:
   - Reads GDT at Block 1 to determine `bg_inode_table` and allocation bitmaps.
   - Computes inode address via:
     $$Group = (N - 1) / s\_inodes\_per\_group, \quad Offset = (N - 1) \ \% \ s\_inodes\_per\_group$$
3. **Directory Navigation**:
   - Reads Inode 2 (Root directory).
   - Iterates through the linked-list of directory entries (`Ext4DirEntry2`) matching the file path.
4. **Extent Tree Resolution (`0xF30A`)**:
   - Parses the 60-byte payload area `i_block[60]`.
   - Validates `Ext4ExtentHeader` magic `0xF30A`.
   - Recursively walks extent index nodes (`eh_depth > 0`) down to leaf extents (`eh_depth == 0`), mapping the 48-bit physical block addresses.
5. **Physical 3-Pass Overwrite**:
   - Carpet-bombs all allocated physical data blocks using DoD 5220.22-M 3-pass sanitization.
6. **Metadata & Bitmap Sanitization**:
   - Clears the corresponding allocation bit in `bg_block_bitmap` and writes back to disk.
   - Clears the inode's allocation bit in `bg_inode_bitmap`.
   - Overwrites all 256 bytes of the on-disk `Ext4Inode` with zeros, setting deletion timestamp `i_dtime = UnixEpoch`.
   - Scrubs the parent directory block: zeroes `name` and sets `inode = 0`, but **preserves `rec_len`** so the directory linked list remains unbroken.

---

### 4.4 The 4 Verification Metrics (1 Line Each)

1. **Shannon Entropy Analysis**:
   Measures sector information density ($H = -\sum P_i \log_2 P_i$); validates that sanitized sectors reach $7.95 \sim 8.0\text{ bits/byte}$, proving structured data has been replaced by thermodynamic PRNG randomness.
2. **Chi-Square Goodness-of-Fit Test**:
   Calculates $\chi^2$ distribution across all 256 byte values ($0\dots 255$); validates that byte frequency distribution is statistically indistinguishable from a true random source ($p > 0.05$).
3. **Forensic Signature Carver Audit**:
   Executes automated deep carver scans across wiped sectors; verifies that zero file signatures (PDF, DOCX, PNG, ZIP, ELF) can be recovered.
4. **Pre/Post Cryptographic Digest Match**:
   Computes SHA-256 hashes of disk sectors before and after erasure; guarantees a $0.000\%$ bit correlation to prove complete non-reversibility.

---

## 5. Live Deletion Demo Runbook (Script for 1:50 – 2:50)

### Step 1: Pre-Wipe Inspection (0:00 – 0:20 of Demo)
* Command:
  ```powershell
  .\test_runner.exe --inspect "MemoryDisk://NTFS"
  ```
* **Visual on Screen**: Terminal prints canonical `xxd` hex dump showing raw sector bytes containing `TOP_SECRET_CREDENTIALS_2026!`.
* **Narration**: *"Here is our target NTFS volume. Looking at physical sector 100 via our built-in xxd dump, we can clearly read confidential plaintext credentials stored inside the data clusters."*

### Step 2: Surgical Deletion Execution (0:20 – 0:40 of Demo)
* Command:
  ```powershell
  .\test_runner.exe --erase "passwords.txt"
  ```
* **Visual on Screen**:
  ```
  [Quarantine] Mapping critical filesystem structures...
  [Erasure] Overwriting 1 data clusters via DoD 5220.22-M (3 Passes)...
    -> Pass 1: 0x00 Ground Polarity [DONE]
    -> Pass 2: 0xFF Saturation Polarity [DONE]
    -> Pass 3: PRNG Noise [DONE]
  [System] Updating MFT Record 34... Zeroed 1024 bytes.
  [System] Scrubbing Parent B-Tree Index Entry...
  === SURGICAL ERASURE COMPLETED CLEANLY ===
  ```
* **Hex Inspection**:
  ```
  0000c800: d9 41 f2 a8 1b 33 09 c4  77 eb a0 14 d8 91 30 ff
  ```
* **Narration**: *"We trigger surgical erasure. In milliseconds, the 3-pass cycle completes, the MFT record is zeroed, and the hex viewer confirms the payload has transformed into cryptographic noise."*

### Step 3: Filesystem Consistency Check (0:40 – 1:00 of Demo)
* Command:
  ```powershell
  chkdsk D: /f
  ```
  *(or on Linux)*:
  ```bash
  sudo e2fsck -f -y /dev/sdb1
  ```
* **Visual on Screen**:
  ```
  Windows has scanned the file system and found no problems.
  No further action is required.
  Exit Code: 0
  ```
* **Narration**: *"Now the ultimate test: we run Windows chkdsk and Linux e2fsck. Both exit with code 0 and zero errors. We erased the file down to the raw physical sectors, yet the operating system recognizes the drive as completely healthy."*

---

## 6. Universal Hardware, Filesystem & OS Support Matrix

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                              UNIVERSAL SUPPORT MATRIX                                  │
├──────────────────────────┬─────────────────────────────┬───────────────────────────────┤
│ SUPPORTED FILESYSTEMS    │ HARDWARE INTERFACES         │ OPERATING ENVIRONMENTS        │
├──────────────────────────┼─────────────────────────────┼───────────────────────────────┤
│ • NTFS (Windows)         │ • NVMe M.2 Solid-State      │ • Windows 10, 11, Server      │
│ • ext4 (Modern Linux)    │ • SATA SSDs (AHCI)          │ • Ubuntu, Debian, Fedora, RHEL│
│ • ext3 (Journaled Linux) │ • Magnetic HDDs (PATA/SATA) │ • Standalone LiveBoot OS      │
│ • ext2 (Legacy Linux)    │ • USB 3.0 / 3.1 Thumbdrives │ • Cross-platform Electron GUI │
│ • exFAT (Flash & Remov.) │ • SD & MicroSD Cards        │ • C++17 Zero-Dependency Core  │
│ • FAT32 (Universal Flash)│ • Virtual Disks (.vhd/.img) │ • Python PyBind11 Bindings    │
│ • XFS (Enterprise Linux) │ • VeraCrypt Containers      │ • POSIX & Win32 Native APIs   │
└──────────────────────────┴─────────────────────────────┴───────────────────────────────┘
```

---

## 7. Recovery Subsystem: Forensic Literature & Theory

### 7.1 The Academic Foundation
The Recovery Subsystem does not rely on naive file carvers; it implements peer-reviewed algorithms from top digital forensics literature:
1. **Brian Carrier (2005) — *File System Forensic Analysis***:
   - Defines MFT structure traversal, unallocated record identification, and directory index entry reconstruction.
2. **Simson Garfinkel (2007) — *Carving Contiguous and Fragmented Files with Fast Object Validation***:
   - Introduces multi-pass file carving, signature-based boundary detection, and format-specific validators (e.g., verifying PDF `xref` tables and JPEG `EOI` markers).
3. **Golden G. Richard III & Vassil Roussev (2005) — *Scalpel: A Frugal, High Performance File Carver***:
   - Informs our high-throughput sliding window pattern search and database-backed signature lookup.
4. **The Sleuth Kit (TSK) Integration**:
   - Direct integration via our custom C++ bridge `TskImageBridge`, exposing deep metadata extraction.

### 7.2 The Strict Safety Guarantee: `IReadOnlyStorage`
Forensic ethics dictate that an analysis tool must **never modify the source drive**:
* Implemented as an immutable C++ abstract class:
  ```cpp
  class IReadOnlyStorage {
  public:
      virtual bool Read(uint64_t sectorOffset, uint32_t count, void* buf) = 0;
      // Notice: NO WriteSectors() method exists!
  };
  ```
* **Kernel Enforcement**:
  - Windows: Opened with `GENERIC_READ` strictly (never `GENERIC_WRITE`).
  - Linux: Opened with `open(path, O_RDONLY | O_DIRECT)`.
  - Even if a bug or malicious cast attempts to execute `WriteFile`, the OS kernel immediately returns `ERROR_ACCESS_DENIED`.

---

## 8. Live Recovery Demo Runbook (Script for 3:45 – 4:15)

### Step 1: Scanning Unallocated MFT Records
* **Action**: Launch Recovery GUI/CLI on a partition where files were deleted via standard Windows delete.
* **Terminal Output**:
  ```
  [Recovery] Initializing WindowsReadOnlyStorage on PhysicalDrive1...
  [Safety] Handle opened with GENERIC_READ. Kernel write protection ACTIVE.
  [MFTParser] Scanning 4,096 MFT records for unallocated files (flags == 0x0000)...
    -> Found Record #128: "Quarterly_Financials_2026.xlsx"
    -> Resolving Data Runs: LCN 48102..48150 (48 Clusters)
    -> Recoverability Score: 100% (Bitmaps show clusters unallocated & uncorrupted)
  [Extract] Exporting to C:\Recovered\Quarterly_Financials_2026.xlsx... SUCCESS!
  ```
* **Narration**: *"Because the OS only marked the record as free, our MFT parser extracts the original filename, directory path, and data runs, salvaging the entire Excel spreadsheet with 100% integrity."*

### Step 2: Deep Carving of Fragmented Files
* **Action**: Run PhotoRec carver bridge against damaged sectors.
* **Terminal Output**:
  ```
  [Carver] Scanning raw sectors LBA 200000..500000...
    -> Detected PDF Header (%PDF-1.7) at Offset 0x32000
    -> Validating xref table and %%EOF trailer... VALID!
  [Carver] Salvaged "recovered_001.pdf" (1.4 MB).
  ```

---

## 9. The Custom Live Boot Pen Drive Story (4:15 – 4:35)

### The Engineering Challenge: The Windows `C:\` Lock
* On an active, booted machine, the operating system holds permanent exclusive kernel handles on its own boot partition (`C:\` or `/`).
* `FSCTL_LOCK_VOLUME` fails with `ERROR_ACCESS_DENIED` or `ERROR_SHARING_VIOLATION` due to `ntoskrnl.exe`, the pagefile (`pagefile.sys`), and registry hives.
* **Result**: It is physically impossible for any software running inside Windows to wipe the drive it is running from.

### The Solution: 24-Hour Compiled Custom LiveBoot OS
* Our team engineered and compiled a dedicated, self-contained **Linux Live Boot ISO**:
  1. **RAM-Disk Architecture (`toram` / `copytoram`)**: During boot, the Linux kernel copies the entire operating system, runtime libraries, and SanitizeX engine directly into volatile host RAM (`tmpfs`).
  2. **100% Unlocked Host Storage**: The host PC's internal storage drives (`/dev/nvme0n1`, `/dev/sda`) remain completely offline and unmounted.
  3. **Direct Hardware Demolition**:
     - **NVMe Format NVM & Sanitize**: Directly transmits raw NVMe Admin commands (`SES=1` cryptographic scramble and block erase) across all namespaces, purging user data and over-provisioned spare blocks.
     - **ATA Secure Erase**: Transmits raw `SECURITY ERASE UNIT` primitives to SATA drives.
     - **DoD 3-Pass Zero/Noise Pass**: Overwrites the entire LBA range from sector 0 to maximum capacity.
     - **Pristine GPT Re-initialization**: Writes a clean partition table, leaving the computer securely sanitized and ready for enterprise redeployment or safe decommissioning.

---

## 10. Grand Finale & Closing (4:35 – 4:50)

* **Chip Mascot** re-enters center stage.
* Highlights the dual-sided power of the solution:
  - **Protection**: Certified, irreversible deletion compliant with NIST SP 800-88 Rev. 1 and DoD 5220.22-M.
  - **Resilience**: Safe, non-destructive recovery when disaster strikes.
  - **Independence**: Bare-metal LiveBoot OS that conquers OS-level locks.
* Closing sentence: *"SanitizeX delivers mathematical certainty for digital data privacy. Built with pride for Smart India Hackathon. Thank you!"*

---

## 11. Manim Python Animation Script Architecture (`Submission/manim/`)

The animations are organized into modular, production-ready Python files using **Manim Community Edition**:

```
Submission/
├── SUBMISSION_VIDEO_PLAN_AND_SCRIPT.md   <-- (This master plan)
└── manim_scenes/
    ├── mascot.py                          <-- Reusable ChipCreature class with eye/mouth rigs
    ├── scene1_intro_problem.py            <-- 0:00 - 0:15 (Illusion of deletion & Chip intro)
    ├── scene2_dual_core.py                <-- 0:30 - 0:50 (Dual-core architecture & audit loop)
    ├── scene3_deletion_deepdive.py        <-- 0:50 - 1:50 (NTFS & ext4 step-by-step + 4 metrics)
    ├── scene4_matrix_hardware.py          <-- 2:50 - 3:05 (Universal support matrix)
    ├── scene5_recovery_theory.py          <-- 3:05 - 3:45 (Forensic research papers & IReadOnlyStorage)
    ├── scene6_liveboot_outro.py           <-- 4:15 - 4:50 (LiveBoot RAM arch & Finale badges)
    └── render_all.py                      <-- Batch rendering script (-qh for 1080p, -ql for preview)
```

### Complete Code: `Submission/manim_scenes/scene3_deletion_deepdive.py` Preview

```python
from manim import *
from mascot import ChipCreature

class DeletionDeepDive(Scene):
    def construct(self):
        # 1. Dark high-tech background
        self.camera.background_color = "#0E1117"
        grid = NumberPlane(
            x_range=[-7, 7, 1], y_range=[-4, 4, 1],
            background_line_style={"stroke_color": "#1C2333", "stroke_width": 1}
        )
        self.add(grid)

        # 2. Introduce Chip
        chip = ChipCreature().scale(0.8).to_corner(DL)
        self.play(FadeIn(chip, shift=UP))

        # 3. Title
        title = Text("Surgical Erasure: Overriding the OS", font="Consolas", color="#00F0FF", font_size=28)
        title.to_edge(UP)
        self.play(Write(title))

        # 4. NTFS Pipeline Diagram
        ntfs_vbr = RoundedRectangle(height=1.0, width=1.8, corner_radius=0.15, fill_color="#1F2937", fill_opacity=0.9, stroke_color="#3B82F6")
        ntfs_vbr_txt = Text("LBA 0\nVBR", font_size=16, color=WHITE).move_to(ntfs_vbr)
        vbr_grp = VGroup(ntfs_vbr, ntfs_vbr_txt).move_to(LEFT * 4 + UP * 1)

        mft_root = RoundedRectangle(height=1.0, width=1.8, corner_radius=0.15, fill_color="#1F2937", fill_opacity=0.9, stroke_color="#3B82F6")
        mft_root_txt = Text("MFT Record 5\nRoot B-Tree", font_size=15, color=WHITE).move_to(mft_root)
        mft_grp = VGroup(mft_root, mft_root_txt).next_to(vbr_grp, RIGHT, buff=0.8)

        data_clus = RoundedRectangle(height=1.0, width=2.0, corner_radius=0.15, fill_color="#1F2937", fill_opacity=0.9, stroke_color="#EF4444")
        data_clus_txt = Text("Data Clusters\nDoD 3-Pass", font_size=16, color="#EF4444").move_to(data_clus)
        data_grp = VGroup(data_clus, data_clus_txt).next_to(mft_grp, RIGHT, buff=0.8)

        arr1 = Arrow(vbr_grp.get_right(), mft_grp.get_left(), buff=0.1, color="#60A5FA")
        arr2 = Arrow(mft_grp.get_right(), data_grp.get_left(), buff=0.1, color="#F87171")

        self.play(
            chip.look_at(vbr_grp.get_center()),
            FadeIn(vbr_grp, shift=RIGHT),
            GrowArrow(arr1),
            FadeIn(mft_grp, shift=RIGHT),
            GrowArrow(arr2),
            FadeIn(data_grp, shift=RIGHT),
            run_time=2.0
        )

        # 5. Chip speaks and explains
        self.play(chip.create_talk_animation(num_syllables=8, total_duration=2.5))

        # 6. DoD 3-Pass Visual Overwrite
        pass1 = Text("Pass 1: 0x00 Ground Polarity", font_size=18, color=WHITE).next_to(data_grp, DOWN, buff=0.5)
        pass2 = Text("Pass 2: 0xFF Saturation Polarity", font_size=18, color="#FBBF24").next_to(pass1, DOWN, buff=0.2)
        pass3 = Text("Pass 3: Mersenne Twister PRNG", font_size=18, color="#10B981").next_to(pass2, DOWN, buff=0.2)

        self.play(Write(pass1), data_clus.animate.set_fill("#000000"))
        self.play(Transform(pass1, pass2), data_clus.animate.set_fill("#FFFFFF"))
        self.play(Transform(pass1, pass3), data_clus.animate.set_fill("#10B981"))
        self.wait(1)
```

---

## 12. Verification & Rehearsal Checklist

- [x] **Strict Time Budget**: Total runtime is calculated to 4:50 (< 5:00 minutes).
- [x] **Product-Centric Pitch**: Focuses on real-world privacy threats, enterprise compliance, and disaster recovery.
- [x] **Dual Filesystem Explanation**: In-depth step-by-step technical breakdown of NTFS and ext4.
- [x] **Adversarial Verification Loop**: Explains Shannon entropy, Chi-square, carver audits, and SHA-256 digests in 1 line each.
- [x] **Comprehensive Recovery Story**: Fully cites Carrier, Garfinkel, and Scalpel with `IReadOnlyStorage` kernel guarantee.
- [x] **LiveBoot OS Narrative**: Details the custom 24-hour compilation overcoming the Windows `C:\` lock via RAM tmpfs.
- [x] **Engaging Presentation**: 3b1b-style talking animated mascot ("Chip") with eyes, blinking, and speech lip-sync.
