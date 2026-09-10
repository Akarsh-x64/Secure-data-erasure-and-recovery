# Forensic Verification & Audit Engine

## 1. Overview & Compliance Philosophy

Merely issuing overwrite commands without mathematically proving data destruction fails enterprise and legal compliance standards (NIST SP 800-88 Rev. 1, DoD 5220.22-M, GDPR Article 17 "Right to be Forgotten", HIPAA Security Rule §164.310).

The **Forensic Verification Engine** (`Erasure/Verification/`) acts as an autonomous auditor within SanitizeX. It subjects target sectors to rigorous statistical entropy testing, zero-residual cryptographic checks, and an adversarial 120+ file signature carver.

---

## 2. Statistical Analysis Suite (`StatisticalTests`)

* **Source Files**: [Erasure/Verification/StatisticalTests.h](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/Verification/StatisticalTests.h), [StatisticalTests.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/Verification/StatisticalTests.cpp)

### 2.1 Shannon Entropy ($H$)
Evaluates the information density and randomness of target sectors across all 256 byte bins ($0 \dots 255$):
$$H(X) = -\sum_{i=0}^{255} P(x_i) \log_2 P(x_i)$$
* **Zero-Filled Sectors (`0x00`)**: $H = 0.0000$ bits/byte (all probabilities concentrate in bin 0).
* **Cleartext Files (Code/Text/ASCII)**: $H \approx 3.5 \dots 5.0$ bits/byte.
* **Compressed / Encrypted Data**: $H \approx 7.2 \dots 7.8$ bits/byte.
* **SanitizeX DoD Pass 3 (PRNG Noise)**: $H \ge 7.9990$ bits/byte (pure cryptographic randomness).

### 2.2 Chi-Square ($\chi^2$) Goodness-of-Fit
Tests whether byte frequencies conform to a perfectly uniform distribution:
$$\chi^2 = \sum_{i=0}^{255} \frac{(O_i - E)^2}{E} \quad \text{where } E = \frac{N}{256}$$
* Degrees of freedom: $df = 255$.
* Uses the Wilson-Hilferty $Z$-transformation to compute the two-tailed $p$-value:
  $$Z = \frac{\left(\frac{\chi^2}{df}\right)^{1/3} - \left(1 - \frac{2}{9 \times df}\right)}{\sqrt{\frac{2}{9 \times df}}}$$
* A uniform PRNG overwrite produces $p \in [0.05, 0.95]$. A low $p$-value indicates non-random patterns or lingering un-wiped sectors.

### 2.3 Serial Correlation Coefficient ($r$)
Measures correlation between adjacent bytes ($x_i$ and $x_{i+1}$) to identify periodic generators or repetitive filler patterns:
$$r = \frac{\sum_{i=0}^{N-2} (x_i - \bar{x})(x_{i+1} - \bar{x})}{\sum_{i=0}^{N-1} (x_i - \bar{x})^2}$$
* True cryptographic noise produces $r \approx 0.000 \pm 0.005$.

### 2.4 Monte Carlo $\pi$ Approximation
Pairs sequential bytes into normalized 2D coordinate points $(u_k, v_k) \in [0, 1)^2$ and counts quadrant circle hits ($u_k^2 + v_k^2 \le 1$):
$$\pi_{\text{approx}} = 4 \times \frac{\text{hits}}{\text{total\_pairs}}$$
* Asserts approximation error $< 0.1\%$ relative to $\pi \approx 3.14159265$.

### 2.5 Zero-Dependency Cryptographic Hashing (SHA-256)
* An independent, self-contained FIPS 180-4 compliant SHA-256 implementation.
* Computes baseline digests (`preWipeSha256`) before sanitization and post-wipe digests (`postWipeSha256`).
* Verifies a 0.000% residual bit match.

---

## 3. Adversarial Signature Carver (`SignatureCarver`)

* **Source Files**: [Erasure/Verification/SignatureCarver.h](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/Verification/SignatureCarver.h), [SignatureCarver.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/Verification/SignatureCarver.cpp)

Even if entropy appears high, a partial overwrite could leave file headers intact (e.g. an un-wiped PDF header at LBA 0). The **Signature Carver** sweeps the target sectors against an internal database of **120+ magic signatures** across 6 categories:

