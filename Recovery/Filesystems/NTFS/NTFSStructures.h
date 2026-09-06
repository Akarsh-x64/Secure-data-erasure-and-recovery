#pragma once

#include <cstdint>

namespace Recovery {
namespace Filesystems {
namespace NTFS {

// ---- Attribute type codes ----

static constexpr uint32_t ATTR_TYPE_STANDARD_INFO = 0x10;
static constexpr uint32_t ATTR_TYPE_ATTRIBUTE_LIST = 0x20;
static constexpr uint32_t ATTR_TYPE_FILE_NAME    = 0x30;
static constexpr uint32_t ATTR_TYPE_DATA          = 0x80;
static constexpr uint32_t ATTR_TYPE_INDEX_ROOT    = 0x90;
static constexpr uint32_t ATTR_TYPE_INDEX_ALLOC   = 0xA0;
static constexpr uint32_t ATTR_TYPE_BITMAP        = 0xB0;
static constexpr uint32_t ATTR_TYPE_END           = 0xFFFFFFFF;

// ---- MFT record flags ----

static constexpr uint16_t MFT_RECORD_IN_USE  = 0x0001;
static constexpr uint16_t MFT_RECORD_IS_DIR  = 0x0002;

// ---- Well-known MFT record indices ----

static constexpr uint64_t MFT_RECORD_MFT            = 0;
static constexpr uint64_t MFT_RECORD_MFT_MIRROR     = 1;
static constexpr uint64_t MFT_RECORD_LOG_FILE       = 2;
static constexpr uint64_t MFT_RECORD_VOLUME          = 3;
static constexpr uint64_t MFT_RECORD_ATTR_DEF        = 4;
static constexpr uint64_t MFT_RECORD_ROOT_DIR        = 5;
static constexpr uint64_t MFT_RECORD_BITMAP          = 6;
static constexpr uint64_t MFT_RECORD_BOOT            = 7;
static constexpr uint64_t MFT_RECORD_BAD_CLUSTER     = 8;
static constexpr uint64_t MFT_RECORD_SECURE          = 9;
static constexpr uint64_t MFT_RECORD_UPCASE          = 10;
static constexpr uint64_t MFT_RECORD_EXTEND          = 11;
static constexpr uint64_t MFT_RECORD_FIRST_USER      = 24;

// ---- $FILE_NAME namespace values ----

static constexpr uint8_t FILENAME_NAMESPACE_POSIX = 0;
static constexpr uint8_t FILENAME_NAMESPACE_WIN32 = 1;
static constexpr uint8_t FILENAME_NAMESPACE_DOS   = 2;
static constexpr uint8_t FILENAME_NAMESPACE_WIN32_AND_DOS = 3;

// ---- NTFS epoch conversion ----
// NTFS timestamps: 100-nanosecond intervals since 1601-01-01
// Unix epoch:      seconds since 1970-01-01
// Difference = 11644473600 seconds = 116444736000000000 in 100ns units

static constexpr uint64_t NTFS_EPOCH_DIFF = 116444736000000000ULL;

inline uint64_t NtfsTimeToUnix(uint64_t ntfsTime) {
    if (ntfsTime == 0 || ntfsTime < NTFS_EPOCH_DIFF) return 0;
    return (ntfsTime - NTFS_EPOCH_DIFF) / 10000000ULL;
}

// ======================================================================
// On-disk structures — all #pragma pack(push,1) for exact byte layout
// ======================================================================

#pragma pack(push, 1)

/**
 * @brief NTFS Boot Sector (VBR) — first 512 bytes of the partition.
 *
 * We only define the fields we actually use for recovery.
 * The full VBR is 512 bytes but we need through offset 0x40.
 */
struct NTFSBootSector {
    uint8_t  jump[3];           // 0x00: Jump instruction
    char     oemId[8];          // 0x03: "NTFS    "

    // BIOS Parameter Block
    uint16_t bytesPerSector;    // 0x0B
    uint8_t  sectorsPerCluster; // 0x0D
    uint16_t reservedSectors;   // 0x0E
    uint8_t  _unused1[3];       // 0x10: always 0 for NTFS
    uint16_t _unused2;          // 0x13: 0 for NTFS
    uint8_t  mediaDescriptor;   // 0x15
    uint16_t _unused3;          // 0x16: 0 for NTFS
    uint16_t sectorsPerTrack;   // 0x18
    uint16_t numberOfHeads;     // 0x1A
    uint32_t hiddenSectors;     // 0x1C
    uint32_t _unused4;          // 0x20: 0 for NTFS

