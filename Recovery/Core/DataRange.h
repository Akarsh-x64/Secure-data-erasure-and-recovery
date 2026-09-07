#pragma once

#include <cstdint>

namespace Recovery {
namespace Core {

    /**
     * @brief A physical byte range on the source storage.
     *
     * Represents a contiguous run of file data at a specific disk offset.
     * Used by filesystem adapters to describe where a file's bytes live.
     *
     * Example (fragmented file):
     *   bytes 0-2 MB   → DataRange { offset=1000000,  length=2097152 }
     *   bytes 2-5 MB   → DataRange { offset=9000000,  length=3145728 }
     *   bytes 5-8 MB   → DataRange { offset=20000000, length=3145728 }
     */
    struct DataRange {
        uint64_t offset;    // Byte offset from start of storage
        uint64_t length;    // Length in bytes

        DataRange() : offset(0), length(0) {}
        DataRange(uint64_t off, uint64_t len) : offset(off), length(len) {}
    };

} // namespace Core
} // namespace Recovery
