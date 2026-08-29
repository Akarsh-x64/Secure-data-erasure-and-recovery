#include "WindowsStorageDevice.h"
#include <iostream>

namespace Erasure {
namespace OS {

WindowsStorageDevice::WindowsStorageDevice() : m_hDevice(INVALID_HANDLE_VALUE) {
    m_geometry = {0, 0, ""};
}

WindowsStorageDevice::~WindowsStorageDevice() {
    Close();
}

bool WindowsStorageDevice::Open(const std::string& devicePath) {
    // If we already have a handle open, close it first
    if (m_hDevice != INVALID_HANDLE_VALUE) {
        Close();
    }

    m_devicePath = devicePath;

    // Open handle to physical drive or volume with Read/Write access
    m_hDevice = CreateFileA(
        devicePath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, // Allow sharing so we can open it even if mounted, lock it later
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (m_hDevice == INVALID_HANDLE_VALUE) {
        return false;
    }

    // Populate the geometry (sector size, etc.)
    if (!UpdateGeometry()) {
        Close();
        return false;
    }

    return true;
}

void WindowsStorageDevice::Close() {
    if (m_hDevice != INVALID_HANDLE_VALUE) {
        // Unlock volume just in case it was locked
        UnlockVolume();
        CloseHandle(m_hDevice);
        m_hDevice = INVALID_HANDLE_VALUE;
    }
}

bool WindowsStorageDevice::UpdateGeometry() {
    DISK_GEOMETRY_EX diskGeometry = { 0 };
    DWORD bytesReturned = 0;

    bool success = DeviceIoControl(
        m_hDevice,
        IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
        nullptr, 0,
        &diskGeometry, sizeof(diskGeometry),
        &bytesReturned,
        nullptr
    );

    if (success) {
        m_geometry.bytesPerSector = diskGeometry.Geometry.BytesPerSector;
        // DiskSize is the total bytes. Divide by bytesPerSector to get total sectors.
        m_geometry.totalSectors = diskGeometry.DiskSize.QuadPart / diskGeometry.Geometry.BytesPerSector;
        m_geometry.devicePath = m_devicePath;
        return true;
    }

    return false;
}

bool WindowsStorageDevice::ReadSectors(uint64_t startSector, uint32_t sectorCount, void* buffer) {
    if (m_hDevice == INVALID_HANDLE_VALUE || buffer == nullptr) return false;

    LARGE_INTEGER offset;
    offset.QuadPart = startSector * m_geometry.bytesPerSector;

    if (!SetFilePointerEx(m_hDevice, offset, nullptr, FILE_BEGIN)) {
        return false;
    }

    DWORD bytesToRead = sectorCount * m_geometry.bytesPerSector;
    DWORD bytesRead = 0;

    if (!ReadFile(m_hDevice, buffer, bytesToRead, &bytesRead, nullptr)) {
        return false;
    }

    return (bytesRead == bytesToRead);
}

bool WindowsStorageDevice::WriteSectors(uint64_t startSector, uint32_t sectorCount, const void* buffer) {
    if (m_hDevice == INVALID_HANDLE_VALUE || buffer == nullptr) return false;

    LARGE_INTEGER offset;
    offset.QuadPart = startSector * m_geometry.bytesPerSector;

    if (!SetFilePointerEx(m_hDevice, offset, nullptr, FILE_BEGIN)) {
        return false;
    }

    DWORD bytesToWrite = sectorCount * m_geometry.bytesPerSector;
    DWORD bytesWritten = 0;

    if (!WriteFile(m_hDevice, buffer, bytesToWrite, &bytesWritten, nullptr)) {
        return false;
    }

    return (bytesWritten == bytesToWrite);
}

bool WindowsStorageDevice::LockVolume() {
    if (m_hDevice == INVALID_HANDLE_VALUE) return false;
    DWORD bytesReturned = 0;
    return DeviceIoControl(m_hDevice, FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0, &bytesReturned, nullptr);
}

bool WindowsStorageDevice::UnlockVolume() {
    if (m_hDevice == INVALID_HANDLE_VALUE) return false;
    DWORD bytesReturned = 0;
    return DeviceIoControl(m_hDevice, FSCTL_UNLOCK_VOLUME, nullptr, 0, nullptr, 0, &bytesReturned, nullptr);
}

bool WindowsStorageDevice::DismountVolume() {
    if (m_hDevice == INVALID_HANDLE_VALUE) return false;
    DWORD bytesReturned = 0;
    return DeviceIoControl(m_hDevice, FSCTL_DISMOUNT_VOLUME, nullptr, 0, nullptr, 0, &bytesReturned, nullptr);
}

bool WindowsStorageDevice::SendDeviceCommand(uint32_t controlCode, void* inBuffer, uint32_t inSize, void* outBuffer, uint32_t outSize) {
    if (m_hDevice == INVALID_HANDLE_VALUE) return false;
    DWORD bytesReturned = 0;
    return DeviceIoControl(m_hDevice, controlCode, inBuffer, inSize, outBuffer, outSize, &bytesReturned, nullptr);
}

Core::DeviceGeometry WindowsStorageDevice::GetGeometry() const {
    return m_geometry;
}

} // namespace OS
} // namespace Erasure
