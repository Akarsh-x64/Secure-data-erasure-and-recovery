#pragma once

#include <cstdint>
#include <string>

namespace Recovery {
namespace Core {

    /**
     * @brief Read-only storage interface for the Recovery module.
     *
     * This is the Recovery module's equivalent of the Erasure module's
     * IStorageDevice, but deliberately exposes NO write, lock, dismount,
     * or hardware pass-through operations.
     *
     * Any class implementing this interface provides safe, read-only
     * access to a storage device or image.
     */
    class IReadOnlyStorage {
    public:
        virtual ~IReadOnlyStorage() = default;

        // Opens a handle to the device for read-only access
        // e.g. "\\\\.\\PhysicalDrive0" or "\\\\.\\E:"
        virtual bool Open(const std::string& path) = 0;

        // Closes the handle. Must be safe to call multiple times.
        virtual void Close() = 0;

        // Reads 'size' bytes starting at byte offset 'offset' into 'buffer'.
        // The implementation handles sector alignment internally.
        // The caller does NOT need to align offset or size.
        // Returns true if the requested bytes were read successfully.
        virtual bool Read(uint64_t offset, uint32_t size, void* buffer) = 0;

        // Returns the total size of the storage device in bytes.
        virtual uint64_t GetSize() const = 0;

        // Returns the physical sector size in bytes.
        virtual uint32_t GetSectorSize() const = 0;
    };

} // namespace Core
} // namespace Recovery
