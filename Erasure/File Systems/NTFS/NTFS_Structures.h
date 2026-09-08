#pragma once

#include <cstdint>
#include <vector>
#include <string>

#pragma pack(push, 1) // Strictly pack all on-disk structures (1-byte alignment) to match exact raw disk layout

namespace Erasure {
namespace FileSystems {
namespace NTFS {

    // =========================================================================
    // Core NTFS Constants & Magic Numbers
    // =========================================================================
    constexpr uint32_t NTFS_MAGIC_FILE = 0x454C4946; // ASCII "FILE"
    constexpr uint32_t NTFS_MAGIC_INDX = 0x58444E49; // ASCII "INDX"
    constexpr uint32_t NTFS_MAGIC_BAAD = 0x44414142; // ASCII "BAAD"
    constexpr uint16_t NTFS_BOOT_SIGNATURE = 0xAA55;

    // Standard Well-Known MFT Record Numbers
    constexpr uint64_t MFT_REC_MFT        = 0;   // Master File Table itself
    constexpr uint64_t MFT_REC_MFTMIRR    = 1;   // Mirror of first 4 MFT records
    constexpr uint64_t MFT_REC_LOGFILE    = 2;   // Transactional log
    constexpr uint64_t MFT_REC_VOLUME     = 3;   // Volume label, version
    constexpr uint64_t MFT_REC_ATTRDEF    = 4;   // Attribute definitions
    constexpr uint64_t MFT_REC_ROOT       = 5;   // Root Directory "."
    constexpr uint64_t MFT_REC_BITMAP     = 6;   // Volume cluster allocation bitmap
    constexpr uint64_t MFT_REC_BOOT       = 7;   // Boot sector duplicate/info
    constexpr uint64_t MFT_REC_BADCLUST   = 8;   // Bad cluster list
    constexpr uint64_t MFT_REC_SECURE     = 9;   // Security descriptors ($SDS, $SDH, $SII)
    constexpr uint64_t MFT_REC_UPCASE     = 10;  // Uppercase character mapping
    constexpr uint64_t MFT_REC_EXTEND     = 11;  // Extension directory
    constexpr uint64_t MFT_REC_USER_START = 16;  // Start of regular user files/directories

    // Attribute Types
    constexpr uint32_t ATTR_STANDARD_INFORMATION = 0x10;
    constexpr uint32_t ATTR_ATTRIBUTE_LIST       = 0x20;
    constexpr uint32_t ATTR_FILE_NAME            = 0x30;
    constexpr uint32_t ATTR_OBJECT_ID            = 0x40;
    constexpr uint32_t ATTR_SECURITY_DESCRIPTOR  = 0x50;
    constexpr uint32_t ATTR_VOLUME_NAME          = 0x60;
    constexpr uint32_t ATTR_VOLUME_INFORMATION   = 0x70;
    constexpr uint32_t ATTR_DATA                 = 0x80;
    constexpr uint32_t ATTR_INDEX_ROOT           = 0x90;
    constexpr uint32_t ATTR_INDEX_ALLOCATION     = 0xA0;
    constexpr uint32_t ATTR_BITMAP               = 0xB0;
    constexpr uint32_t ATTR_REPARSE_POINT        = 0xC0;
    constexpr uint32_t ATTR_END                  = 0xFFFFFFFF;

    // Record Flags
    constexpr uint16_t FILE_RECORD_IN_USE        = 0x0001;
    constexpr uint16_t FILE_RECORD_DIRECTORY     = 0x0002;

    // Index Entry Flags
    constexpr uint16_t INDEX_ENTRY_HAS_SUBNODES  = 0x0001;
    constexpr uint16_t INDEX_ENTRY_LAST          = 0x0002;

    // File Name Namespaces
    constexpr uint8_t  FILE_NAME_POSIX           = 0;
    constexpr uint8_t  FILE_NAME_WIN32           = 1;
    constexpr uint8_t  FILE_NAME_DOS             = 2;
    constexpr uint8_t  FILE_NAME_WIN32_AND_DOS   = 3;

