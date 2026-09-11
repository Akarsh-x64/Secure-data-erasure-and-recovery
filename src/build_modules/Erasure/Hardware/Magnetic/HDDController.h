#pragma once

#include "../../Core/IHardwareController.h"
#include "../../Core/IStorageDevice.h"
#include <memory>

namespace Erasure {
namespace Hardware {

/**
 * @brief Handles Secure Erasure for Magnetic HDDs and Virtual Disks.
 * Uses standard WriteSectors to overwrite data (e.g. DoD 3-pass or zero-fill).
 */
class HDDController : public Core::IHardwareController {
private:
    Core::IStorageDevice* m_device;

    // Helper for generating pattern buffers (e.g. 0x00 or 0xFF)
    bool OverwriteWithPattern(uint64_t startSector, uint32_t sectorCount, uint8_t pattern);

    // Helper for generating random noise / gibberish buffer
    bool OverwriteWithRandom(uint64_t startSector, uint32_t sectorCount);

public:
    // Takes ownership of the OS device pipe
    explicit HDDController(Core::IStorageDevice* device);
    ~HDDController() override = default;

    bool SecureEraseSectors(uint64_t startSector, uint32_t sectorCount) override;
    bool SecureEraseDrive() override;

    bool ReadSectors(uint64_t startSector, uint32_t sectorCount, void* buffer) override;
    bool WriteSectors(uint64_t startSector, uint32_t sectorCount, const void* buffer) override;
    
    Core::DeviceGeometry GetGeometry() const override;
};

} // namespace Hardware
} // namespace Erasure
