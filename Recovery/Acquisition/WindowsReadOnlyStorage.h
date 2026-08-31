#pragma once

#include "../Core/IReadOnlyStorage.h"
#include <windows.h>
#include <string>

namespace Recovery {
namespace Acquisition {

    /**
     * @brief Windows implementation of IReadOnlyStorage.
     *
     * Opens a physical drive or mounted volume using Win32 API with
     * GENERIC_READ access ONLY. No write handle is ever created.
     *
     * This reuses the same Win32 pattern as the Erasure module's
     * WindowsStorageDevice (CreateFileA, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
     * SetFilePointerEx + ReadFile) but deliberately omits:
     *   - GENERIC_WRITE in the access mask
     *   - WriteSectors / WriteFile
     *   - LockVolume / UnlockVolume / DismountVolume
     *   - SendDeviceCommand (hardware pass-through)
     *
     * The Read() method accepts a raw byte offset and byte count.
     * Callers MUST provide sector-aligned parameters (or use ByteReader
     * which handles alignment transparently).
     */
    class WindowsReadOnlyStorage : public Core::IReadOnlyStorage {
    private:
        HANDLE      m_hDevice;
        std::string m_devicePath;
        uint32_t    m_sectorSize;
        uint64_t    m_totalBytes;

        // Queries the OS for sector size and total capacity
        bool UpdateGeometry();

    public:
        WindowsReadOnlyStorage();
        ~WindowsReadOnlyStorage() override;

        // Non-copyable
        WindowsReadOnlyStorage(const WindowsReadOnlyStorage&) = delete;
        WindowsReadOnlyStorage& operator=(const WindowsReadOnlyStorage&) = delete;

        bool Open(const std::string& path) override;
        void Close() override;

        bool Read(uint64_t offset, uint32_t size, void* buffer) override;

        uint64_t GetSize() const override;
        uint32_t GetSectorSize() const override;
    };

} // namespace Acquisition
} // namespace Recovery
