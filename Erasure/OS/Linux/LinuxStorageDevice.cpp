#include "LinuxStorageDevice.h"
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <sys/file.h>
#include <sys/mount.h>

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

    // Open handle to physical drive (e.g., "/dev/sda" or "/dev/nvme0n1")
    // O_RDWR: Read and Write access
    // Note: O_DIRECT can be added to bypass kernel caches, but it requires 
    // memory buffers to be strictly sector-aligned via posix_memalign().
    m_fd = open(devicePath.c_str(), O_RDWR);

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

    // pread64 reads from a specific offset without changing the global file pointer
    ssize_t bytesRead = pread64(m_fd, buffer, bytesToRead, offset);

    return (bytesRead == static_cast<ssize_t>(bytesToRead));
}

bool LinuxStorageDevice::WriteSectors(uint64_t startSector, uint32_t sectorCount, const void *buffer) {
    if (m_fd < 0 || buffer == nullptr) return false;

    uint64_t offset = startSector * m_geometry.bytesPerSector;
    size_t bytesToWrite = sectorCount * m_geometry.bytesPerSector;

    // pwrite64 writes to a specific offset safely
    ssize_t bytesWritten = pwrite64(m_fd, buffer, bytesToWrite, offset);

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
    
    // In Linux, we flush the block device's kernel buffers before raw wiping.
    // To literally unmount an active filesystem, you would use umount(m_devicePath.c_str()), 
    // but flushing the block buffer is the equivalent safety step for a raw device handle.
    return ioctl(m_fd, BLKFLSBUF, 0) == 0;
}

bool LinuxStorageDevice::SendDeviceCommand(uint32_t controlCode, void *inBuffer, uint32_t inSize, void *outBuffer, uint32_t outSize) {
    if (m_fd < 0) return false;
    
    // Linux ioctl APIs typically pack all arguments (input and output) 
    // into a single struct pointer (e.g., nvme_passthru_cmd or sg_io_hdr_t).
    // The hardware controller layer is expected to pass that struct into `inBuffer`.
    return ioctl(m_fd, controlCode, inBuffer) == 0;
}

Core::DeviceGeometry LinuxStorageDevice::GetGeometry() const {
    return m_geometry;
}

} // namespace OS
} // namespace Erasure
