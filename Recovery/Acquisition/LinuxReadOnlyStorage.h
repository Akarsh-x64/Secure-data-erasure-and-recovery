#pragma once

#include "../Core/IReadOnlyStorage.h"
#include <string>
#include <cstdint>

namespace Recovery {
namespace Acquisition {

    /**
     * @brief Linux (POSIX) implementation of IReadOnlyStorage.
     *
     * Opens a block device or disk image using POSIX open() with O_RDONLY.
     * No write access is ever requested.
     *
     * Supported paths:
     *   - /dev/sda, /dev/nvme0n1        (whole disk)
     *   - /dev/sda1, /dev/nvme0n1p1     (partition)
     *   - /dev/loop0                     (loop device)
     *   - /path/to/image.dd             (raw disk image file)
     */
    class LinuxReadOnlyStorage : public Core::IReadOnlyStorage {
    private:
        int         m_fd;
        std::string m_devicePath;
        uint32_t    m_sectorSize;
        uint64_t    m_totalBytes;

        // Queries the kernel for sector size and device capacity
        bool UpdateGeometry();

    public:
        LinuxReadOnlyStorage();
        ~LinuxReadOnlyStorage() override;

        // Non-copyable
        LinuxReadOnlyStorage(const LinuxReadOnlyStorage&) = delete;
        LinuxReadOnlyStorage& operator=(const LinuxReadOnlyStorage&) = delete;

        bool Open(const std::string& path) override;
        void Close() override;

        bool Read(uint64_t offset, uint32_t size, void* buffer) override;

        uint64_t GetSize() const override;
        uint32_t GetSectorSize() const override;
    };

} // namespace Acquisition
} // namespace Recovery
