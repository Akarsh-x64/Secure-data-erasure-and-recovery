#include "FilesystemDetector.h"

#include <cstring>

namespace Recovery {
namespace Filesystems {

// ---- Little-endian / big-endian helpers ----

static uint16_t ReadLE16(const uint8_t* d) {
    return static_cast<uint16_t>(d[0]) | (static_cast<uint16_t>(d[1]) << 8);
}

static uint32_t ReadLE32(const uint8_t* d) {
    return static_cast<uint32_t>(d[0])
         | (static_cast<uint32_t>(d[1]) << 8)
         | (static_cast<uint32_t>(d[2]) << 16)
         | (static_cast<uint32_t>(d[3]) << 24);
}

static uint16_t ReadBE16(const uint8_t* d) {
    return (static_cast<uint16_t>(d[0]) << 8) | static_cast<uint16_t>(d[1]);
}

static uint32_t ReadBE32(const uint8_t* d) {
    return (static_cast<uint32_t>(d[0]) << 24)
         | (static_cast<uint32_t>(d[1]) << 16)
         | (static_cast<uint32_t>(d[2]) << 8)
         | static_cast<uint32_t>(d[3]);
}

bool FilesystemDetector::IsPowerOfTwo(uint32_t v) {
    return v != 0 && (v & (v - 1)) == 0;
}

// ---- Public API ----

Core::FileSystemType FilesystemDetector::Detect(
    Core::ByteReader& reader,
    const Core::StorageRegion& partition)
{
    uint64_t base = partition.startOffset;
    uint64_t size = partition.size;

    if (size == 0) return Core::FileSystemType::Unknown;

    // Try each filesystem in order of signature specificity.
    // NTFS/exFAT first (unique 8-byte OEM IDs), then others.
    Core::FileSystemType result;

    result = TryNTFS(reader, base, size);
    if (result != Core::FileSystemType::Unknown) return result;

    result = TryExFAT(reader, base, size);
    if (result != Core::FileSystemType::Unknown) return result;

    result = TryXFS(reader, base, size);
    if (result != Core::FileSystemType::Unknown) return result;

    result = TryAPFS(reader, base, size);
    if (result != Core::FileSystemType::Unknown) return result;

    result = TryExt(reader, base, size);
    if (result != Core::FileSystemType::Unknown) return result;

    result = TryHFSPlus(reader, base, size);
    if (result != Core::FileSystemType::Unknown) return result;

    // FAT32 last — relies on structural checks, higher false-positive risk
    result = TryFAT32(reader, base, size);
    if (result != Core::FileSystemType::Unknown) return result;

    return Core::FileSystemType::Unknown;
}

std::string FilesystemDetector::GetFileSystemName(Core::FileSystemType type) {
    switch (type) {
        case Core::FileSystemType::Unknown: return "Unknown";
        case Core::FileSystemType::NTFS:    return "NTFS";
        case Core::FileSystemType::ExFAT:   return "exFAT";
        case Core::FileSystemType::FAT32:   return "FAT32";
        case Core::FileSystemType::EXT2:    return "ext2";
        case Core::FileSystemType::EXT3:    return "ext3";
        case Core::FileSystemType::EXT4:    return "ext4";
        case Core::FileSystemType::XFS:     return "XFS";
        case Core::FileSystemType::HFSPlus: return "HFS+";
        case Core::FileSystemType::APFS:    return "APFS";
        default:                            return "Unknown";
    }
}

// ---- NTFS ----
// VBR OEM ID at offset 0x03 = "NTFS    " (8 bytes)
// BPB: bytes/sector at 0x0B (uint16 LE), sectors/cluster at 0x0D

Core::FileSystemType FilesystemDetector::TryNTFS(
    Core::ByteReader& reader, uint64_t base, uint64_t size)
{
    if (size < 512) return Core::FileSystemType::Unknown;

    // Read bytes 0x03..0x0D (11 bytes: 8 OEM + 2 BPS + 1 SPC)
    uint8_t buf[11];
    if (!reader.ReadBytes(base + 0x03, 11, buf))
        return Core::FileSystemType::Unknown;

    if (std::memcmp(buf, "NTFS    ", 8) != 0)
        return Core::FileSystemType::Unknown;

    uint16_t bytesPerSector = ReadLE16(buf + 8);
    uint8_t  sectorsPerCluster = buf[10];

    if (!IsPowerOfTwo(bytesPerSector) || bytesPerSector < 256 || bytesPerSector > 4096)
        return Core::FileSystemType::Unknown;

    if (sectorsPerCluster == 0)
        return Core::FileSystemType::Unknown;

    return Core::FileSystemType::NTFS;
}

// ---- exFAT ----
// VBR OEM ID at offset 0x03 = "EXFAT   " (8 bytes)
// BytesPerSectorShift at 0x6C (1 byte), valid range 9..12

Core::FileSystemType FilesystemDetector::TryExFAT(
    Core::ByteReader& reader, uint64_t base, uint64_t size)
{
    if (size < 512) return Core::FileSystemType::Unknown;

    uint8_t oemId[8];
    if (!reader.ReadBytes(base + 0x03, 8, oemId))
        return Core::FileSystemType::Unknown;

    if (std::memcmp(oemId, "EXFAT   ", 8) != 0)
        return Core::FileSystemType::Unknown;

    uint8_t shift = 0;
    if (!reader.ReadBytes(base + 0x6C, 1, &shift))
        return Core::FileSystemType::Unknown;

    if (shift < 9 || shift > 12)
        return Core::FileSystemType::Unknown;

    return Core::FileSystemType::ExFAT;
}

// ---- FAT32 ----
// Structural BPB validation — does NOT rely on "FAT" string alone.
//
// Required checks:
//   0x00: Jump instruction (0xEB or 0xE9)
//   0x0B: BytsPerSec (uint16 LE) — 512/1024/2048/4096
//   0x0D: SecPerClus (uint8) — power of 2, 1..128
//   0x0E: RsvdSecCnt (uint16 LE) — > 0
//   0x10: NumFATs (uint8) — 1 or 2
//   0x11: RootEntCnt (uint16 LE) — MUST be 0 for FAT32
//   0x13: TotSec16 (uint16 LE) — MUST be 0 for FAT32
//   0x15: Media (uint8) — 0xF0 or 0xF8..0xFF
//   0x16: FATSz16 (uint16 LE) — MUST be 0 for FAT32
//   0x24: FATSz32 (uint32 LE) — MUST be > 0 for FAT32
//   0x52: FilSysType (8 bytes) — "FAT32   " as confirmation

Core::FileSystemType FilesystemDetector::TryFAT32(
    Core::ByteReader& reader, uint64_t base, uint64_t size)
{
    // Need at least 90 bytes (0x52 + 8) for the BS_FilSysType field
    if (size < 90) return Core::FileSystemType::Unknown;

    uint8_t vbr[90];
    if (!reader.ReadBytes(base, 90, vbr))
        return Core::FileSystemType::Unknown;

    // 1. Jump instruction
    if (vbr[0] != 0xEB && vbr[0] != 0xE9)
        return Core::FileSystemType::Unknown;

    // 2. Bytes per sector
    uint16_t bytesPerSector = ReadLE16(vbr + 0x0B);
    if (!IsPowerOfTwo(bytesPerSector) || bytesPerSector < 512 || bytesPerSector > 4096)
        return Core::FileSystemType::Unknown;

    // 3. Sectors per cluster — power of 2, 1..128
    uint8_t secPerClus = vbr[0x0D];
    if (secPerClus == 0 || !IsPowerOfTwo(secPerClus))
        return Core::FileSystemType::Unknown;

    // 4. Reserved sector count > 0
    uint16_t rsvdSecCnt = ReadLE16(vbr + 0x0E);
    if (rsvdSecCnt == 0)
        return Core::FileSystemType::Unknown;

    // 5. Number of FATs — 1 or 2
    uint8_t numFATs = vbr[0x10];
    if (numFATs < 1 || numFATs > 2)
        return Core::FileSystemType::Unknown;

    // 6. Root entry count MUST be 0 for FAT32
    uint16_t rootEntCnt = ReadLE16(vbr + 0x11);
    if (rootEntCnt != 0)
        return Core::FileSystemType::Unknown;

    // 7. Total sectors 16 MUST be 0 for FAT32
    uint16_t totSec16 = ReadLE16(vbr + 0x13);
    if (totSec16 != 0)
        return Core::FileSystemType::Unknown;

    // 8. Media type — 0xF0 or 0xF8..0xFF
    uint8_t media = vbr[0x15];
    if (media != 0xF0 && media < 0xF8)
        return Core::FileSystemType::Unknown;

    // 9. FAT size 16 MUST be 0 for FAT32
    uint16_t fatSz16 = ReadLE16(vbr + 0x16);
    if (fatSz16 != 0)
        return Core::FileSystemType::Unknown;

    // 10. FAT size 32 MUST be > 0 for FAT32
    uint32_t fatSz32 = ReadLE32(vbr + 0x24);
    if (fatSz32 == 0)
        return Core::FileSystemType::Unknown;

    // 11. FilSysType confirmation (optional but adds confidence)
    if (std::memcmp(vbr + 0x52, "FAT32   ", 8) != 0)
        return Core::FileSystemType::Unknown;

    return Core::FileSystemType::FAT32;
}

// ---- ext2/3/4 ----
// Superblock at partition offset 1024 bytes.
// Magic number at superblock+0x38 (2 bytes LE) = 0xEF53
// Feature flags distinguish ext2 vs ext3 vs ext4:
//   s_feature_compat  at superblock+0x5C (uint32 LE)
//   s_feature_incompat at superblock+0x60 (uint32 LE)
//   s_rev_level at superblock+0x4C (uint32 LE)
//
// ext4 indicators (incompat flags):
//   EXTENTS  = 0x0040
//   64BIT    = 0x0080
//   FLEX_BG  = 0x0200
//
// ext3 indicator (compat flags):
//   HAS_JOURNAL = 0x0004
//
// Revision 0 (original) has no feature fields → EXT2.
// Returns Unknown when feature flags cannot be read (ambiguous).

static constexpr uint32_t EXT_MAGIC               = 0xEF53;
static constexpr uint64_t EXT_SUPERBLOCK_OFFSET    = 1024;
static constexpr uint32_t EXT_MAGIC_FIELD_OFFSET   = 0x38; // within superblock
static constexpr uint32_t EXT_REV_LEVEL_OFFSET     = 0x4C;
static constexpr uint32_t EXT_COMPAT_OFFSET        = 0x5C;
static constexpr uint32_t EXT_INCOMPAT_OFFSET      = 0x60;

static constexpr uint32_t EXT4_INCOMPAT_EXTENTS    = 0x0040;
static constexpr uint32_t EXT4_INCOMPAT_64BIT      = 0x0080;
static constexpr uint32_t EXT4_INCOMPAT_FLEX_BG    = 0x0200;
static constexpr uint32_t EXT4_INCOMPAT_MASK       = EXT4_INCOMPAT_EXTENTS
                                                   | EXT4_INCOMPAT_64BIT
                                                   | EXT4_INCOMPAT_FLEX_BG;
static constexpr uint32_t EXT3_COMPAT_HAS_JOURNAL  = 0x0004;

Core::FileSystemType FilesystemDetector::TryExt(
    Core::ByteReader& reader, uint64_t base, uint64_t size)
{
    // Need at least superblock offset + enough bytes for feature fields
    // Superblock starts at 1024, we need through offset 0x64 (= 100) + 4 = 104
    static constexpr uint32_t NEEDED = EXT_INCOMPAT_OFFSET + 4; // 0x64 = 100 + 4 = 104

    if (size < EXT_SUPERBLOCK_OFFSET + NEEDED)
        return Core::FileSystemType::Unknown;

    uint64_t sbOffset = base + EXT_SUPERBLOCK_OFFSET;

    // Read magic
    uint8_t magicBuf[2];
    if (!reader.ReadBytes(sbOffset + EXT_MAGIC_FIELD_OFFSET, 2, magicBuf))
        return Core::FileSystemType::Unknown;

    uint16_t magic = ReadLE16(magicBuf);
    if (magic != EXT_MAGIC)
        return Core::FileSystemType::Unknown;

    // Read revision level
    uint8_t revBuf[4];
    if (!reader.ReadBytes(sbOffset + EXT_REV_LEVEL_OFFSET, 4, revBuf))
        return Core::FileSystemType::Unknown;

    uint32_t revLevel = ReadLE32(revBuf);

    // Revision 0 (original ext2) has no feature flags
    if (revLevel == 0)
        return Core::FileSystemType::EXT2;

    // Read compat + incompat feature flags
    uint8_t compatBuf[4], incompatBuf[4];
    if (!reader.ReadBytes(sbOffset + EXT_COMPAT_OFFSET, 4, compatBuf))
        return Core::FileSystemType::Unknown;  // Ambiguous: can't read flags
    if (!reader.ReadBytes(sbOffset + EXT_INCOMPAT_OFFSET, 4, incompatBuf))
        return Core::FileSystemType::Unknown;  // Ambiguous: can't read flags

    uint32_t compatFeatures   = ReadLE32(compatBuf);
    uint32_t incompatFeatures = ReadLE32(incompatBuf);

    // ext4: any ext4-only incompat feature set
    if (incompatFeatures & EXT4_INCOMPAT_MASK)
        return Core::FileSystemType::EXT4;

    // ext3: has journal but no ext4 features
    if (compatFeatures & EXT3_COMPAT_HAS_JOURNAL)
        return Core::FileSystemType::EXT3;

    // ext2: no journal, no ext4 features
    return Core::FileSystemType::EXT2;
}

// ---- XFS ----
// Superblock at partition offset 0, magic "XFSB" (4 bytes big-endian = 0x58465342)
// sb_blocksize at offset 4 (uint32 BE), must be power of 2, 512..65536

Core::FileSystemType FilesystemDetector::TryXFS(
    Core::ByteReader& reader, uint64_t base, uint64_t size)
{
    if (size < 8) return Core::FileSystemType::Unknown;

    uint8_t buf[8];
    if (!reader.ReadBytes(base, 8, buf))
        return Core::FileSystemType::Unknown;

    uint32_t magic = ReadBE32(buf);
    if (magic != 0x58465342)  // "XFSB"
        return Core::FileSystemType::Unknown;

    uint32_t blockSize = ReadBE32(buf + 4);
    if (!IsPowerOfTwo(blockSize) || blockSize < 512 || blockSize > 65536)
        return Core::FileSystemType::Unknown;

    return Core::FileSystemType::XFS;
}

// ---- HFS+ ----
// Volume Header at partition offset 1024, signature (2 bytes BE) = 0x482B ("H+")
// Version at offset 1026 (2 bytes BE) — typically 4 (HFS+) or 5 (HFSX)

Core::FileSystemType FilesystemDetector::TryHFSPlus(
    Core::ByteReader& reader, uint64_t base, uint64_t size)
{
    if (size < 1028) return Core::FileSystemType::Unknown;

    uint8_t buf[4];
    if (!reader.ReadBytes(base + 1024, 4, buf))
        return Core::FileSystemType::Unknown;

    uint16_t sig = ReadBE16(buf);
    if (sig != 0x482B && sig != 0x4858)  // "H+" or "HX" (HFSX)
        return Core::FileSystemType::Unknown;

    uint16_t version = ReadBE16(buf + 2);
    if (version < 4 || version > 5)
        return Core::FileSystemType::Unknown;

    return Core::FileSystemType::HFSPlus;
}

// ---- APFS ----
// Container Superblock: obj_phys_t header (32 bytes) + nx_magic at offset 32
// nx_magic (4 bytes LE) = 0x4253584E ("NXSB")
// nx_block_size at offset 36 (4 bytes LE), must be power of 2
//
// This is CONTAINER DETECTION ONLY — not APFS volume parsing.

Core::FileSystemType FilesystemDetector::TryAPFS(
    Core::ByteReader& reader, uint64_t base, uint64_t size)
{
    if (size < 40) return Core::FileSystemType::Unknown;

    uint8_t buf[8];
    if (!reader.ReadBytes(base + 32, 8, buf))
        return Core::FileSystemType::Unknown;

    uint32_t magic = ReadLE32(buf);
    if (magic != 0x4253584E)  // "NXSB" in LE
        return Core::FileSystemType::Unknown;

    uint32_t blockSize = ReadLE32(buf + 4);
    if (!IsPowerOfTwo(blockSize) || blockSize < 512 || blockSize > 65536)
        return Core::FileSystemType::Unknown;

    return Core::FileSystemType::APFS;
}

} // namespace Filesystems
} // namespace Recovery
