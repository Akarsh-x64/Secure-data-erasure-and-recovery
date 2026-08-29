#include "WindowsStorageDevice.h"
#include <iostream>

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
    // Unlock volume just in case it was locked
    UnlockVolume();
    CloseHandle(m_hDevice);
    m_hDevice = INVALID_HANDLE_VALUE;
  }
}

bool WindowsStorageDevice::UpdateGeometry() {
  DISK_GEOMETRY_EX diskGeometry = {0}; // Struct to hold the hardware response
  DWORD bytesReturned = 0; // How many bytes Windows actually gave back to us

  // Send the IOCTL_DISK_GET_DRIVE_GEOMETRY_EX command to the hardware to ask
  // for its sector size
  bool success = DeviceIoControl(
      m_hDevice,                        // The handle to our drive
      IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, // The specific Windows control code to
                                        // ask for geometry
      nullptr, 0, // Input buffer (we aren't sending any data, just asking a
                  // question, so it's null)
      &diskGeometry,
      sizeof(
          diskGeometry), // Output buffer (where Windows will write the answer)
      &bytesReturned,    // Where Windows will write the size of the answer
      nullptr // Overlapped struct for async I/O (we use sync, so null)
  );

  if (success) {
    // Successfully got the geometry! Now we translate it into our custom
    // struct.
    m_geometry.bytesPerSector = diskGeometry.Geometry.BytesPerSector;

    // DiskSize is the total capacity in bytes.
    // We divide by bytesPerSector to calculate exactly how many sectors exist
    // on the drive.
    m_geometry.totalSectors =
        diskGeometry.DiskSize.QuadPart / diskGeometry.Geometry.BytesPerSector;

    m_geometry.devicePath = m_devicePath;
    return true;
  }

  return false;
}

bool WindowsStorageDevice::ReadSectors(uint64_t startSector,
                                       uint32_t sectorCount, void *buffer) {
  if (m_hDevice == INVALID_HANDLE_VALUE || buffer == nullptr)
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
  if (m_hDevice == INVALID_HANDLE_VALUE || buffer == nullptr)
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
