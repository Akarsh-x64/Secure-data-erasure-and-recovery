#pragma once

#include "../../Core/IStorageDevice.h"
#include <string>

namespace Erasure {
namespace OS {

class LinuxStorageDevice : public Core::IStorageDevice {
private:
    int m_fd;
    std::string m_devicePath;
    Core::DeviceGeometry m_geometry;

    // Helper to query the OS for sector size and total sectors
    bool UpdateGeometry();

public:
    LinuxStorageDevice();
    ~LinuxStorageDevice() override;

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
