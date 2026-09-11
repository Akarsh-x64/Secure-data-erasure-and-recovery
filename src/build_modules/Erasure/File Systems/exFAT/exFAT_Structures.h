#pragma once

#include <cstdint>

#pragma pack(push, 1) // Strictly pack structs to match raw disk layout

namespace Erasure {
namespace FileSystems {

// The Main Boot Sector (VBR) for exFAT
struct ExFatBootSector {
    uint8_t  jumpBoot[3];
    char     fileSystemName[8]; // "EXFAT   "
    uint8_t  mustBeZero[53];
    uint64_t partitionOffset;
    uint64_t volumeLengthSectors;
    uint32_t fatOffsetSectors;
    uint32_t fatLengthSectors;
    uint32_t clusterHeapOffsetSectors;
    uint32_t clusterCount;
    uint32_t rootDirectoryFirstCluster;
    uint32_t volumeSerialNumber;
    uint16_t fileSystemRevision;
    uint16_t volumeFlags;
    uint8_t  bytesPerSectorShift;   // 1 << bytesPerSectorShift = bytesPerSector
    uint8_t  sectorsPerClusterShift; // 1 << sectorsPerClusterShift = sectorsPerCluster
    uint8_t  numberOfFats;
    uint8_t  driveSelect;
    uint8_t  percentInUse;
    uint8_t  reserved[7];
    uint8_t  bootCode[390];
    uint16_t bootSignature; // 0xAA55
};

// A generic 32-byte exFAT directory entry
struct ExFatDirectoryEntry {
    uint8_t entryType;
    uint8_t customData[31];
};

// 0x85: File Directory Entry
struct ExFatFileDirectoryEntry {
    uint8_t  entryType; // 0x85
    uint8_t  secondaryCount;
    uint16_t setChecksum;
    uint16_t fileAttributes;
    uint16_t reserved1;
    uint32_t createTimestamp;
    uint32_t lastModifiedTimestamp;
    uint32_t lastAccessedTimestamp;
    uint8_t  create10msIncrement;
    uint8_t  lastModified10msIncrement;
    uint8_t  createUtcOffset;
    uint8_t  lastModifiedUtcOffset;
    uint8_t  lastAccessedUtcOffset;
    uint8_t  reserved2[7];
};

// 0xC0: Stream Extension Directory Entry
struct ExFatStreamExtensionDirectoryEntry {
    uint8_t  entryType; // 0xC0
    uint8_t  generalSecondaryFlags; // bit 1: No FAT chain
    uint8_t  reserved1;
    uint8_t  nameLength;
    uint16_t nameHash;
    uint16_t reserved2;
    uint64_t validDataLength;
    uint32_t reserved3;
    uint32_t firstCluster;
    uint64_t dataLength;
};

// 0xC1: File Name Directory Entry
struct ExFatFileNameDirectoryEntry {
    uint8_t  entryType; // 0xC1
    uint8_t  generalSecondaryFlags;
    char16_t fileName[15]; // 15 unicode characters
};

// 0x81: Allocation Bitmap Directory Entry
struct ExFatBitmapDirectoryEntry {
    uint8_t  entryType; // 0x81
    uint8_t  bitmapFlags;
    uint8_t  reserved[18];
    uint32_t firstCluster;
    uint64_t dataLength;
};

} // namespace FileSystems
} // namespace Erasure

#pragma pack(pop) // Restore default packing
