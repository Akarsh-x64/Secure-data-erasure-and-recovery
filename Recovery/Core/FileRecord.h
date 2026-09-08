#pragma once

#include "RecoveryEnums.h"
#include "DataRange.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Recovery {
namespace Core {

    struct FileNameRecord {
        std::string name;
        std::string shortName;
        std::string path;
        uint64_t parentRecordId = 0;
        uint64_t parentSequence = 0;
        uint64_t metadataSequence = 0;
        bool allocated = false;
        bool deleted = false;
    };

    struct FileAttributeRecord {
        uint16_t id = 0;
        uint32_t type = 0;
        uint32_t flags = 0;
        std::string name;
        uint64_t size = 0;
        uint64_t allocatedSize = 0;
        uint64_t initializedSize = 0;
        uint32_t compressionSize = 0;
        uint32_t skipLength = 0;
        bool resident = false;
        bool compressed = false;
        bool sparse = false;
    };

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
        uint64_t changeTime = 0;
        uint64_t deletionTime = 0;
        uint32_t createdTimeNanos = 0;
        uint32_t modifiedTimeNanos = 0;
        uint32_t accessedTimeNanos = 0;
        uint32_t changeTimeNanos = 0;

        uint64_t sequenceNumber = 0;
        uint32_t metadataFlags = 0;
        uint32_t metadataType = 0;
        uint64_t uid = 0;
        uint64_t gid = 0;
        uint64_t linkCount = 0;
        bool isDirectory = false;
        bool isCompressed = false;
        bool isSparse = false;
        std::string symbolicLinkTarget;
        uint32_t nameFlags = 0;
        uint64_t parentRecordId = 0;
        uint64_t parentSequence = 0;
        std::vector<FileNameRecord> names;
        std::vector<FileAttributeRecord> attributes;

        bool allocated;                 // Still allocated in the filesystem
        bool deleted;                   // Marked as deleted
        bool orphaned;                  // No parent directory reference
        DataRangeStatus dataRangeStatus; // Whether physical data ranges are known

        FileSystemType filesystem;      // Source filesystem type

        uint64_t filesystemRecordId;    // Native FS record (MFT index, inode, etc.)

        std::vector<DataRange> dataRanges;  // Physical data locations on storage
        RecoveryBackend recoveryBackend;
        std::string sourcePath;
        std::optional<uint32_t> partitionIndex;
        uint64_t partitionOffset = 0;
        uint64_t partitionSize = 0;

        FileRecord()
            : id(0)
            , size(0)
            , createdTime(0)
            , modifiedTime(0)
            , accessedTime(0)
            , allocated(false)
            , deleted(false)
            , orphaned(false)
            , dataRangeStatus(DataRangeStatus::Unknown)
            , filesystem(FileSystemType::Unknown)
            , filesystemRecordId(0)
            , recoveryBackend(RecoveryBackend::Unknown)
        {}
    };

} // namespace Core
} // namespace Recovery
