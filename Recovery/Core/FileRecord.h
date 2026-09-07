#pragma once

#include "RecoveryEnums.h"
#include "DataRange.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Recovery {
namespace Core {

    /**
     * @brief Filesystem-independent file representation.
     *
     * Every filesystem adapter (NTFS, exFAT, ext4, ...) translates its
     * native file metadata into this common struct so the recovery engine
     * can work generically across all filesystems.
     */
    struct FileRecord {
        uint64_t id;                    // Unique ID within this scan session

        std::string path;               // Full path (e.g. "/Users/docs/report.pdf")
        std::string filename;           // Filename only (e.g. "report.pdf")
        std::string extension;          // Extension only (e.g. "pdf")

        uint64_t size;                  // Logical file size in bytes

        uint64_t createdTime;           // Creation timestamp (epoch seconds)
        uint64_t modifiedTime;          // Last modification timestamp
        uint64_t accessedTime;          // Last access timestamp

        bool allocated;                 // Still allocated in the filesystem
        bool deleted;                   // Marked as deleted
        bool orphaned;                  // No parent directory reference

        FileSystemType filesystem;      // Source filesystem type

        uint64_t filesystemRecordId;    // Native FS record (MFT index, inode, etc.)

        std::vector<DataRange> dataRanges;  // Physical data locations on storage

        FileRecord()
            : id(0)
            , size(0)
            , createdTime(0)
            , modifiedTime(0)
            , accessedTime(0)
            , allocated(false)
            , deleted(false)
            , orphaned(false)
            , filesystem(FileSystemType::Unknown)
            , filesystemRecordId(0)
        {}
    };

} // namespace Core
} // namespace Recovery
