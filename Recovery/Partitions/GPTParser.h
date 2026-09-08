#pragma once

#include "IPartitionParser.h"

namespace Recovery {
namespace Partitions {

    /**
     * @brief Parses GPT (GUID Partition Table) partition tables.
     *
     * Reads the primary GPT header at LBA 1 and the partition entry
     * array that follows it.
     *
     * GPT disk layout:
     *   LBA 0   : Protective MBR (parsed by MBRParser)
     *   LBA 1   : GPT Header (92 bytes minimum)
     *   LBA 2+  : Partition Entry Array
     *   ...
     *   LBA N-1 : Backup GPT Header (not read in V1)
     *
     * GPT Header (at LBA 1):
     *   Offset 0  (8 bytes) : Signature "EFI PART"
     *   Offset 8  (4 bytes) : Revision
     *   Offset 12 (4 bytes) : Header size
     *   Offset 16 (4 bytes) : Header CRC32
     *   Offset 20 (4 bytes) : Reserved
     *   Offset 24 (8 bytes) : Current LBA
     *   Offset 32 (8 bytes) : Backup LBA
     *   Offset 40 (8 bytes) : First usable LBA
     *   Offset 48 (8 bytes) : Last usable LBA
     *   Offset 56 (16 bytes): Disk GUID
     *   Offset 72 (8 bytes) : Partition entry array starting LBA
     *   Offset 80 (4 bytes) : Number of partition entries
     *   Offset 84 (4 bytes) : Size of each partition entry
     *   Offset 88 (4 bytes) : Partition entry array CRC32
     *
     * Each partition entry (typically 128 bytes):
     *   Offset 0  (16 bytes): Partition type GUID
     *   Offset 16 (16 bytes): Unique partition GUID
     *   Offset 32 (8 bytes) : First LBA
     *   Offset 40 (8 bytes) : Last LBA
     *   Offset 48 (8 bytes) : Attribute flags
     *   Offset 56 (72 bytes): Partition name (UTF-16LE, null-terminated)
     *
     * Notes:
     *   - Entries with an all-zero type GUID are empty and skipped.
     *   - UTF-16LE names are converted to UTF-8 (BMP range only).
     *   - CRC32 validation is not performed in V1.
     *   - Only the primary GPT header is read (no backup header fallback).
     */
    class GPTParser : public IPartitionParser {
    private:
        // Returns a human-readable description for a well-known GPT type GUID
        static std::string GetTypeDescription(const uint8_t guid[16]);

        // Converts UTF-16LE bytes to a UTF-8 std::string
        static std::string UTF16LEToUTF8(const uint8_t* data, uint32_t byteCount);

        // Checks if a 16-byte GUID is all zeroes
        static bool IsZeroGUID(const uint8_t guid[16]);

    public:
        GPTParser() = default;
        ~GPTParser() override = default;

        bool CanParse(Core::ByteReader& reader) override;

        std::vector<Core::PartitionInfo>
        Parse(Core::ByteReader& reader, uint32_t sectorSize) override;
    };

} // namespace Partitions
} // namespace Recovery
