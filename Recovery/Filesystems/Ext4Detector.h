#pragma once

#include "../Core/ByteReader.h"
#include "../Core/StorageRegion.h"
#include <string>

namespace Recovery {
namespace Filesystems {

    /**
     * @brief High-level metadata extracted from an ext4 filesystem.
     */
    struct Ext4Metadata {
        bool        isValid;
        uint32_t    blockSize;
        uint64_t    totalBlocks;
        uint64_t    freeBlocks;
        uint32_t    totalInodes;
        uint32_t    freeInodes;
        uint16_t    inodeSize;
        std::string volumeName;
        std::string uuid;
        bool        hasExtents;
        bool        is64Bit;

        Ext4Metadata()
            : isValid(false)
            , blockSize(0)
            , totalBlocks(0)
            , freeBlocks(0)
            , totalInodes(0)
            , freeInodes(0)
            , inodeSize(0)
            , hasExtents(false)
            , is64Bit(false)
        {}
    };

    /**
     * @brief Read-only detector for ext4 filesystems.
     * Probes any storage region (whole drive or individual partition)
     * for an ext4 superblock at offset + 1024 bytes.
     */
    class Ext4Detector {
    public:
        // Probes the region to check for the ext4 magic 0xEF53 at offset + 1024
        static bool CanParse(Core::ByteReader& reader, const Core::StorageRegion& region = Core::StorageRegion());

        // Parses the superblock and extracts forensic filesystem metadata
        static Ext4Metadata Parse(Core::ByteReader& reader, const Core::StorageRegion& region = Core::StorageRegion());
    };

} // namespace Filesystems
} // namespace Recovery
