#include "LinuxReadOnlyStorage.h"

#include <iostream>
#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/fs.h>       // BLKGETSIZE64, BLKSSZGET

namespace Recovery {
namespace Acquisition {

LinuxReadOnlyStorage::LinuxReadOnlyStorage()
    : m_fd(-1), m_sectorSize(0), m_totalBytes(0) {}

LinuxReadOnlyStorage::~LinuxReadOnlyStorage() {
    Close();
}

bool LinuxReadOnlyStorage::Open(const std::string& path) {
    if (m_fd >= 0) {
        Close();
    }

    m_devicePath = path;

    // Open with O_RDONLY — no write access is ever requested.
    // This is the fundamental safety guarantee: even if a bug tries
    // to call write() on this fd, the kernel will reject it with EBADF.
    m_fd = ::open(path.c_str(), O_RDONLY);

    if (m_fd < 0) {
        std::cerr << "[Recovery] Failed to open '" << path
                  << "': " << std::strerror(errno) << "\n";
        return false;
    }

    if (!UpdateGeometry()) {
        std::cerr << "[Recovery] Failed to query device geometry.\n";
        Close();
        return false;
    }

    return true;
}

void LinuxReadOnlyStorage::Close() {
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

bool LinuxReadOnlyStorage::UpdateGeometry() {
    struct stat st;
    if (fstat(m_fd, &st) < 0) {
        return false;
    }

    if (S_ISBLK(st.st_mode)) {
        // Block device (e.g. /dev/sda, /dev/nvme0n1p1)

        // Get total size in bytes
        uint64_t size = 0;
        if (ioctl(m_fd, BLKGETSIZE64, &size) < 0) {
            return false;
        }
        m_totalBytes = size;

        // Get logical sector size
        int sectorSize = 0;
        if (ioctl(m_fd, BLKSSZGET, &sectorSize) < 0) {
            m_sectorSize = 512; // Safe default
        } else {
            m_sectorSize = static_cast<uint32_t>(sectorSize);
        }

    } else if (S_ISREG(st.st_mode)) {
        // Regular file (e.g. a raw disk image)
        m_totalBytes = static_cast<uint64_t>(st.st_size);
        m_sectorSize = 512; // Conventional sector size for images

    } else {
        std::cerr << "[Recovery] '" << m_devicePath
                  << "' is not a block device or regular file.\n";
        return false;
    }

    return true;
}

bool LinuxReadOnlyStorage::Read(uint64_t offset, uint32_t size, void* buffer) {
    if (m_fd < 0 || buffer == nullptr || size == 0)
        return false;

    // pread() reads at a given offset without changing the file position.
    // This is thread-safe and simpler than lseek + read.
    ssize_t bytesRead = ::pread(m_fd, buffer, size, static_cast<off_t>(offset));

    if (bytesRead < 0) {
        std::cerr << "[Recovery] Read error at offset " << offset
                  << ": " << std::strerror(errno) << "\n";
        return false;
    }

    return (static_cast<uint32_t>(bytesRead) == size);
}

uint64_t LinuxReadOnlyStorage::GetSize() const {
    return m_totalBytes;
}

uint32_t LinuxReadOnlyStorage::GetSectorSize() const {
    return m_sectorSize;
}

} // namespace Acquisition
} // namespace Recovery
