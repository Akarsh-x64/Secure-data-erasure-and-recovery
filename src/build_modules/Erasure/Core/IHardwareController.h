#pragma once

#include "IStorageDevice.h"
#include <cstdint>

namespace Erasure {
namespace Core {

/**
 * @brief The Hardware layer contract.
 * Contains the "Destroying Logic" for specific hardware architectures.
 */
class IHardwareController {
public:
  virtual ~IHardwareController() = default;

  // Securely erases specific sectors (Used by FileSystem for single files)
  // e.g. Constructs a TRIM payload and passes it to
  // IStorageDevice->SendDeviceCommand
  virtual bool SecureEraseSectors(uint64_t startSector,
                                  uint32_t sectorCount) = 0;

  // Completely sanitizes the entire disk (e.g. ATA Secure Erase or NVMe Format)
  virtual bool SecureEraseDrive() = 0;

  // Proxy for standard reads/writes for metadata updates
  virtual bool ReadSectors(uint64_t startSector, uint32_t sectorCount,
                           void *buffer) = 0;
  virtual bool WriteSectors(uint64_t startSector, uint32_t sectorCount,
                            const void *buffer) = 0;

  virtual DeviceGeometry GetGeometry() const = 0;
};

} // namespace Core
} // namespace Erasure
