#pragma once

#include "IPartitionParser.h"

namespace Recovery {
namespace Partitions {

    /**
     * @brief Parses MBR (Master Boot Record) partition tables.
     *
     * Reads sector 0 (LBA 0) and extracts up to 4 primary partition entries
     * from the partition table at byte offset 0x1BE.
     *
     * MBR layout (sector 0, 512 bytes):
     *   0x000 - 0x1BD : Bootstrap code (446 bytes)
     *   0x1BE - 0x1CD : Partition entry #1 (16 bytes)
     *   0x1CE - 0x1DD : Partition entry #2 (16 bytes)
     *   0x1DE - 0x1ED : Partition entry #3 (16 bytes)
     *   0x1EE - 0x1FD : Partition entry #4 (16 bytes)
     *   0x1FE - 0x1FF : Boot signature (0x55, 0xAA)
     *
     * Each partition entry (16 bytes):
     *   Offset 0  : Boot indicator (0x80 = bootable, 0x00 = inactive)
     *   Offset 1-3: Starting CHS address (ignored — use LBA instead)
     *   Offset 4  : Partition type byte
     *   Offset 5-7: Ending CHS address (ignored)
     *   Offset 8  : Starting LBA (little-endian uint32)
     *   Offset 12 : Number of sectors (little-endian uint32)
     *
     * Notes:
     *   - Type byte 0xEE indicates a GPT protective MBR.
     *     This entry is NOT returned as a normal partition.
     *     Use HasProtectiveMBR() to check after parsing.
     *   - Extended partitions (0x05, 0x0F, 0x85) are reported
     *     but the extended partition chain is NOT recursively walked.
     */
    class MBRParser : public IPartitionParser {
    private:
        bool m_hasProtectiveMBR;

        // Returns a human-readable description for a given MBR type byte
        static std::string GetTypeDescription(uint8_t typeByte);

    public:
        MBRParser();
        ~MBRParser() override = default;

        bool CanParse(Core::ByteReader& reader) override;

        std::vector<Core::PartitionInfo>
        Parse(Core::ByteReader& reader, uint32_t sectorSize) override;

        /**
         * @brief Returns true if the MBR contains a protective MBR entry (0xEE).
         *
         * Call this after Parse() to determine whether the disk uses GPT
         * and should be parsed by GPTParser instead.
         */
        bool HasProtectiveMBR() const { return m_hasProtectiveMBR; }
    };

} // namespace Partitions
} // namespace Recovery
