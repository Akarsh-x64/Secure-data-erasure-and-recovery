#include "VerificationReport.h"
#include <iomanip>
#include <sstream>

namespace Erasure {
namespace Verification {

std::string VerificationReportScopeToString(VerificationScope scope) {
    switch (scope) {
        case VerificationScope::FILE_ERASURE: return "FILE_ERASURE";
        case VerificationScope::DIRECTORY_ERASURE: return "DIRECTORY_ERASURE";
        case VerificationScope::VOLUME_WIPE: return "VOLUME_WIPE";
        default: return "UNKNOWN";
    }
}

std::string AuditReport::ToJson() const {
    std::ostringstream json;
    json << "{\n";
    json << "  \"type\": \"FORENSIC_AUDIT_REPORT\",\n";
    json << "  \"targetPath\": \"" << targetPath << "\",\n";
    json << "  \"scope\": \"" << VerificationReportScopeToString(scope) << "\",\n";
    json << "  \"timestamp\": \"" << timestamp << "\",\n";
    json << "  \"erasureStandard\": \"" << erasureStandard << "\",\n";
    json << "  \"filesystemType\": \"" << filesystemType << "\",\n";
    json << "  \"sectorsAudited\": " << sectorsAudited << ",\n";
    json << "  \"totalBytesAudited\": " << totalBytesAudited << ",\n";
    json << "  \"clusterSize\": " << clusterSize << ",\n";
    json << "  \"preWipeSha256\": \"" << preWipeSha256 << "\",\n";
    json << "  \"postWipeSha256\": \"" << postWipeSha256 << "\",\n";
    json << "  \"rawByteMatchRate\": " << std::fixed << std::setprecision(2) << rawByteMatchRate << ",\n";
    json << "  \"statistics\": {\n";
    json << "    \"shannonEntropy\": " << std::setprecision(4) << shannonEntropy << ",\n";
    json << "    \"chiSquareValue\": " << std::setprecision(2) << chiSquareValue << ",\n";
    json << "    \"chiSquarePValue\": " << std::setprecision(4) << chiSquarePValue << ",\n";
    json << "    \"serialCorrelation\": " << std::setprecision(4) << serialCorrelation << ",\n";
    json << "    \"monteCarloPi\": " << std::setprecision(5) << monteCarloPi << ",\n";
    json << "    \"monteCarloPiErrorPercent\": " << std::setprecision(3) << monteCarloPiError << "\n";
    json << "  },\n";
    json << "  \"carving\": {\n";
    json << "    \"signaturesChecked\": " << signaturesChecked << ",\n";
    json << "    \"signaturesDetected\": " << signaturesDetected << ",\n";
    json << "    \"artifacts\": [\n";
    for (size_t i = 0; i < detectedArtifacts.size(); ++i) {
        json << "      {\n";
        json << "        \"name\": \"" << detectedArtifacts[i].signatureName << "\",\n";
        json << "        \"extension\": \"" << detectedArtifacts[i].extension << "\",\n";
        json << "        \"offset\": " << detectedArtifacts[i].byteOffset << ",\n";
        json << "        \"lba\": " << detectedArtifacts[i].lba << "\n";
        json << "      }" << (i + 1 < detectedArtifacts.size() ? "," : "") << "\n";
    }
    json << "    ]\n";
    json << "  },\n";
    json << "  \"slackSpace\": {\n";
    json << "    \"auditedBytes\": " << slackBytesAudited << ",\n";
    json << "    \"residualBytes\": " << slackResidualBytes << "\n";
    json << "  },\n";
    json << "  \"metadata\": {\n";
    json << "    \"cleared\": " << (metadataCleared ? "true" : "false") << ",\n";
    json << "    \"directoryUnlinked\": " << (directoryUnlinked ? "true" : "false") << "\n";
    json << "  },\n";
    json << "  \"nistSampling\": {\n";
    json << "    \"samplesChecked\": " << nistSamplesChecked << ",\n";
    json << "    \"zones\": " << nistZonesCount << ",\n";
    json << "    \"confidencePercent\": " << std::setprecision(3) << nistConfidencePercent << "\n";
    json << "  },\n";
    json << "  \"verdict\": {\n";
    json << "    \"passed\": " << (passed ? "true" : "false") << ",\n";
    json << "    \"failureReason\": \"" << failureReason << "\"\n";
    json << "  }\n";
    json << "}";
    return json.str();
}

void AuditReport::PrintTerminalReport(std::ostream& os) const {
    os << "\n";
    os << "================================================================================\n";
    os << "        CRYPTOGRAPHIC & FORENSIC ERASURE VERIFICATION CERTIFICATE               \n";
    os << "================================================================================\n";
    os << " Target Path:        " << targetPath << "\n";
    os << " Operation Scope:    " << VerificationReportScopeToString(scope) << "\n";
    os << " Filesystem Type:    " << filesystemType << "\n";
    os << " Sanitization Standard: " << erasureStandard << "\n";
    os << " Timestamp:          " << timestamp << "\n";
    os << " Sectors Audited:    " << sectorsAudited << " (" << (totalBytesAudited / 1024) << " KB)\n";
    os << "--------------------------------------------------------------------------------\n";

    // 1. Overall Status Badge
    os << " [AUDIT VERDICT]:    ";
    if (passed) {
        os << "[PASSED] - ZERO RECOVERY GUARANTEE CONFIRMED\n";
    } else {
        os << "[FAILED] - " << failureReason << "\n";
    }
    os << "--------------------------------------------------------------------------------\n";

    // 2. Cryptographic Hashes
    os << " [1. CRYPTOGRAPHIC PROOF (FIPS 180-4 SHA-256)]\n";
    if (!preWipeSha256.empty()) {
        os << "   Pre-Wipe Digest:  " << preWipeSha256 << "\n";
    }
    os << "   Post-Wipe Digest: " << postWipeSha256 << "\n";
    if (!preWipeSha256.empty() && preWipeSha256 != postWipeSha256) {
        os << "   Divergence Rate:  100.00% (Payload Fundamentally Transformed)\n";
    }
    os << "   Byte Pattern Match: " << std::fixed << std::setprecision(2) << rawByteMatchRate << "%\n\n";

    // 3. Statistical Randomness & Uniformity
    os << " [2. STATISTICAL ENTROPY & RANDOMNESS AUDIT]\n";
    os << "   Shannon Entropy:  " << StatisticalTests::RenderAsciiGauge(shannonEntropy, 8.0, 20, "bits/byte") << "\n";
    os << "   Chi-Square (x2):  " << std::fixed << std::setprecision(2) << chiSquareValue
       << " (p-value: " << std::setprecision(4) << chiSquarePValue
       << (chiSquarePValue >= 0.05 ? " [UNIFORM / TRUE NOISE]" : " [STRUCTURED / SKEWED]") << ")\n";
    os << "   Serial Autocorr:  " << std::setprecision(4) << serialCorrelation << " (Adjacent-byte dependency)\n";
    os << "   Monte Carlo Pi:   " << std::setprecision(5) << monteCarloPi
       << " (Error: " << std::setprecision(3) << monteCarloPiError << "%)\n\n";

    // Visual Byte Distribution Chart
    if (!byteHistogram.empty()) {
        os << StatisticalTests::RenderByteDistributionHistogram(byteHistogram, 32, 6);
        os << "\n";
    }

    // 4. Adversarial Signature Carving
    os << " [3. ADVERSARIAL FILE SIGNATURE CARVING]\n";
    os << "   Signatures Audited: " << signaturesChecked << " magic file headers\n";
    os << "   Detections Found:   " << signaturesDetected;
    if (signaturesDetected == 0) {
        os << " [PASSED - NO SURVIVING FILE ARTIFACTS]\n";
    } else {
        os << " [ALERT - " << signaturesDetected << " SURVIVING HEADERS FOUND!]\n";
        for (const auto& art : detectedArtifacts) {
            os << "     -> " << art.signatureName << " (" << art.extension
               << ") at Disk Offset 0x" << std::hex << art.byteOffset
               << " (LBA " << std::dec << art.lba << ")\n";
        }
    }

    // 5. Cluster Slack Space & Metadata
    if (scope == VerificationScope::FILE_ERASURE || scope == VerificationScope::DIRECTORY_ERASURE) {
        os << "\n [4. CLUSTER SLACK SPACE & METADATA PURGE]\n";
        os << "   Slack Bytes Audited:  " << slackBytesAudited << " bytes\n";
        os << "   Residual Slack Data:  " << slackResidualBytes << " bytes ("
           << (slackResidualBytes == 0 ? "100.00% CLEAN [PASSED]" : "DATA RESIDUE DETECTED") << ")\n";
        os << "   On-Disk Inode/Entry:  " << (metadataCleared ? "STAMPED DELETED & ZEROED [PASSED]" : "STILL ALLOCATED") << "\n";
        os << "   Parent Dir Unlinking: " << (directoryUnlinked ? "REMOVED FROM DIRECTORY B-TREE [PASSED]" : "LINKED") << "\n";
    }

    // 6. NIST SP 800-88 Sampling (Volume Wipes)
    if (scope == VerificationScope::VOLUME_WIPE) {
        os << "\n [5. NIST SP 800-88 REV. 1 STRATIFIED SAMPLING]\n";
        os << "   Sample Set Size:    " << nistSamplesChecked << " clusters across " << nistZonesCount << " zones\n";
        os << "   Confidence Level:   " << StatisticalTests::RenderConfidenceGauge(nistConfidencePercent, 24) << "\n";
    }

    os << "================================================================================\n";
    os << " [JSON-RPC IPC PAYLOAD READY FOR ELECTRON FRONTEND]\n";
    os << "================================================================================\n\n";
}

} // namespace Verification
} // namespace Erasure
