#pragma once

#include "../Core/PartitionInfo.h"
#include "../Core/ByteReader.h"
#include <vector>

namespace Recovery {
namespace Partitions {

    /**
     * @brief Abstract interface for partition table parsers.
     *
     * Each implementation (MBR, GPT) provides:
     *   - CanParse(): detect whether the storage contains this partition format
     *   - Parse():    extract all valid partition entries
     *
     * Parsers operate through ByteReader (which wraps IReadOnlyStorage)
     * and never open files or devices directly.
     */
    class IPartitionParser {
    public:
        virtual ~IPartitionParser() = default;

        /**
         * @brief Returns true if this parser recognizes the partition table format.
         *
         * Should be a lightweight check (e.g. read a signature).
         * Must not crash on truncated/empty/corrupted images.
         */
        virtual bool CanParse(Core::ByteReader& reader) = 0;

        /**
         * @brief Parses the partition table and returns all valid entries.
         *
         * @param reader      ByteReader wrapping the storage source
         * @param sectorSize  Physical sector size (used to compute byte offsets)
         * @return Vector of discovered partitions (may be empty)
         */
        virtual std::vector<Core::PartitionInfo>
        Parse(Core::ByteReader& reader, uint32_t sectorSize) = 0;
    };

} // namespace Partitions
} // namespace Recovery