    // =========================================================================
    // Volume Boot Record (Sector 0)
    // =========================================================================
    struct NtfsBootSector {
        uint8_t  jumpInstruction[3];    // e.g. 0xEB, 0x52, 0x90
        char     oemId[8];              // "NTFS    "
        uint16_t bytesPerSector;        // e.g. 512 or 4096
        uint8_t  sectorsPerCluster;     // e.g. 1, 2, 4, 8
        uint16_t reservedSectors;       // Must be 0 in NTFS
        uint8_t  mustBeZero1[3];
        uint16_t unused1;
        uint8_t  mediaDescriptor;       // e.g. 0xF8 (fixed disk)
        uint16_t mustBeZero2;
        uint16_t sectorsPerTrack;
        uint16_t numberOfHeads;
        uint32_t hiddenSectors;
        uint32_t unused2;
        uint32_t unused3;
        uint64_t totalSectors;          // Total volume sectors
        uint64_t mftStartLCN;           // Starting LCN of $MFT
        uint64_t mftMirrStartLCN;       // Starting LCN of $MFTMirr
        int8_t   clustersPerMftRecord;  // If negative: 2^|x| bytes (e.g. -10 -> 1024 bytes)
        uint8_t  mustBeZero3[3];
        int8_t   clustersPerIndexBuffer;// If negative: 2^|x| bytes (e.g. -12 -> 4096 bytes)
        uint8_t  mustBeZero4[3];
        uint64_t volumeSerialNumber;    // 64-bit volume serial number
        uint32_t checksum;
        uint8_t  bootCode[426];
        uint16_t bootSignature;         // 0xAA55
    };

    // =========================================================================
    // MFT Record Header (1024 bytes on disk)
    // =========================================================================
    struct NtfsRecordHeader {
        uint32_t magic;                 // "FILE" (0x454C4946)
        uint16_t updateSequenceOffset;  // Offset to fixup array (e.g. 0x30)
        uint16_t updateSequenceSize;    // Count of USN + array entries (e.g. 3)
        uint64_t logSequenceNumber;     // $LogFile LSN
        uint16_t sequenceNumber;        // Sequence number incremented upon reuse
        uint16_t hardLinkCount;         // Number of directory links
        uint16_t firstAttributeOffset;  // Offset to first attribute (e.g. 0x38)
        uint16_t flags;                 // 0x0001 = In-Use, 0x0002 = Directory
        uint32_t usedBytes;             // Actual bytes consumed by record
        uint32_t allocatedBytes;        // Allocated size (typically 1024)
        uint64_t baseFileRecord;        // 0 if base record; else reference to base
        uint16_t nextAttributeInstance; // Next attribute instance ID
        uint16_t reserved;
        uint32_t recordNumber;          // MFT record number (NTFS 3.1+)
    };

    // =========================================================================
    // Common Attribute Header
    // =========================================================================
    struct NtfsAttributeHeader {
        uint32_t type;                  // Attribute Type (e.g. 0x10, 0x30, 0x80)
        uint32_t length;                // Total length including this header
        uint8_t  nonResidentFlag;       // 0 = Resident, 1 = Non-resident
        uint8_t  nameLength;            // Name length in UTF-16 characters
        uint16_t nameOffset;            // Offset to name from start of attribute
        uint16_t flags;                 // 0x0001 = Compressed, 0x4000 = Encrypted, 0x8000 = Sparse
        uint16_t attributeId;           // Attribute instance ID
    };

    // Resident Attribute Header (appended to NtfsAttributeHeader if nonResidentFlag == 0)
    struct NtfsResidentAttributeHeader {
        uint32_t valueLength;           // Length of attribute data payload
        uint16_t valueOffset;           // Offset to attribute data from start of attribute
        uint8_t  residentFlags;         // 0x01 = Indexed
        uint8_t  reserved;
    };

    // Non-Resident Attribute Header (appended to NtfsAttributeHeader if nonResidentFlag == 1)
    struct NtfsNonResidentAttributeHeader {
        uint64_t startingVCN;           // Starting Virtual Cluster Number
        uint64_t highestVCN;            // Highest Virtual Cluster Number
        uint16_t dataRunsOffset;        // Offset to data run list from start of attribute
        uint16_t compressionUnitSize;   // Compression unit size
        uint32_t padding;
        uint64_t allocatedSize;         // Allocated size on disk (cluster aligned)
        uint64_t dataSize;              // Actual data size in bytes
        uint64_t initializedSize;       // Initialized data size
    };

    // =========================================================================
    // Attribute Payload Structures
    // =========================================================================

    // 0x10: $STANDARD_INFORMATION
    struct NtfsStandardInformation {
        uint64_t creationTime;
        uint64_t lastModificationTime;
        uint64_t mftChangeTime;
        uint64_t lastAccessTime;
        uint32_t dosFilePermissions;
        uint32_t maxVersions;
        uint32_t versionNumber;
        uint32_t classId;
        uint32_t ownerId;
        uint32_t securityId;
        uint64_t quotaCharged;
        uint64_t usn;
    };

