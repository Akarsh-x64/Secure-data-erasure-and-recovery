#pragma once

#include <cstdint>

#pragma pack(push, 1) // Strictly pack all on-disk structures (1-byte alignment)

namespace Erasure {
namespace FileSystems {
namespace FAT32 {

    // =========================================================================
    // Core FAT32 Constants & Signatures
    // =========================================================================
    constexpr uint16_t FAT32_BOOT_SIGNATURE       = 0xAA55;
    constexpr uint32_t FAT32_FSINFO_LEAD_SIG      = 0x41615252; // "RRaA"
    constexpr uint32_t FAT32_FSINFO_STRUC_SIG     = 0x61417272; // "rrAa"
    constexpr uint32_t FAT32_FSINFO_TRAIL_SIG     = 0xAA550000;

    // Cluster Entry Special Markers (28-bit values)
    constexpr uint32_t FAT32_CLUSTER_FREE         = 0x00000000;
    constexpr uint32_t FAT32_CLUSTER_RESERVED     = 0x00000001;
    constexpr uint32_t FAT32_CLUSTER_BAD          = 0x0FFFFFF7;
    constexpr uint32_t FAT32_CLUSTER_EOC_MIN      = 0x0FFFFFF8;
    constexpr uint32_t FAT32_CLUSTER_EOC_MAX      = 0x0FFFFFFF;
    constexpr uint32_t FAT32_CLUSTER_MASK         = 0x0FFFFFFF; // Mask out reserved top 4 bits

    // Directory Entry Markers
    constexpr uint8_t  FAT32_DIR_ENTRY_FREE_ALL   = 0x00; // Entry is free and all subsequent entries are free
    constexpr uint8_t  FAT32_DIR_ENTRY_DELETED    = 0xE5; // Entry is deleted / unallocated
    constexpr uint8_t  FAT32_DIR_ENTRY_KANJI      = 0x05; // Actual initial byte is 0xE5 (Kanji escape)

    // Directory Attributes
    constexpr uint8_t  FAT32_ATTR_READ_ONLY       = 0x01;
    constexpr uint8_t  FAT32_ATTR_HIDDEN          = 0x02;
    constexpr uint8_t  FAT32_ATTR_SYSTEM          = 0x04;
    constexpr uint8_t  FAT32_ATTR_VOLUME_ID       = 0x08;
    constexpr uint8_t  FAT32_ATTR_DIRECTORY       = 0x10;
    constexpr uint8_t  FAT32_ATTR_ARCHIVE         = 0x20;
    constexpr uint8_t  FAT32_ATTR_LONG_NAME       = 0x0F; // READ_ONLY | HIDDEN | SYSTEM | VOLUME_ID

    // LFN Order Masks
    constexpr uint8_t  FAT32_LFN_LAST_ENTRY       = 0x40; // Bit 6 denotes final physical LFN entry (first in sequence)
    constexpr uint8_t  FAT32_LFN_ORDER_MASK       = 0x1F; // Lower 5 bits represent sequence order (1..31)

    // =========================================================================
    // Volume Boot Record (VBR / Sector 0 / 512 Bytes)
    // =========================================================================
    struct Fat32BootSector {
        uint8_t  jmpBoot[3];           // Jump instruction (e.g., 0xEB, 0x58, 0x90)
        char     oemName[8];           // OEM Name (e.g., "MSWIN4.1")
        uint16_t bytesPerSector;       // BPB_BytsPerSec (512, 1024, 2048, 4096)
        uint8_t  sectorsPerCluster;    // BPB_SecPerClus (1, 2, 4, 8, 16, 32, 64, 128)
        uint16_t reservedSectorCount;  // BPB_RsvdSecCnt (Typically 32)
        uint8_t  numFATs;              // BPB_NumFATs (Typically 2)
        uint16_t rootEntryCount;       // BPB_RootEntCnt (Must be 0 for FAT32)
        uint16_t totalSectors16;       // BPB_TotSec16 (Must be 0 for FAT32)
        uint8_t  media;                // BPB_Media (0xF8 for fixed disk)
        uint16_t fatSize16;            // BPB_FATSz16 (Must be 0 for FAT32)
        uint16_t sectorsPerTrack;      // BPB_SecPerTrk
        uint16_t numHeads;             // BPB_NumHeads
        uint32_t hiddenSectors;        // BPB_HiddSec
        uint32_t totalSectors32;       // BPB_TotSec32 (Total partition sector count)
        
