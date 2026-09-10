#pragma once

#include "NTFSStructures.h"
#include "../../Core/ByteReader.h"
#include "../../Core/DataRange.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Recovery {
namespace Filesystems {
namespace NTFS {

    /**
     * @brief Parsed information from a single MFT FILE record.
     */
    struct MFTFileInfo {
        uint64_t    mftIndex;           // MFT record number
        uint64_t    parentMftIndex;     // Parent directory MFT record number
        std::string filename;           // Extracted filename (UTF-8)
        uint64_t    fileSize;           // Logical file size
        uint64_t    createdTime;        // Unix epoch seconds
        uint64_t    modifiedTime;       // Unix epoch seconds
        uint64_t    accessedTime;       // Unix epoch seconds
        bool        inUse;              // FILE record IN_USE flag
        bool        isDirectory;        // FILE record IS_DIRECTORY flag
        bool        hasResidentData;    // $DATA is resident (inline)
        std::vector<uint8_t>          residentData;  // Inline data (if resident)
        std::vector<Core::DataRange>  dataRanges;    // Non-resident data runs

        MFTFileInfo()
            : mftIndex(0)
            , parentMftIndex(0)
            , fileSize(0)
            , createdTime(0)
            , modifiedTime(0)
            , accessedTime(0)
            , inUse(false)
            , isDirectory(false)
            , hasResidentData(false)
        {}
    };

    /**
     * @brief Read-only MFT entry parser.
     *
     * Parses individual MFT FILE records from raw disk bytes. Handles:
     *   - Fixup array validation and application
     *   - $FILE_NAME attribute extraction (Win32 preferred over DOS)
     *   - Resident $DATA extraction
     *   - Non-resident $DATA data-run decoding
     *   - Allocated/deleted state from flags
     *
     * Does NOT modify the source storage.
     */
    class MFTParser {
    public:
        /**
         * @param reader           ByteReader for disk access
         * @param partitionOffset  Absolute byte offset of the partition start
         * @param bytesPerMftRecord Size of each MFT record in bytes
         * @param bytesPerCluster  Bytes per cluster (from boot sector)
         * @param bytesPerSector   Bytes per sector (from boot sector)
         */
        MFTParser(Core::ByteReader& reader,
                  uint64_t partitionOffset,
                  uint32_t bytesPerMftRecord,
                  uint32_t bytesPerCluster,
                  uint32_t bytesPerSector);

        /**
         * @brief Parses a single MFT entry at the given absolute byte offset.
         *
         * @param mftByteOffset  Absolute byte offset of the FILE record
         * @param mftIndex       MFT record number (for populating info)
         * @param info           Output: parsed file info
         * @return true if the record was valid and parsed successfully
         */
        bool ParseEntry(uint64_t mftByteOffset, uint64_t mftIndex, MFTFileInfo& info);

    private:
        Core::ByteReader& m_reader;
        uint64_t m_partitionOffset;
        uint32_t m_bytesPerMftRecord;
        uint32_t m_bytesPerCluster;
        uint32_t m_bytesPerSector;

        /**
         * @brief Validates and applies the fixup array to a raw MFT record.
         *
         * NTFS writes a signature value at the end of each sector within
         * a multi-sector structure. The fixup array contains the original
         * bytes that were replaced. This method:
         *   1. Validates that each sector's last 2 bytes match the fixup signature
         *   2. Replaces them with the original values from the fixup array
         */
        bool ApplyFixups(uint8_t* record, uint32_t recordSize);

        /**
         * @brief Walks the attribute chain within a parsed MFT record.
         */
        bool ParseAttributes(const uint8_t* record, uint32_t recordSize,
                             uint32_t firstAttrOffset, MFTFileInfo& info);

        /**
         * @brief Converts a UTF-16LE string to UTF-8.
         */
        static std::string Utf16ToUtf8(const uint8_t* utf16Data, uint32_t charCount);
    };

} // namespace NTFS
} // namespace Filesystems
} // namespace Recovery