    // 0x30: $FILE_NAME
    struct NtfsFileNameAttribute {
        uint64_t parentDirectory;       // MFT record of parent directory (lowest 48 bits)
        uint64_t creationTime;
        uint64_t lastModificationTime;
        uint64_t mftChangeTime;
        uint64_t lastAccessTime;
        uint64_t allocatedSize;
        uint64_t realSize;
        uint32_t flags;                 // 0x10000000 = Directory, etc.
        uint32_t reparseValue;
        uint8_t  fileNameLength;        // Length in UTF-16 characters
        uint8_t  namespaceType;         // 0=POSIX, 1=Win32, 2=DOS, 3=Win32&DOS
        char16_t fileName[1];           // Variable length UTF-16LE string
    };

    // =========================================================================
    // Directory B-Tree Index Structures ($INDEX_ROOT & $INDEX_ALLOCATION)
    // =========================================================================

    // 0x90: $INDEX_ROOT Node Header
    struct NtfsIndexRootHeader {
        uint32_t attributeType;         // Type of indexed attribute (usually 0x30 $FILE_NAME)
        uint32_t collationRule;         // Collation rule (usually 0x01 binary)
        uint32_t indexAllocationEntrySize; // Size of each index record (typically 4096 bytes)
        uint8_t  clustersPerIndexRecord;   // Clusters per index record
        uint8_t  padding[3];
    };

    // Header inside Index Root and Index Allocation Blocks
    struct NtfsIndexHeader {
        uint32_t firstEntryOffset;      // Offset to first index entry from header start
        uint32_t totalEntriesSize;      // Total size of index entries
        uint32_t allocatedSize;         // Allocated size of index buffer
        uint8_t  flags;                 // 0x01 = Node has child sub-nodes
        uint8_t  padding[3];
    };

    // Generic Index Entry in B-Tree
    struct NtfsIndexEntry {
        uint64_t fileReference;         // Lower 48 bits: MFT record number; upper 16 bits: sequence number
        uint16_t length;                // Total entry length
        uint16_t keyLength;             // Length of index key ($FILE_NAME)
        uint16_t flags;                 // 0x01 = Has sub-node (child VCN), 0x02 = Last entry in node
        uint16_t reserved;
        // Followed by NtfsFileNameAttribute if not last entry
        // If (flags & 0x01): last 8 bytes are child VCN in $INDEX_ALLOCATION
    };

    // 4096-byte "INDX" block in non-resident $INDEX_ALLOCATION stream
    struct NtfsIndexBlock {
        uint32_t magic;                 // "INDX" (0x58444E49)
        uint16_t updateSequenceOffset;  // Offset to fixup array
        uint16_t updateSequenceSize;    // Count of USN + array entries
        uint64_t logSequenceNumber;     // $LogFile LSN
        uint64_t indexVCN;              // Virtual cluster number of this block in index
        NtfsIndexHeader indexHeader;    // Embedded index header
    };

    // =========================================================================
    // Helper Data Structures for Engine Operations
    // =========================================================================

    // Represents a continuous cluster run: [start LCN, cluster count]
    struct NtfsExtent {
        uint64_t lcn;          // Logical Cluster Number (physical cluster on disk)
        uint64_t clusterCount; // Number of contiguous clusters
    };

    // Descriptor bundle storing physical locations for before/after verification
    struct TargetLocations {
        bool isValid = false;
        bool isDirectory = false;
        bool isResident = false;
        std::string path;

        // MFT Record physical location
        uint64_t mftRecordNum = 0;
        uint64_t mftSector = 0;
        uint32_t mftByteOffsetInSector = 0;
        uint32_t mftRecordSize = 1024;

        // Data payload extents (physical sectors)
        std::vector<NtfsExtent> dataExtents;
        uint64_t fileSize = 0;

        // Parent directory entry location
        uint64_t parentDirRecordNum = 0;
        uint64_t parentIndexSector = 0;
        uint32_t parentIndexByteOffset = 0;
        uint32_t parentIndexEntrySize = 0;

        // Cluster Bitmap location
        uint64_t bitmapSector = 0;
        uint32_t bitmapByteOffsetInSector = 0;
        uint8_t  bitmapOriginalByte = 0;
        uint8_t  bitmapBitMask = 0;
    };

} // namespace NTFS
} // namespace FileSystems
} // namespace Erasure

#pragma pack(pop) // Restore default structure packing
