#pragma once

#include <cstdint>
#include <cstring>
#include <string>

namespace Recovery {
namespace Core {

    /**
     * @brief Partition table scheme that produced this entry.
     */
    enum class PartitionScheme {
        MBR,
        GPT
    };

    /**
     * @brief Filesystem-independent partition descriptor.
     *
     * Used by both MBR and GPT parsers to describe a discovered partition.
     * Fields specific to one scheme are zero-initialized when the other
     * scheme produced the entry.
     *
     * This struct does NOT contain filesystem-specific fields (cluster size,
     * MFT offset, etc.) — those belong to the filesystem-detection phase.
     */
    struct PartitionInfo {
        uint32_t        index;          // 0-based partition index

        uint64_t        startLBA;       // Starting Logical Block Address
        uint64_t        sectorCount;    // Size in sectors
        uint64_t        startOffset;    // startLBA * sectorSize (bytes)
        uint64_t        sizeBytes;      // sectorCount * sectorSize (bytes)

        PartitionScheme scheme;         // MBR or GPT

        // --- MBR-specific fields ---
        uint8_t         mbrTypeByte;    // Raw partition type byte (0x07=NTFS, 0x0C=FAT32, etc.)
        bool            bootable;       // Boot indicator (0x80 = active/bootable)

        // --- GPT-specific fields ---
        uint8_t         typeGUID[16];   // Partition type GUID
        uint8_t         uniqueGUID[16]; // Unique partition GUID
        uint64_t        gptAttributes;  // GPT attribute flags
        std::string     gptName;        // UTF-16LE partition name, converted to UTF-8

        // Human-readable description of the partition type
        // e.g. "NTFS/exFAT (0x07)" for MBR, "Microsoft Basic Data" for GPT
        std::string     description;

        /**
         * @brief Default constructor — zero-initializes all fields.
         */
        PartitionInfo()
            : index(0)
            , startLBA(0)
            , sectorCount(0)
            , startOffset(0)
            , sizeBytes(0)
            , scheme(PartitionScheme::MBR)
            , mbrTypeByte(0)
            , bootable(false)
            , typeGUID{}
            , uniqueGUID{}
            , gptAttributes(0)
        {
            std::memset(typeGUID, 0, sizeof(typeGUID));
            std::memset(uniqueGUID, 0, sizeof(uniqueGUID));
        }
    };

} // namespace Core
} // namespace Recovery
