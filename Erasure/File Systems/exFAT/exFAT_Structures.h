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

} // namespace FileSystems
} // namespace Erasure

#pragma pack(pop) // Restore default packing
