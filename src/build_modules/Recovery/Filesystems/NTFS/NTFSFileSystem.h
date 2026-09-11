#pragma once

#include "../IRecoveryFileSystem.h"
#include "NTFSStructures.h"
#include "MFTParser.h"
#include "../../Core/ByteReader.h"
#include "../../Core/StorageRegion.h"
#include "../../Core/FileRecord.h"

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace Recovery {
namespace Filesystems {
namespace NTFS {

    /**
     * @brief Read-only NTFS filesystem adapter for recovery.
     *
     * Implements IRecoveryFileSystem by:
     *   1. Parsing the NTFS boot sector to locate the MFT
     *   2. Walking MFT entries to discover all files and directories
     *   3. Reconstructing directory hierarchy paths
     *   4. Providing file metadata and data-range access
     *
     * All operations are strictly read-only.
     */
    class NTFSFileSystem : public IRecoveryFileSystem {
    public:
        /**
         * @param reader    ByteReader wrapping the storage source
         * @param partition StorageRegion describing the NTFS partition's byte range
         */
        NTFSFileSystem(Core::ByteReader& reader,
                       const Core::StorageRegion& partition);

        ~NTFSFileSystem() override = default;

        // ---- IRecoveryFileSystem interface ----

        bool Mount() override;
        Core::FileSystemType GetType() const override;
        bool EnumerateFiles(std::vector<Core::FileRecord>& results) override;
        bool GetFileMetadata(uint64_t fileId, Core::FileRecord& metadata) override;
        bool GetDataRanges(uint64_t fileId, std::vector<Core::DataRange>& ranges) override;
        bool ReadFile(uint64_t fileId, std::vector<uint8_t>& output) override;

    private:
        Core::ByteReader& m_reader;
        Core::StorageRegion m_partition;
        bool m_mounted;

        // Boot sector parameters
        uint32_t m_bytesPerSector;
        uint32_t m_sectorsPerCluster;
        uint32_t m_bytesPerCluster;
        uint32_t m_bytesPerMftRecord;
        uint64_t m_mftLCN;
        uint64_t m_mftStartOffset;  // Absolute byte offset

        // Parsed data
        std::vector<MFTFileInfo>  m_mftEntries;
        std::vector<Core::FileRecord> m_fileRecords;

        // Map from MFT index → position in m_mftEntries
        std::unordered_map<uint64_t, size_t> m_mftIndexMap;

        // Map from fileRecord.id → position in m_fileRecords
        std::unordered_map<uint64_t, size_t> m_fileIdMap;

        /**
         * @brief Reconstructs the full path for a file by walking parent references.
         */
        std::string BuildPath(uint64_t mftIndex);

        /**
         * @brief Converts parsed MFT entries into FileRecord objects.
         */
        void BuildFileRecords();

        /**
         * @brief Reads data from the given data ranges into the output buffer.
         */
        bool ReadDataRanges(const std::vector<Core::DataRange>& ranges,
                            uint64_t expectedSize,
                            std::vector<uint8_t>& output);
    };

} // namespace NTFS
} // namespace Filesystems
} // namespace Recovery
