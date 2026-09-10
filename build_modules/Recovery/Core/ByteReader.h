#pragma once

#include "IReadOnlyStorage.h"
#include <vector>
#include <cstring>
#include <algorithm>

namespace Recovery {
namespace Core {

    /**
     * @brief Convenience wrapper over IReadOnlyStorage for arbitrary byte-offset reads.
     *
     * Raw disk I/O on Windows requires sector-aligned reads. This class
     * accepts any (offset, size) pair and internally aligns the read to
     * sector boundaries, then copies only the requested bytes into the
     * caller's buffer.
     *
     * This is NON-OWNING: it holds a pointer to the IReadOnlyStorage
     * and does not manage its lifetime.
     */
    class ByteReader {
    private:
        IReadOnlyStorage* m_storage;

    public:
        explicit ByteReader(IReadOnlyStorage* storage) : m_storage(storage) {}
        ~ByteReader() = default;

        // Non-copyable, movable
        ByteReader(const ByteReader&) = delete;
        ByteReader& operator=(const ByteReader&) = delete;
        ByteReader(ByteReader&&) = default;
        ByteReader& operator=(ByteReader&&) = default;

        /**
         * @brief Reads arbitrary bytes from the underlying storage.
         *
         * Handles sector alignment transparently:
         *  - Rounds 'offset' down to the nearest sector boundary
         *  - Rounds the end (offset + size) up to the next sector boundary
         *  - Reads the aligned range into a temporary buffer
         *  - Copies only the requested sub-range into 'buffer'
         *
         * @param offset  Byte offset from the start of the device (need not be sector-aligned)
         * @param size    Number of bytes to read
         * @param buffer  Destination buffer (must be at least 'size' bytes)
         * @return true if the read succeeded
         */
        bool ReadBytes(uint64_t offset, uint32_t size, void* buffer) const {
            if (!m_storage || !buffer || size == 0) return false;

            uint32_t sectorSize = m_storage->GetSectorSize();
            if (sectorSize == 0) return false;

            // Calculate aligned boundaries
            uint64_t alignedStart = (offset / sectorSize) * sectorSize;
            uint64_t alignedEnd   = ((offset + size + sectorSize - 1) / sectorSize) * sectorSize;
            uint32_t alignedSize  = static_cast<uint32_t>(alignedEnd - alignedStart);

            // If the request is already aligned, read directly
            if (alignedStart == offset && alignedSize == size) {
                return m_storage->Read(offset, size, buffer);
            }

            // Otherwise, read into a temporary aligned buffer and copy out the requested bytes
            std::vector<uint8_t> alignedBuffer(alignedSize);
            if (!m_storage->Read(alignedStart, alignedSize, alignedBuffer.data())) {
                return false;
            }

            uint32_t internalOffset = static_cast<uint32_t>(offset - alignedStart);
            std::memcpy(buffer, alignedBuffer.data() + internalOffset, size);
            return true;
        }

        /**
         * @brief Convenience: reads bytes into a vector.
         */
        bool ReadBytes(uint64_t offset, uint32_t size, std::vector<uint8_t>& out) const {
            out.resize(size);
            if (!ReadBytes(offset, size, out.data())) {
                out.clear();
                return false;
            }
            return true;
        }

        /**
         * @brief Reads a single struct/POD from an arbitrary byte offset.
         *
         * Usage: ExFatBootSector vbr; reader.ReadStruct(0, vbr);
         */
        template<typename T>
        bool ReadStruct(uint64_t offset, T& out) const {
            return ReadBytes(offset, static_cast<uint32_t>(sizeof(T)), &out);
        }

        // Accessors forwarded from the underlying storage
        uint64_t GetSize() const { return m_storage ? m_storage->GetSize() : 0; }
        uint32_t GetSectorSize() const { return m_storage ? m_storage->GetSectorSize() : 0; }
    };

} // namespace Core
} // namespace Recovery
