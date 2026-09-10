#include "WindowsReadOnlyStorage.h"
#include <winioctl.h>
#include <iostream>
#include <cstring>

namespace Recovery {
namespace Acquisition {

WindowsReadOnlyStorage::WindowsReadOnlyStorage()
    : m_hDevice(INVALID_HANDLE_VALUE), m_sectorSize(0), m_totalBytes(0) {}

WindowsReadOnlyStorage::~WindowsReadOnlyStorage() {
    Close();
}

bool WindowsReadOnlyStorage::Open(const std::string& path) {
    // If we already have a handle open, close it first
    if (m_hDevice != INVALID_HANDLE_VALUE) {
        Close();
    }

    m_devicePath = path;

    // Open with GENERIC_READ ONLY — no write access is ever requested.
    // This is the fundamental safety guarantee of the Recovery module:
    // even if a bug tries to call WriteFile through this handle,
    // Windows will reject the write at the kernel level.
    m_hDevice = CreateFileA(
        path.c_str(),
        GENERIC_READ,                           // READ ONLY — no GENERIC_WRITE
        FILE_SHARE_READ | FILE_SHARE_WRITE,     // Allow sharing
        nullptr,                                // Default security
        OPEN_EXISTING,                          // Device must already exist
        FILE_ATTRIBUTE_NORMAL,                  // No special flags
        nullptr                                 // No template
    );

    if (m_hDevice == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        std::cerr << "[Recovery] Failed to open '" << path
                  << "' (error " << err << "). "
                  << "Ensure Administrator privileges and correct path.\n";
        return false;
    }

    if (!UpdateGeometry()) {
        std::cerr << "[Recovery] Failed to query device geometry.\n";
        Close();
        return false;
    }

    return true;
}

void WindowsReadOnlyStorage::Close() {
    if (m_hDevice != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hDevice);
        m_hDevice = INVALID_HANDLE_VALUE;
    }
    // Note: no UnlockVolume here — we never lock it
}

bool WindowsReadOnlyStorage::UpdateGeometry() {
    DISK_GEOMETRY_EX diskGeometry;
    std::memset(&diskGeometry, 0, sizeof(diskGeometry));
    DWORD bytesReturned = 0;

    // Same IOCTL used by the Erasure module's WindowsStorageDevice.
    // This is a read-only query — it does not modify the device.
    bool success = DeviceIoControl(
        m_hDevice,
        IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
        nullptr, 0,
        &diskGeometry, sizeof(diskGeometry),
        &bytesReturned,
        nullptr
    );

    if (success) {
        m_sectorSize = diskGeometry.Geometry.BytesPerSector;
        m_totalBytes = diskGeometry.DiskSize.QuadPart;
        return true;
    }

    // Fallback: try GET_LENGTH_INFO for volumes that don't support GEOMETRY_EX
    // (e.g. some virtual volumes or partitions)
    GET_LENGTH_INFORMATION lengthInfo;
    std::memset(&lengthInfo, 0, sizeof(lengthInfo));
    success = DeviceIoControl(
        m_hDevice,
        IOCTL_DISK_GET_LENGTH_INFO,
        nullptr, 0,
        &lengthInfo, sizeof(lengthInfo),
        &bytesReturned,
        nullptr
    );

    if (success) {
        m_totalBytes = lengthInfo.Length.QuadPart;
        // For the sector size fallback, try the simpler DISK_GEOMETRY
        DISK_GEOMETRY simpleGeo;
        std::memset(&simpleGeo, 0, sizeof(simpleGeo));
        if (DeviceIoControl(m_hDevice, IOCTL_DISK_GET_DRIVE_GEOMETRY,
                            nullptr, 0, &simpleGeo, sizeof(simpleGeo),
                            &bytesReturned, nullptr)) {
            m_sectorSize = simpleGeo.BytesPerSector;
        } else {
            m_sectorSize = 512; // Safe default for most hardware
        }
        return true;
    }

    return false;
}

bool WindowsReadOnlyStorage::Read(uint64_t offset, uint32_t size, void* buffer) {
    if (m_hDevice == INVALID_HANDLE_VALUE || buffer == nullptr || size == 0)
        return false;

    // Seek to the requested byte offset
    LARGE_INTEGER liOffset;
    liOffset.QuadPart = static_cast<LONGLONG>(offset);

    if (!SetFilePointerEx(m_hDevice, liOffset, nullptr, FILE_BEGIN)) {
        return false;
    }

    // Read the requested bytes
    DWORD bytesRead = 0;
    if (!ReadFile(m_hDevice, buffer, size, &bytesRead, nullptr)) {
        return false;
    }

    return (bytesRead == size);
}

uint64_t WindowsReadOnlyStorage::GetSize() const {
    return m_totalBytes;
}

uint32_t WindowsReadOnlyStorage::GetSectorSize() const {
    return m_sectorSize;
}

} // namespace Acquisition
} // namespace Recovery
