#pragma once

#include "../../Core/DataRange.h"

#include <cstdint>
#include <vector>

namespace Recovery {
namespace Filesystems {
namespace NTFS {

    /**
     * @brief Decodes NTFS data-run encodings into DataRange arrays.
     *
     * NTFS non-resident attributes store file data locations as a sequence
     * of variable-length "data runs". Each run is encoded as:
     *
     *   Header byte: high nibble = offset field size (0-4 bytes)
     *                low nibble  = length field size (1-4 bytes)
     *
     *   Followed by:
     *     'length_size' bytes — cluster count (unsigned, little-endian)
     *     'offset_size' bytes — cluster offset (signed, little-endian, relative to previous run)
     *
     * A header byte of 0x00 terminates the run list.
     *
     * Sparse runs have offset_size = 0 (no data on disk, hole in file).
     */
    class DataRunParser {
    public:
        /**
         * @brief Parses a data-run byte sequence into absolute byte ranges.
         *
         * @param runData         Pointer to the raw data-run bytes
         * @param runDataLength   Length of the data-run buffer
         * @param bytesPerCluster Bytes per cluster (from boot sector)
         * @return Vector of DataRange with absolute byte offsets and lengths.
         *         Sparse runs (offset=0) are included with offset=0 to preserve
         *         file layout; callers should check for this.
         */
        static std::vector<Core::DataRange> Parse(
            const uint8_t* runData,
            uint32_t       runDataLength,
            uint32_t       bytesPerCluster);

    private:
        /**
         * @brief Reads a signed little-endian integer of variable width.
         */
        static int64_t ReadSignedLE(const uint8_t* data, uint32_t bytes);

        /**
         * @brief Reads an unsigned little-endian integer of variable width.
         */
        static uint64_t ReadUnsignedLE(const uint8_t* data, uint32_t bytes);
    };

} // namespace NTFS
} // namespace Filesystems
} // namespace Recovery
