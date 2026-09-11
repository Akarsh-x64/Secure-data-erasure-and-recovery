#include "HDDController.h"
#include <vector>
#include <cstring>
#include <random>
#include <chrono>

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

bool HDDController::OverwriteWithRandom(uint64_t startSector, uint32_t sectorCount) {
    if (!m_device || sectorCount == 0) return false;

    uint32_t sectorSize = m_device->GetGeometry().bytesPerSector;
    if (sectorSize == 0) return false;

    const uint32_t MAX_CHUNK = 1024;
    uint32_t bufferSectors = (sectorCount < MAX_CHUNK) ? sectorCount : MAX_CHUNK;
    std::vector<uint8_t> buffer(static_cast<size_t>(bufferSectors) * sectorSize);

    // High-performance 64-bit Mersenne Twister PRNG for gibberish / noise
    std::mt19937_64 rng(static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));

    uint64_t currentSector = startSector;
    uint32_t remainingSectors = sectorCount;

    while (remainingSectors > 0) {
        uint32_t sectorsToWrite = (remainingSectors > bufferSectors) ? bufferSectors : remainingSectors;
        size_t bytesToWrite = static_cast<size_t>(sectorsToWrite) * sectorSize;

        // Fill buffer with 64-bit random gibberish
        uint64_t* words = reinterpret_cast<uint64_t*>(buffer.data());
        size_t wordCount = bytesToWrite / sizeof(uint64_t);
        for (size_t i = 0; i < wordCount; ++i) {
            words[i] = rng();
        }
        for (size_t i = wordCount * sizeof(uint64_t); i < bytesToWrite; ++i) {
            buffer[i] = static_cast<uint8_t>(rng() & 0xFF);
        }

        if (!m_device->WriteSectors(currentSector, sectorsToWrite, buffer.data())) {
            return false;
        }
        currentSector += sectorsToWrite;
        remainingSectors -= sectorsToWrite;
    }

    return true;
}

bool HDDController::SecureEraseSectors(uint64_t startSector, uint32_t sectorCount) {
    // 3-Pass DoD 5220.22-M Overwrite:
    // Pass 1: Write all zeros (0x00)
    if (!OverwriteWithPattern(startSector, sectorCount, 0x00)) {
        return false;
    }

    // Pass 2: Write all ones (0xFF / binary 11111111)
    if (!OverwriteWithPattern(startSector, sectorCount, 0xFF)) {
        return false;
    }

    // Pass 3: Write pseudorandom noise / gibberish
    if (!OverwriteWithRandom(startSector, sectorCount)) {
        return false;
    }

    return true;
}

bool HDDController::SecureEraseDrive() {
    if (!m_device) return false;

    Core::DeviceGeometry geo = m_device->GetGeometry();
    if (geo.totalSectors == 0) return false;

    // Lock and dismount to ensure the OS doesn't interfere
    m_device->LockVolume();
    m_device->DismountVolume();

    const uint32_t CHUNK_SIZE = 1024;

    // Pass 1: Write all zeros (0x00) across entire drive
    {
        uint64_t remaining = geo.totalSectors;
        uint64_t curr = 0;
        while (remaining > 0) {
            uint32_t count = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : static_cast<uint32_t>(remaining);
            if (!OverwriteWithPattern(curr, count, 0x00)) return false;
            curr += count;
            remaining -= count;
        }
    }

    // Pass 2: Write all ones (0xFF) across entire drive
    {
        uint64_t remaining = geo.totalSectors;
        uint64_t curr = 0;
        while (remaining > 0) {
            uint32_t count = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : static_cast<uint32_t>(remaining);
            if (!OverwriteWithPattern(curr, count, 0xFF)) return false;
            curr += count;
            remaining -= count;
        }
    }

    // Pass 3: Write pseudorandom noise / gibberish across entire drive
    {
        uint64_t remaining = geo.totalSectors;
        uint64_t curr = 0;
        while (remaining > 0) {
            uint32_t count = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : static_cast<uint32_t>(remaining);
            if (!OverwriteWithRandom(curr, count)) return false;
            curr += count;
            remaining -= count;
        }
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