    // Extended NTFS BPB
    uint32_t _unused5;          // 0x24: 0x00800080 typically
    uint64_t totalSectors;      // 0x28
    uint64_t mftLCN;            // 0x30: Logical Cluster Number of $MFT
    uint64_t mftMirrorLCN;      // 0x38: LCN of $MFTMirr
    int8_t   mftRecordSize;     // 0x40: MFT record size encoding
    uint8_t  _pad1[3];          // 0x41
    int8_t   indexBlockSize;    // 0x44: Index block size encoding
    uint8_t  _pad2[3];          // 0x45
    uint64_t volumeSerial;      // 0x48: Volume serial number
};

/**
 * @brief MFT FILE record header.
 *
 * Every MFT entry starts with this header. The signature should be "FILE".
 * Deleted entries may have signature "FILE" with IN_USE flag cleared,
 * or may have been zeroed out entirely.
 */
struct MFTEntryHeader {
    char     signature[4];          // 0x00: "FILE"
    uint16_t fixupArrayOffset;      // 0x04: Offset to update sequence array
    uint16_t fixupArrayCount;       // 0x06: Size of fixup array (incl. signature)
    uint64_t logFileSeqNumber;      // 0x08: $LogFile sequence number
    uint16_t sequenceNumber;        // 0x10: Sequence number (incremented on reuse)
    uint16_t hardLinkCount;         // 0x12
    uint16_t firstAttributeOffset;  // 0x14: Offset to first attribute
    uint16_t flags;                 // 0x16: MFT_RECORD_IN_USE | MFT_RECORD_IS_DIR
    uint32_t usedSize;              // 0x18: Used size of MFT entry
    uint32_t allocatedSize;         // 0x1C: Allocated size of MFT entry
    uint64_t baseRecordRef;         // 0x20: Base MFT record (for extension records)
    uint16_t nextAttributeId;       // 0x28
};

/**
 * @brief Common attribute header (shared by resident and non-resident).
 */
struct AttributeHeader {
    uint32_t type;              // 0x00: Attribute type code
    uint32_t length;            // 0x04: Total attribute length (including header)
    uint8_t  nonResident;       // 0x08: 0 = resident, 1 = non-resident
    uint8_t  nameLength;        // 0x09: Length of attribute name (in UTF-16 chars)
    uint16_t nameOffset;        // 0x0A: Offset to attribute name
    uint16_t flags;             // 0x0C: Attribute flags (compressed, encrypted, sparse)
    uint16_t attributeId;       // 0x0E: Unique ID within this MFT record
};

/**
 * @brief Resident attribute: data is stored inline within the MFT record.
 */
struct ResidentAttrHeader {
    AttributeHeader header;     // 0x00: Common header (16 bytes)
    uint32_t valueLength;       // 0x10: Length of the attribute value
    uint16_t valueOffset;       // 0x14: Offset to the value (from attribute start)
    uint16_t indexedFlag;       // 0x16
};

/**
 * @brief Non-resident attribute: data is stored in data runs on disk.
 */
struct NonResidentAttrHeader {
    AttributeHeader header;     // 0x00: Common header (16 bytes)
    uint64_t startingVCN;       // 0x10: Starting Virtual Cluster Number
    uint64_t lastVCN;           // 0x18: Last VCN
    uint16_t dataRunOffset;     // 0x20: Offset to data runs (from attribute start)
    uint16_t compressionUnit;   // 0x22
    uint32_t _padding;          // 0x24
    uint64_t allocatedSize;     // 0x28: Allocated size on disk
    uint64_t realSize;          // 0x30: Actual data size
    uint64_t initializedSize;   // 0x38: Initialized data size
};

/**
 * @brief $FILE_NAME attribute body (resident only in NTFS).
 *
 * The filename follows immediately after this structure as
 * UTF-16LE characters (filenameLengthChars * 2 bytes).
 */
struct FileNameAttribute {
    uint64_t parentDirReference; // 0x00: Parent directory MFT ref (6 bytes ref + 2 bytes seq)
    uint64_t creationTime;       // 0x08: File creation time (NTFS timestamp)
    uint64_t modificationTime;   // 0x10: File modification time
    uint64_t mftModificationTime;// 0x18: MFT record modification time
    uint64_t readTime;           // 0x20: File read/access time
    uint64_t allocatedSize;      // 0x28: Allocated size of file
    uint64_t realSize;           // 0x30: Real size of file
    uint32_t flags;              // 0x38: File flags (readonly, hidden, system, etc.)
    uint32_t reparseValue;       // 0x3C: Reparse point / EA
    uint8_t  filenameLengthChars;// 0x40: Filename length in UTF-16 characters
    uint8_t  filenameNamespace;  // 0x41: Namespace (POSIX=0, Win32=1, DOS=2, Win32+DOS=3)
    // UTF-16LE filename follows immediately: filenameLengthChars * 2 bytes
};

#pragma pack(pop)

// ---- Helpers ----

/**
 * @brief Extracts the 48-bit MFT record index from a parent directory reference.
 *
 * NTFS stores parent references as:
 *   bits 0-47:  MFT record number
 *   bits 48-63: sequence number
 */
inline uint64_t ParentRefToMftIndex(uint64_t parentRef) {
    return parentRef & 0x0000FFFFFFFFFFFFULL;
}

/**
 * @brief Decodes the MFT record size field from the boot sector.
 *
 * If the value is positive, it's the number of clusters per MFT record.
 * If negative, the record size is 2^|value| bytes.
 */
inline uint32_t DecodeMftRecordSize(int8_t sizeField, uint32_t bytesPerCluster) {
    if (sizeField > 0) {
        return static_cast<uint32_t>(sizeField) * bytesPerCluster;
    } else {
        return 1u << static_cast<uint32_t>(-sizeField);
    }
}

} // namespace NTFS
} // namespace Filesystems
} // namespace Recovery