        // FAT32 Extended BPB
        uint32_t fatSize32;            // BPB_FATSz32 (Sectors per FAT table)
        uint16_t extFlags;             // BPB_ExtFlags (Mirroring flags & active FAT index)
        uint16_t fsVersion;            // BPB_FSVer (0x0000)
        uint32_t rootCluster;          // BPB_RootClus (Starting cluster of root dir, typically 2)
        uint16_t fsInfoSector;         // BPB_FSInfo (Sector of FSINFO structure, typically 1)
        uint16_t backupBootSector;     // BPB_BkBootSec (Backup VBR sector, typically 6)
        uint8_t  reserved[12];         // Reserved for future expansion
        uint8_t  driveNumber;          // BS_DrvNum (0x80 for hard drive)
        uint8_t  reserved1;            // BS_Reserved1
        uint8_t  bootSignature;        // BS_BootSig (0x29 extended signature)
        uint32_t volumeID;             // BS_VolID (32-bit volume serial number)
        char     volumeLabel[11];      // BS_VolLab ("NO NAME    ")
        char     fileSystemType[8];    // BS_FilSysType ("FAT32   ")
        uint8_t  bootCode[420];        // Boot bootstrap code
        uint16_t signature;            // 0xAA55
    };
    static_assert(sizeof(Fat32BootSector) == 512, "Fat32BootSector must be exactly 512 bytes");

    // =========================================================================
    // FSInfo Sector (Sector 1 / 512 Bytes)
    // =========================================================================
    struct Fat32FSInfo {
        uint32_t leadSig;              // 0x41615252 ("RRaA")
        uint8_t  reserved1[480];       // Reserved block
        uint32_t strucSig;             // 0x61417272 ("rrAa")
        uint32_t freeCount;            // Free cluster count (0xFFFFFFFF = unknown)
        uint32_t nextFree;             // Most recently allocated cluster hint
        uint8_t  reserved2[12];        // Reserved
        uint32_t trailSig;             // 0xAA550000
    };
    static_assert(sizeof(Fat32FSInfo) == 512, "Fat32FSInfo must be exactly 512 bytes");

    // =========================================================================
    // Short File Name (SFN) Directory Entry (32 Bytes)
    // =========================================================================
    struct Fat32DirEntry {
        uint8_t  name[11];             // 8 bytes name + 3 bytes extension (space-padded)
        uint8_t  attr;                 // File attributes
        uint8_t  ntRes;                // Reserved for Windows NT (e.g. lowercase flags)
        uint8_t  crtTimeTenth;         // Millisecond stamp at creation
        uint16_t crtTime;              // Creation time
        uint16_t crtDate;              // Creation date
        uint16_t lstAccDate;           // Last access date
        uint16_t fstClusHI;            // High 16 bits of first cluster
        uint16_t wrtTime;              // Last modification time
        uint16_t wrtDate;              // Last modification date
        uint16_t fstClusLO;            // Low 16 bits of first cluster
        uint32_t fileSize;             // Real file size in bytes
    };
    static_assert(sizeof(Fat32DirEntry) == 32, "Fat32DirEntry must be exactly 32 bytes");

    // =========================================================================
    // Long File Name (LFN / VFAT) Directory Entry (32 Bytes)
    // =========================================================================
    struct Fat32LfnEntry {
        uint8_t  order;                // Sequence number (masked with 0x40 for final entry)
        char16_t name1[5];             // Characters 1-5 of long name (UTF-16LE)
        uint8_t  attr;                 // Always 0x0F (FAT32_ATTR_LONG_NAME)
        uint8_t  type;                 // Always 0x00
        uint8_t  checksum;             // Checksum of short 8.3 directory entry
        char16_t name2[6];             // Characters 6-11 of long name (UTF-16LE)
        uint16_t firstClusterLO;       // Must be 0x0000
        char16_t name3[2];             // Characters 12-13 of long name (UTF-16LE)
    };
    static_assert(sizeof(Fat32LfnEntry) == 32, "Fat32LfnEntry must be exactly 32 bytes");

} // namespace FAT32
} // namespace FileSystems
} // namespace Erasure

#pragma pack(pop)