| Category | File Formats & Magic Signatures Checked |
|---|---|
| **Documents** | PDF (`%PDF-`), MS Office OLE2 (`\xD0\xCF\x11\xE0`), OOXML/ZIP (`PK\x03\x04`), RTF (`{\rtf1`), PostScript, ODT, EPUB |
| **Images** | JPEG (`\xFF\xD8\xFF`), PNG (`\x89PNG`), GIF (`GIF87a`/`89a`), BMP (`BM`), TIFF, WebP, PSD, ICO, RAW (CR2/NEF), HEIC |
| **Archives** | 7-Zip (`7z\xBC\xAF`), RAR (`Rar!\x1A`), GZ, BZ2, XZ, TAR, Zstandard, LZ4, CAB, DEB, RPM |
| **Audio/Video**| MP3 (ID3v2), FLAC (`fLaC`), WAV (`RIFF....WAVE`), OGG (`OggS`), MP4/MOV (`ftyp`), AVI, MKV/WebM (`\x1A\x45\xDF\xA3`), FLV |
| **Binaries** | Windows PE (`MZ`), Linux ELF (`\x7FELF`), Mach-O, Java Class (`\xCA\xFE\xBA\xBE`), WebAssembly (`\x00asm`), DEX |
| **Databases** | SQLite 3 (`SQLite format 3`), PCAP, PCAPNG, VMDK (`KDMV`), VHD (`conect`), Windows Registry (`regf`), BitLocker (`-FVE-FS-`) |

### Verification Rule: Zero Tolerance
* The carver sweeps every sector boundary.
* A sanitization audit **FAILS** if even a single file signature is detected ($N_{\text{signatures}} > 0$).

---

## 4. Verification Engine Orchestrator (`VerificationEngine`)

* **Source Files**: [Erasure/Verification/VerificationEngine.h](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/Verification/VerificationEngine.h), [VerificationEngine.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/Verification/VerificationEngine.cpp)

### 4.1 Lifecycle of an Audited Erasure
```
1. Target Selected: "confidential.pdf" (LBA 2048 .. 2079)
   │
   ▼
2. CapturePreWipeDigest()
   • Reads sectors via IHardwareController
   • Computes pre-wipe SHA-256 (e.g. 9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08)
   │
   ▼
3. Surgical Erasure Executed (DoD 3-Pass Overwrite + Metadata Wipe)
   │
   ▼
4. AuditFileErasure()
   • Re-reads physical sectors from disk
   • Computes post-wipe SHA-256
   • Inspects cluster slack space (remaining bytes between file end and cluster boundary)
   • Evaluates Shannon entropy, Chi-Square, and Monte Carlo Pi
   • Executes adversarial SignatureCarver sweep
   │
   ▼
5. Audit Passed -> Generate VerificationReport (ASCII Certificate + JSON-RPC event)
```

### 4.2 NIST SP 800-88 Rev. 1 Stratified Sampling (`AuditVolumeWipe`)
When auditing whole-volume or whole-drive wipes (where reading every sector of a 4 TB drive would take 10+ hours), the engine executes **Stratified Sampling**:
1. Divides total disk capacity into $K$ equidistant zones ($K = 128 \dots 1024$).
2. Within each zone, selects pseudorandom cluster offsets.
3. Reads sectors and validates that bytes match the expected wipe pattern (`0x00` or PRNG noise).
4. Calculates the binomial confidence interval:
   $$C = 1 - (1 - p)^N$$
   For $N = 1024$ samples and defective threshold $p = 0.003$, confidence $C > 99.999\%$.

---

## 5. Visual Audit Certificates & Telemetry

* **Source Files**: [Erasure/Verification/VerificationReport.h](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/Verification/VerificationReport.h), [VerificationReport.cpp](file:///c:/Users/Sudhit/Documents/Study%20Material/Projects/SIH%20v2/Erasure/Verification/VerificationReport.cpp)

### Terminal ASCII Certificate
`PrintTerminalReport()` renders a visual certificate with block histogram graphs:
```text
================================================================================
                    FORENSIC SANITIZATION AUDIT CERTIFICATE                     
================================================================================
Target Path:           C:\evidence\audit.log
Target Scope:          FILE_ERASURE
Physical Sectors:      2048 - 4095 (2048 sectors / 1,048,576 bytes)
Timestamp:             2026-09-10 19:25:00 UTC
Standard:              DoD 5220.22-M (3-Pass)

--- CRYPTOGRAPHIC INTEGRITY ---
Pre-Wipe SHA-256:      e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
Post-Wipe SHA-256:     6b86b273ff34fce19d6b804eff5a3f5747ada4eaa22f1d49c01e52ddb7875b4b
Residual Match:        0.000% (PASSED)

--- STATISTICAL ENTROPY ANALYSIS ---
Shannon Entropy:       [====================] 7.9994 / 8.0000 bits/byte
Chi-Square (df=255):   253.12 (p-value: 0.524, Uniform)
Serial Correlation:    -0.0004 (Zero Periodicity)
Monte Carlo Pi:        3.1418 (Error: 0.006%)

--- ADVERSARIAL CARVER AUDIT ---
Signatures Scanned:    120+ File Magic Signatures
Signatures Detected:   0 (PASSED)
Slack Space Sanitized: YES

VERDICT:               CERTIFIED DESTROYED (NIST SP 800-88 CONFIDENCE > 99.99%)
================================================================================
```

### JSON-RPC Telemetry Serialization
`ToJson()` formats the complete audit record as a JSON payload delivered directly to the Electron frontend via IPC or WebSocket.
