#include "HDDController.h"
#include <vector>
#include <cstring>

namespace Erasure {
namespace Hardware {

HDDController::HDDController(Core::IStorageDevice* device) : m_device(device) {
}

bool HDDController::OverwriteWithPattern(uint64_t startSector, uint32_t sectorCount, uint8_t pattern) {
    if (!m_device || sectorCount == 0) return false;

    uint32_t sectorSize = m_device->GetGeometry().bytesPerSector;
    if (sectorSize == 0) return false;

    // Allocate bounded buffer (1024 sectors = 512KB for standard 512B sectors)
    const uint32_t MAX_CHUNK = 1024;
    uint32_t bufferSectors = (sectorCount < MAX_CHUNK) ? sectorCount : MAX_CHUNK;
    std::vector<uint8_t> buffer(static_cast<size_t>(bufferSectors) * sectorSize, pattern);

    uint64_t currentSector = startSector;
    uint32_t remainingSectors = sectorCount;

    while (remainingSectors > 0) {
        uint32_t sectorsToWrite = (remainingSectors > bufferSectors) ? bufferSectors : remainingSectors;
        if (!m_device->WriteSectors(currentSector, sectorsToWrite, buffer.data())) {
            return false;
        }
        currentSector += sectorsToWrite;
        remainingSectors -= sectorsToWrite;
    }

    return true;
}

bool HDDController::SecureEraseSectors(uint64_t startSector, uint32_t sectorCount) {
    // For HDDs and Virtual Disks, a secure erase is a standard overwrite.
    // For extreme security, we could do 3 passes (e.g. 0x00, 0xFF, random).
    // For now, we do a single pass of Zeros for speed and basic security.
    return OverwriteWithPattern(startSector, sectorCount, 0x00);
}

bool HDDController::SecureEraseDrive() {
    if (!m_device) return false;

    Core::DeviceGeometry geo = m_device->GetGeometry();
    if (geo.totalSectors == 0) return false;

    // Lock and dismount to ensure the OS doesn't interfere
    m_device->LockVolume();
    m_device->DismountVolume();

    // Overwrite the entire drive in chunks (e.g. 1024 sectors at a time to save RAM)
    const uint32_t CHUNK_SIZE = 1024;
    uint64_t remainingSectors = geo.totalSectors;
    uint64_t currentSector = 0;

    while (remainingSectors > 0) {
        uint32_t sectorsToWrite = (remainingSectors > CHUNK_SIZE) ? CHUNK_SIZE : static_cast<uint32_t>(remainingSectors);
        
        if (!OverwriteWithPattern(currentSector, sectorsToWrite, 0x00)) {
            return false; // Failed during wipe
        }

        currentSector += sectorsToWrite;
        remainingSectors -= sectorsToWrite;
    }

    return true;
}

bool HDDController::ReadSectors(uint64_t startSector, uint32_t sectorCount, void* buffer) {
    if (!m_device) return false;
    return m_device->ReadSectors(startSector, sectorCount, buffer);
}

bool HDDController::WriteSectors(uint64_t startSector, uint32_t sectorCount, const void* buffer) {
    if (!m_device) return false;
    return m_device->WriteSectors(startSector, sectorCount, buffer);
}

Core::DeviceGeometry HDDController::GetGeometry() const {
    if (!m_device) return {0, 0, ""};
    return m_device->GetGeometry();
}

} // namespace Hardware
} // namespace Erasure
