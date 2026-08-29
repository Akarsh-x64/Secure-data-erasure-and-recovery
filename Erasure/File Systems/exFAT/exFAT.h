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

    // Helpers to calculate physical offsets
    uint64_t ClusterToSector(uint32_t cluster) const;

public:
    explicit ExFatDriver(Core::IHardwareController* hardware);
    ~ExFatDriver() override = default;

    bool Mount() override;
    bool DeleteFile(const std::string& relativePath) override;
    bool WipeVolume() override;
};

} // namespace FileSystems
} // namespace Erasure
