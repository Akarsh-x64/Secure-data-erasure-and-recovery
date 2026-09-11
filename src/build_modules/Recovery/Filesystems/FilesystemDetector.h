#pragma once

#include "../Core/RecoveryEnums.h"
#include "../Core/ByteReader.h"
#include "../Core/StorageRegion.h"

#include <string>

namespace Recovery {
namespace Filesystems {

    /**
     * @brief Detects filesystem types from partition boot sectors / superblocks.
     *
     * Operates entirely through ByteReader (read-only). The detector
     * translates partition-relative offsets to absolute disk offsets
     * using the StorageRegion, keeping ByteReader filesystem-agnostic.
     *
     * Detection is signature-based with structural sanity checks:
     *   - NTFS:     OEM ID + BPB validation
     *   - exFAT:    OEM ID + shift validation
     *   - FAT32:    Structural BPB checks (NOT just "FAT" string)
     *   - ext2/3/4: Superblock magic + feature-flag differentiation
     *   - XFS:      Superblock magic + block size validation
     *   - HFS+:     Volume header signature + version check
     *   - APFS:     Container superblock magic (detection only, no parsing)
     */
    class FilesystemDetector {
    public:
        /**
         * @brief Detects the filesystem type of a partition.
         *
         * @param reader     ByteReader wrapping the storage source
         * @param partition  StorageRegion describing the partition's byte range
         * @return Detected FileSystemType, or Unknown if unrecognized
         */
        static Core::FileSystemType Detect(
            Core::ByteReader& reader,
            const Core::StorageRegion& partition);

        /**
         * @brief Returns a human-readable name for a FileSystemType.
         */
        static std::string GetFileSystemName(Core::FileSystemType type);

    private:
        static Core::FileSystemType TryNTFS(Core::ByteReader& reader, uint64_t base, uint64_t size);
        static Core::FileSystemType TryExFAT(Core::ByteReader& reader, uint64_t base, uint64_t size);
        static Core::FileSystemType TryFAT32(Core::ByteReader& reader, uint64_t base, uint64_t size);
        static Core::FileSystemType TryExt(Core::ByteReader& reader, uint64_t base, uint64_t size);
        static Core::FileSystemType TryXFS(Core::ByteReader& reader, uint64_t base, uint64_t size);
        static Core::FileSystemType TryHFSPlus(Core::ByteReader& reader, uint64_t base, uint64_t size);
        static Core::FileSystemType TryAPFS(Core::ByteReader& reader, uint64_t base, uint64_t size);

        static bool IsPowerOfTwo(uint32_t v);
    };

} // namespace Filesystems
} // namespace Recovery
