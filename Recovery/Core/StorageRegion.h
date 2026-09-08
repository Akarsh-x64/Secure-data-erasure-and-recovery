#pragma once

#include <cstdint>

namespace Recovery {
namespace Core {

    /**
     * @brief A generic byte-range on a storage source.
     *
     * This is intentionally minimal. It allows future phases to pass
     * arbitrary byte ranges (whole disk, single partition, unallocated
     * space, or a custom range) to filesystem detection, recovery,
     * and carving layers without knowing anything about MBR/GPT.
     *
     * Example usage:
     *   - An entire disk:       { 0, diskSize }
     *   - A single partition:   { partition.startOffset, partition.sizeBytes }
     *   - Unallocated gap:      { gapStart, gapSize }
     */
    struct StorageRegion {
        uint64_t startOffset;   // Byte offset from start of storage
        uint64_t size;          // Size in bytes

        StorageRegion() : startOffset(0), size(0) {}
        StorageRegion(uint64_t offset, uint64_t sz) : startOffset(offset), size(sz) {}
    };

} // namespace Core
} // namespace Recovery
