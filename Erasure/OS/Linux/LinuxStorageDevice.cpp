#include "LinuxStorageDevice.h"
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/mount.h>
#include <cstdlib>   // For posix_memalign and free
#include <cstring>   // For std::memcpy

#include <linux/fs.h>

namespace Erasure {
namespace OS {

LinuxStorageDevice::LinuxStorageDevice() : m_fd(-1) {
    m_geometry = {0, 0, ""};
}

LinuxStorageDevice::~LinuxStorageDevice() { 
    Close(); 
}

bool LinuxStorageDevice::Open(const std::string &devicePath) {
    if (m_fd != -1) {
        Close();
    }

    m_devicePath = devicePath;

    // Open handle to physical drive with O_DIRECT (bypass page cache) 
    // and O_SYNC (synchronous physical writes)
    m_fd = open(devicePath.c_str(), O_RDWR | O_DIRECT | O_SYNC);

    if (m_fd < 0) {
        return false;
    }

    // Populate the geometry (sector size, etc.)
    if (!UpdateGeometry()) {
        Close();
        return false;
    }

    return true;
}

void LinuxStorageDevice::Close() {
    if (m_fd != -1) {
        UnlockVolume();
        close(m_fd);
        m_fd = -1;
    }
}

bool LinuxStorageDevice::UpdateGeometry() {
    int logicalSectorSize = 0;
    uint64_t totalBytes = 0;

    // BLKSSZGET gets the logical block size (usually 512 or 4096)
    if (ioctl(m_fd, BLKSSZGET, &logicalSectorSize) < 0) {
        return false;
    }

    // BLKGETSIZE64 gets the total device size in bytes
    if (ioctl(m_fd, BLKGETSIZE64, &totalBytes) < 0) {
        return false;
    }

    m_geometry.bytesPerSector = logicalSectorSize;
    m_geometry.totalSectors = totalBytes / logicalSectorSize;
    m_geometry.devicePath = m_devicePath;

    return true;
}

bool LinuxStorageDevice::ReadSectors(uint64_t startSector, uint32_t sectorCount, void *buffer) {
    if (m_fd < 0 || buffer == nullptr) return false;

    uint64_t offset = startSector * m_geometry.bytesPerSector;
    size_t bytesToRead = sectorCount * m_geometry.bytesPerSector;

    // O_DIRECT requires strictly aligned memory buffers
    void* alignedBuffer = nullptr;
    if (posix_memalign(&alignedBuffer, m_geometry.bytesPerSector, bytesToRead) != 0) {
        return false; // Memory allocation failed
    }

    // pread64 reads directly from metal into the aligned buffer
    ssize_t bytesRead = pread64(m_fd, alignedBuffer, bytesToRead, offset);

    // If successful, copy it back into the parser's unaligned buffer
    if (bytesRead == static_cast<ssize_t>(bytesToRead)) {
        std::memcpy(buffer, alignedBuffer, bytesToRead);
    }

    free(alignedBuffer);
    return (bytesRead == static_cast<ssize_t>(bytesToRead));
}

bool LinuxStorageDevice::WriteSectors(uint64_t startSector, uint32_t sectorCount, const void *buffer) {
    if (m_fd < 0 || buffer == nullptr) return false;

    uint64_t offset = startSector * m_geometry.bytesPerSector;
    size_t bytesToWrite = sectorCount * m_geometry.bytesPerSector;

    // O_DIRECT requires strictly aligned memory buffers
    void* alignedBuffer = nullptr;
    if (posix_memalign(&alignedBuffer, m_geometry.bytesPerSector, bytesToWrite) != 0) {
        return false; 
    }

    // Copy the parser's unaligned data INTO the aligned buffer before writing
    std::memcpy(alignedBuffer, buffer, bytesToWrite);

    // Write directly to metal, bypassing the OS cache
    ssize_t bytesWritten = pwrite64(m_fd, alignedBuffer, bytesToWrite, offset);

    free(alignedBuffer);
    return (bytesWritten == static_cast<ssize_t>(bytesToWrite));
}

bool LinuxStorageDevice::LockVolume() {
    if (m_fd < 0) return false;
    // Attempt to place an exclusive, non-blocking lock on the block device
    return flock(m_fd, LOCK_EX | LOCK_NB) == 0;
}

bool LinuxStorageDevice::UnlockVolume() {
    if (m_fd < 0) return false;
    // Release the exclusive lock
    return flock(m_fd, LOCK_UN) == 0;
}

bool LinuxStorageDevice::DismountVolume() {
    if (m_fd < 0) return false;
    // Flush any lingering kernel block buffers before raw wiping
    return ioctl(m_fd, BLKFLSBUF, 0) == 0;
}

bool LinuxStorageDevice::SendDeviceCommand(uint32_t controlCode, void *inBuffer, uint32_t inSize, void *outBuffer, uint32_t outSize) {
    if (m_fd < 0) return false;
    return ioctl(m_fd, controlCode, inBuffer) == 0;
}

Core::DeviceGeometry LinuxStorageDevice::GetGeometry() const {
    return m_geometry;
}

} // namespace OS
} // namespace Erasure