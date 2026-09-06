#pragma once

#include "../Core/RecoveryEnums.h"
#include "../Core/FileRecord.h"
#include "../Core/DataRange.h"

#include <cstdint>
#include <vector>

namespace Recovery {
namespace Filesystems {

    /**
     * @brief Abstract read-only filesystem interface for recovery.
     *
     * Each supported filesystem (NTFS, exFAT, ext4, ...) implements this
     * interface to expose file enumeration and data-range discovery
     * without any write operations.
     *
     * Phase 3 defines the interface only. Concrete implementations
     * are created in Phase 4 (NTFS) and Phase 5 (exFAT adapter).
     */
    class IRecoveryFileSystem {
    public:
        virtual ~IRecoveryFileSystem() = default;

        /**
         * @brief Parse filesystem metadata structures (boot sector, MFT, inodes, etc.).
         *
         * Must be called before any enumeration or data access.
         * Does NOT modify the source storage.
         */
        virtual bool Mount() = 0;

        /**
         * @brief Returns the filesystem type this adapter handles.
         */
        virtual Core::FileSystemType GetType() const = 0;

        /**
         * @brief Discovers all files (allocated, deleted, orphaned) and appends to results.
         */
        virtual bool EnumerateFiles(
            std::vector<Core::FileRecord>& results) = 0;

        /**
         * @brief Retrieves metadata for a specific file by its ID.
         */
        virtual bool GetFileMetadata(
            uint64_t fileId,
            Core::FileRecord& metadata) = 0;

        /**
         * @brief Returns the physical data ranges for a file's content.
         */
        virtual bool GetDataRanges(
            uint64_t fileId,
            std::vector<Core::DataRange>& ranges) = 0;

        /**
         * @brief Reads the complete file content into output.
         */
        virtual bool ReadFile(
            uint64_t fileId,
            std::vector<uint8_t>& output) = 0;
    };

} // namespace Filesystems
} // namespace Recovery
