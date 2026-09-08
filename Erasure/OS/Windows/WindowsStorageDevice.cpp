#include "WindowsStorageDevice.h"
#include <iostream>
#include <cstring>

namespace Erasure {
namespace OS {

WindowsStorageDevice::WindowsStorageDevice() : m_hDevice(INVALID_HANDLE_VALUE) {
  m_geometry = {0, 0, ""};
}

WindowsStorageDevice::~WindowsStorageDevice() { Close(); }

bool WindowsStorageDevice::Open(const std::string &devicePath) {
  // If we already have a handle open, close it first
  if (m_hDevice != INVALID_HANDLE_VALUE) {
    Close();
  }

  m_devicePath = devicePath;

  // Open handle to physical drive or volume with Read/Write access
  // We use CreateFileA to talk to the Windows Kernel.
  m_hDevice = CreateFileA(
      devicePath
          .c_str(), // 1. The path to the drive (e.g., "\\.\PhysicalDrive0")
      GENERIC_READ |
          GENERIC_WRITE, // 2. Request both Read and Write permissions
      FILE_SHARE_READ |
          FILE_SHARE_WRITE, // 3. Allow sharing so Windows doesn't block us if
                            // the drive is in use. We lock it later.
      nullptr,              // 4. Security attributes (Null means ignore)
      OPEN_EXISTING,        // 5. Must be OPEN_EXISTING because we are targeting
                            // physical hardware, not creating a new text file
      FILE_ATTRIBUTE_NORMAL, // 6. Standard file attributes, no special caching
                             // flags needed
      nullptr // 7. Template file (Null because not used for hardware)
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
    FlushFileBuffers(m_hDevice);
    DismountVolume();
    UnlockVolume();
    CloseHandle(m_hDevice);
    m_hDevice = INVALID_HANDLE_VALUE;
  }
}

bool WindowsStorageDevice::UpdateGeometry() {
  DISK_GEOMETRY_EX diskGeometryEx;
  std::memset(&diskGeometryEx, 0, sizeof(diskGeometryEx));
  DWORD bytesReturned = 0;

  // 1. Try IOCTL_DISK_GET_DRIVE_GEOMETRY_EX (standard for physical drives)
  bool success = DeviceIoControl(
      m_hDevice,
      IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
      nullptr, 0,
      &diskGeometryEx,
      sizeof(diskGeometryEx),
      &bytesReturned,
      nullptr
  );

  if (success && diskGeometryEx.Geometry.BytesPerSector > 0) {
    m_geometry.bytesPerSector = diskGeometryEx.Geometry.BytesPerSector;
    m_geometry.totalSectors =
        diskGeometryEx.DiskSize.QuadPart / diskGeometryEx.Geometry.BytesPerSector;
    m_geometry.devicePath = m_devicePath;
    return true;
  }

  // 2. Fallback: Legacy IOCTL_DISK_GET_DRIVE_GEOMETRY
  DISK_GEOMETRY diskGeometry;
  std::memset(&diskGeometry, 0, sizeof(diskGeometry));
  bytesReturned = 0;
  if (DeviceIoControl(m_hDevice, IOCTL_DISK_GET_DRIVE_GEOMETRY, nullptr, 0,
                      &diskGeometry, sizeof(diskGeometry), &bytesReturned, nullptr) &&
      diskGeometry.BytesPerSector > 0) {
    m_geometry.bytesPerSector = diskGeometry.BytesPerSector;

    // Query exact partition/volume length if available
    GET_LENGTH_INFORMATION lengthInfo;
    std::memset(&lengthInfo, 0, sizeof(lengthInfo));
    bytesReturned = 0;
    if (DeviceIoControl(m_hDevice, IOCTL_DISK_GET_LENGTH_INFO, nullptr, 0,
                        &lengthInfo, sizeof(lengthInfo), &bytesReturned, nullptr) &&
        lengthInfo.Length.QuadPart > 0) {
      m_geometry.totalSectors = lengthInfo.Length.QuadPart / m_geometry.bytesPerSector;
    } else {
      m_geometry.totalSectors = diskGeometry.Cylinders.QuadPart *
                                diskGeometry.TracksPerCylinder *
                                diskGeometry.SectorsPerTrack;
    }
    m_geometry.devicePath = m_devicePath;
    return true;
  }

  // 3. Fallback: Direct IOCTL_DISK_GET_LENGTH_INFO (common for mounted volume handles like \\.\E:)
  GET_LENGTH_INFORMATION lengthInfo;
  std::memset(&lengthInfo, 0, sizeof(lengthInfo));
  bytesReturned = 0;
  if (DeviceIoControl(m_hDevice, IOCTL_DISK_GET_LENGTH_INFO, nullptr, 0,
                      &lengthInfo, sizeof(lengthInfo), &bytesReturned, nullptr) &&
      lengthInfo.Length.QuadPart > 0) {
    m_geometry.bytesPerSector = 512; // Default standard sector size
    m_geometry.totalSectors = lengthInfo.Length.QuadPart / 512;
    m_geometry.devicePath = m_devicePath;
    return true;
  }

  return false;
}

bool WindowsStorageDevice::ReadSectors(uint64_t startSector,
                                       uint32_t sectorCount, void *buffer) {
  if (m_hDevice == INVALID_HANDLE_VALUE || buffer == nullptr || m_geometry.bytesPerSector == 0)
    return false;

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

bool WindowsStorageDevice::WriteSectors(uint64_t startSector,
                                        uint32_t sectorCount,
                                        const void *buffer) {
  if (m_hDevice == INVALID_HANDLE_VALUE || buffer == nullptr || m_geometry.bytesPerSector == 0)
    return false;

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
  if (m_hDevice == INVALID_HANDLE_VALUE)
    return false;
  DWORD bytesReturned = 0;
  return DeviceIoControl(m_hDevice, FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0,
                         &bytesReturned, nullptr);
}

bool WindowsStorageDevice::UnlockVolume() {
  if (m_hDevice == INVALID_HANDLE_VALUE)
    return false;
  DWORD bytesReturned = 0;
  return DeviceIoControl(m_hDevice, FSCTL_UNLOCK_VOLUME, nullptr, 0, nullptr, 0,
                         &bytesReturned, nullptr);
}

bool WindowsStorageDevice::DismountVolume() {
  if (m_hDevice == INVALID_HANDLE_VALUE)
    return false;
  DWORD bytesReturned = 0;
  return DeviceIoControl(m_hDevice, FSCTL_DISMOUNT_VOLUME, nullptr, 0, nullptr,
                         0, &bytesReturned, nullptr);
}

bool WindowsStorageDevice::SendDeviceCommand(uint32_t controlCode,
                                             void *inBuffer, uint32_t inSize,
                                             void *outBuffer,
                                             uint32_t outSize) {
  if (m_hDevice == INVALID_HANDLE_VALUE)
    return false;
  DWORD bytesReturned = 0;
  return DeviceIoControl(m_hDevice, controlCode, inBuffer, inSize, outBuffer,
                         outSize, &bytesReturned, nullptr);
}

Core::DeviceGeometry WindowsStorageDevice::GetGeometry() const {
  return m_geometry;
}

} // namespace OS
} // namespace Erasure
