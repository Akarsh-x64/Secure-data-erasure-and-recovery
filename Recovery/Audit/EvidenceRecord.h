#pragma once

#include "../Carving/Core/CarvingCandidate.h"
#include "../Carving/Core/VerificationResult.h"
#include "../Core/FileRecord.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Recovery {
namespace Audit {

    /**
     * @brief Audit-facing provenance snapshot for one recovered item.
     *
     * This is derived from an existing FileRecord or CarvingCandidate; it is
     * not a replacement recovery model and does not perform recovery,
     * verification, hashing, or persistence. Unknown provenance stays unset
     * and this structure does not determine legal admissibility.
     */
    struct EvidenceRecord {
        std::string evidenceId;
        Core::RecoveryBackend recoveryBackend = Core::RecoveryBackend::Unknown;
        std::string sourcePath;
        std::optional<uint32_t> partitionIndex;
        std::optional<uint64_t> partitionOffset;
        std::optional<uint64_t> partitionSize;
        std::optional<Core::FileSystemType> filesystem;
        std::string name;
        std::string path;
        std::string fileType;
        std::optional<uint64_t> recoveredSize;
        std::optional<uint64_t> sourceOffset;
        std::string detectedType;
        std::optional<Carving::VerificationClassification> verificationClassification;
        std::optional<double> verificationScore;
        std::string sha256;
        std::vector<Carving::VerificationCheck> verificationChecks;
        std::string verificationExplanation;

        static EvidenceRecord FromFileRecord(
            const std::string& id,
            const Core::FileRecord& record,
            const Carving::VerificationResult* verification = nullptr) {
            EvidenceRecord result;
            result.evidenceId = id;
            result.recoveryBackend = record.recoveryBackend;
            result.sourcePath = record.sourcePath;
            result.partitionIndex = record.partitionIndex;
            if (record.partitionIndex.has_value()) {
                result.partitionOffset = record.partitionOffset;
                result.partitionSize = record.partitionSize;
            }
            result.filesystem = record.filesystem;
            result.name = record.filename;
            result.path = record.path;
            result.recoveredSize = record.size;
            if (verification != nullptr) {
                result.ApplyVerification(*verification);
            }
            return result;
        }

        static EvidenceRecord FromCarvingCandidate(
            const std::string& id,
            const Carving::CarvingCandidate& candidate) {
            EvidenceRecord result;
            result.evidenceId = id;
            result.recoveryBackend = candidate.recoveryBackend;
            result.sourcePath = candidate.sourcePath;
            result.partitionIndex = candidate.partitionIndex;
            if (candidate.partitionIndex.has_value()) {
                result.partitionOffset = candidate.partitionOffset;
                result.partitionSize = candidate.partitionSize;
            }
            result.name = candidate.recoveredPath;
            result.path = candidate.recoveredPath;
            result.fileType = candidate.fileType;
            result.recoveredSize = candidate.recoveredSize;
            if (candidate.sourceOffsetKnown) {
                result.sourceOffset = candidate.sourceOffset;
            }
            result.ApplyVerification(candidate.verification);
            return result;
        }

    private:
        void ApplyVerification(const Carving::VerificationResult& verification) {
            detectedType = verification.detectedType;
            verificationClassification = verification.classification;
            verificationScore = verification.score;
            sha256 = verification.sha256;
            verificationChecks = verification.checks;
            verificationExplanation = verification.explanation;
        }
    };

} // namespace Audit
} // namespace Recovery
