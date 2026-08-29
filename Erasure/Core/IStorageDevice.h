#pragma once

#include "Types.h"
#include <vector>

namespace Erasure {
namespace Core {

    /**
     * @brief The Operating System layer contract.
     * This acts as the dumb "pipe" to the physical device.
     */
    class IStorageDevice {
    public:
        virtual ~IStorageDevice() = default;

        // Opens a handle to the physical device (e.g., \\.\PhysicalDrive0)
        virtual bool Open(const std::string& devicePath) = 0;
        virtual void Close() = 0;

        // Basic I/O (Must be sector aligned)
        virtual bool ReadSectors(uint64_t startSector, uint32_t sectorCount, void* buffer) = 0;
        virtual bool WriteSectors(uint64_t startSector, uint32_t sectorCount, const void* buffer) = 0;

        // Volume Locking for Raw Writes
        virtual bool LockVolume() = 0;
        virtual bool UnlockVolume() = 0;
        virtual bool DismountVolume() = 0;

        // Hardware Pass-through (for sending NVMe/ATA commands from the Hardware layer)
        // buffer IN/OUT contains the specific OS-wrapped payload structure (e.g. ATA_PASS_THROUGH_EX)
        virtual bool SendDeviceCommand(uint32_t controlCode, void* inBuffer, uint32_t inSize, void* outBuffer, uint32_t outSize) = 0;

        // Geometry info
        virtual DeviceGeometry GetGeometry() const = 0;
    };

} // namespace Core
} // namespace Erasure
