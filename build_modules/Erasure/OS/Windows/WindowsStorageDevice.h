#pragma once

#include "../../Core/IStorageDevice.h"
#include <windows.h>
#include <string>

namespace Erasure {
namespace OS {

class WindowsStorageDevice : public Core::IStorageDevice {
private:
    HANDLE m_hDevice;
    std::string m_devicePath;
    Core::DeviceGeometry m_geometry;

    // Helper to query the OS for sector size and total sectors
    bool UpdateGeometry();

public:
    WindowsStorageDevice();
    ~WindowsStorageDevice() override;

    bool Open(const std::string& devicePath) override;
    void Close() override;

    bool ReadSectors(uint64_t startSector, uint32_t sectorCount, void* buffer) override;
    bool WriteSectors(uint64_t startSector, uint32_t sectorCount, const void* buffer) override;

    bool LockVolume() override;
    bool UnlockVolume() override;
    bool DismountVolume() override;

    bool SendDeviceCommand(uint32_t controlCode, void* inBuffer, uint32_t inSize, void* outBuffer, uint32_t outSize) override;

    Core::DeviceGeometry GetGeometry() const override;
};

} // namespace OS
} // namespace Erasure
