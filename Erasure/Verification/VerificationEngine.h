#pragma once

#include "../../Core/IHardwareController.h"
#include "../../Core/IStorageDevice.h"
#include "VerificationReport.h"
#include "SignatureCarver.h"
#include "StatisticalTests.h"
#include <vector>
#include <string>
#include <memory>

namespace Erasure {
namespace Verification {

/**
 * @brief Multi-Filesystem Forensic Erasure Verification Engine.
 *
 * Adheres strictly to the 3-Layer Decoupled Architecture:
 * Routes all disk reads through IHardwareController / IStorageDevice.
 */
class VerificationEngine {
private:
    Core::IHardwareController* m_hardware;
    Core::IStorageDevice*      m_device;
    SignatureCarver            m_carver;

    static std::string GetCurrentTimestamp();

public:
    VerificationEngine(Core::IHardwareController* hardware, Core::IStorageDevice* device);

    /**
     * @brief Reads target sectors prior to deletion and computes baseline SHA-256 digest.
     */
    std::string CapturePreWipeDigest(const std::vector<uint64_t>& sectors);

    /**
     * @brief Audits a single erased file.
     * Checks physical extents, cluster slack space, entropy, and runs adversarial carver.
     */
    AuditReport AuditFileErasure(const std::string& path,
                                const std::string& fsType,
                                const std::vector<uint64_t>& sectors,
                                uint64_t fileSize,
                                uint32_t clusterSize,
                                const std::string& preWipeSha256,
                                bool metadataCleared = true,
                                bool dirUnlinked = true);

    /**
     * @brief Audits an erased directory tree.
     */
    AuditReport AuditDirectoryErasure(const std::string& path,
                                     const std::string& fsType,
                                     const std::vector<std::string>& childFiles,
                                     const std::vector<uint64_t>& dirMetadataSectors,
                                     uint32_t clusterSize,
                                     bool parentUnlinked = true);

    /**
     * @brief Audits a wiped volume using NIST SP 800-88 Rev. 1 Stratified Sampling.
     */
    AuditReport AuditVolumeWipe(const std::string& fsType,
                               uint64_t firstDataSector,
                               uint64_t totalSectors,
                               uint32_t sectorsPerCluster,
                               const std::vector<uint64_t>& quarantinedSectors = {});
};

} // namespace Verification
} // namespace Erasure
