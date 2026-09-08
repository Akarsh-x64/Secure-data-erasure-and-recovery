#pragma once

namespace Recovery {
namespace Core {

    /**
     * @brief Filesystem types detectable by the Recovery module.
     *
     * Used by FilesystemDetector (Phase 3) and future filesystem adapters.
     */
    enum class FileSystemType {
        Unknown,
        NTFS,
        ExFAT,
        FAT32,
        EXT2,
        EXT3,
        EXT4,
        XFS,
        HFSPlus,
        APFS
    };

    /**
     * @brief Classification of data corruption found during recovery analysis.
     *
     * A deleted file is NOT automatically corruption — it is a recovery state.
     */
    enum class CorruptionType {
        None,
        DeletedFile,
        MetadataCorruption,
        DirectoryCorruption,
        AllocationCorruption,
        MissingData,
        Fragmentation,
        InvalidFileStructure,
        FilesystemCorruption,
        Unknown
    };

    /**
     * @brief Recovery method used or attempted, ordered by confidence.
     *
     * Priority: Metadata > Journal > Orphan > Partial > Carving
     */
    enum class RecoveryMethod {
        Metadata,
        Journal,
        Orphan,
        Partial,
        Carving
    };

} // namespace Core
} // namespace Recovery
