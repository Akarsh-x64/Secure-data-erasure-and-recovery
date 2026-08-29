#pragma once

#include "../../Core/IFileSystemDriver.h"
#include "../../Core/IHardwareController.h"
#include "exFAT_Structures.h"

namespace Erasure {
namespace FileSystems {

class ExFatDriver : public Core::IFileSystemDriver {
private:
    Core::IHardwareController* m_hardware;
    
    // Cached map info
    ExFatBootSector m_vbr;
    uint32_t m_bytesPerSector;
    uint32_t m_sectorsPerCluster;

    // Bitmap cache
    uint32_t m_bitmapFirstCluster;
    uint64_t m_bitmapDataLength;

    // Helpers to calculate physical offsets
    uint64_t ClusterToSector(uint32_t cluster) const;
    
    // Low level FAT and Bitmap manipulators
    uint32_t ReadFatEntry(uint32_t cluster) const;
    bool WriteFatEntry(uint32_t cluster, uint32_t value);
    bool ClearBitmapBit(uint32_t cluster);

public:
    explicit ExFatDriver(Core::IHardwareController* hardware);
    ~ExFatDriver() override = default;

    bool Mount() override;
    bool DeleteFile(const std::string& relativePath) override;
    bool WipeVolume() override;

    // Test function to verify VBR parsing
    void PrintVBRInfo() const;
};

} // namespace FileSystems
} // namespace Erasure
