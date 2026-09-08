#include "DataRunParser.h"

namespace Recovery {
namespace Filesystems {
namespace NTFS {

int64_t DataRunParser::ReadSignedLE(const uint8_t* data, uint32_t bytes) {
    if (bytes == 0 || bytes > 8) return 0;

    int64_t value = 0;
    for (uint32_t i = 0; i < bytes; ++i) {
        value |= static_cast<int64_t>(data[i]) << (i * 8);
    }

    // Sign-extend: if the high bit of the last byte is set, fill upper bits with 1s
    if (data[bytes - 1] & 0x80) {
        for (uint32_t i = bytes; i < 8; ++i) {
            value |= static_cast<int64_t>(0xFF) << (i * 8);
        }
    }

    return value;
}

uint64_t DataRunParser::ReadUnsignedLE(const uint8_t* data, uint32_t bytes) {
    if (bytes == 0 || bytes > 8) return 0;

    uint64_t value = 0;
    for (uint32_t i = 0; i < bytes; ++i) {
        value |= static_cast<uint64_t>(data[i]) << (i * 8);
    }
    return value;
}

std::vector<Core::DataRange> DataRunParser::Parse(
    const uint8_t* runData,
    uint32_t       runDataLength,
    uint32_t       bytesPerCluster)
{
    std::vector<Core::DataRange> ranges;

    if (!runData || runDataLength == 0 || bytesPerCluster == 0)
        return ranges;

    uint32_t pos = 0;
    int64_t previousLCN = 0;  // Data-run offsets are relative to the previous run

    while (pos < runDataLength) {
        uint8_t header = runData[pos];

        // A header of 0x00 terminates the data-run list
        if (header == 0x00)
            break;

        uint32_t lengthSize = header & 0x0F;        // Low nibble
        uint32_t offsetSize = (header >> 4) & 0x0F;  // High nibble

        pos++;

        // Validate: we need enough bytes remaining
        if (pos + lengthSize + offsetSize > runDataLength)
            break;

        // Sanity: length and offset sizes should be 1-4 bytes each (max)
        if (lengthSize == 0 || lengthSize > 4 || offsetSize > 4)
            break;

        // Read cluster count (unsigned)
        uint64_t clusterCount = ReadUnsignedLE(runData + pos, lengthSize);
        pos += lengthSize;

        if (offsetSize == 0) {
            // Sparse run — no data on disk (hole in file)
            // Include with offset=0 so callers know about the gap
            ranges.emplace_back(0, clusterCount * bytesPerCluster);
        } else {
            // Read cluster offset (signed, relative to previous LCN)
            int64_t clusterOffset = ReadSignedLE(runData + pos, offsetSize);
            pos += offsetSize;

            previousLCN += clusterOffset;

            // Negative absolute LCN would be invalid
            if (previousLCN < 0)
                break;

            uint64_t absoluteByteOffset = static_cast<uint64_t>(previousLCN) * bytesPerCluster;
            uint64_t byteLength = clusterCount * bytesPerCluster;

            ranges.emplace_back(absoluteByteOffset, byteLength);
        }
    }

    return ranges;
}

} // namespace NTFS
} // namespace Filesystems
} // namespace Recovery
