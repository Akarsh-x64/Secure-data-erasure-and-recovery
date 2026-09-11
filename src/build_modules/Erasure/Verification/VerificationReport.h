#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <iostream>
#include "SignatureCarver.h"
#include "StatisticalTests.h"

namespace Erasure {
namespace Verification {

enum class VerificationScope {
    FILE_ERASURE,
    DIRECTORY_ERASURE,
    VOLUME_WIPE
};

struct AuditReport {
    // Identity & Target
    std::string targetPath;
    VerificationScope scope = VerificationScope::FILE_ERASURE;
    std::string timestamp;
    std::string erasureStandard = "DoD 5220.22-M (3-Pass)";
    std::string filesystemType = "Generic";

    // Capacity & Physical Extents
    uint64_t sectorsAudited = 0;
    uint64_t totalBytesAudited = 0;
    uint32_t clusterSize = 4096;

    // Cryptographic Evidence
    std::string preWipeSha256;
    std::string postWipeSha256;
    double rawByteMatchRate = 100.0; // % matching expected sanitize pattern

    // Statistical & Entropy Metrics
    double shannonEntropy = 0.0;
    double chiSquareValue = 0.0;
    double chiSquarePValue = 0.0;
    double serialCorrelation = 0.0;
    double monteCarloPi = 0.0;
    double monteCarloPiError = 0.0;
    std::vector<uint64_t> byteHistogram; // 256 bins for visual rendering

    // Adversarial Signature Carving
    size_t signaturesChecked = 0;
    size_t signaturesDetected = 0;
    std::vector<CarvedArtifact> detectedArtifacts;

    // Slack Space & Structure Wiping
    uint64_t slackBytesAudited = 0;
    uint64_t slackResidualBytes = 0;
    bool metadataCleared = true;
    bool directoryUnlinked = true;

    // NIST SP 800-88 Sampling (Volume Wipes)
    uint32_t nistSamplesChecked = 0;
    uint32_t nistZonesCount = 0;
    double nistConfidencePercent = 0.0;

    // Overall Verdict
    bool passed = false;
    std::string failureReason;

    // Serialization & Terminal Presentation
    std::string ToJson() const;
    void PrintTerminalReport(std::ostream& os = std::cout) const;
};

} // namespace Verification
} // namespace Erasure
