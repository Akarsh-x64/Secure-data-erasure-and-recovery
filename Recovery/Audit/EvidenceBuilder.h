#pragma once

#include "EvidenceRecord.h"
#include "../Carving/Core/RecoveryResult.h"

#include <string>
#include <vector>

namespace Recovery {
namespace Audit {

    /**
     * @brief Deterministically converts recovery output into evidence snapshots.
     *
     * The builder performs no recovery, verification, hashing, persistence, or
     * deduplication. Inconsistent metadata-verification vectors are preserved
     * without attaching unverifiable results.
     */
    class EvidenceBuilder {
    public:
        std::vector<EvidenceRecord> Build(
            const Carving::RecoveryResult& recovery) const {
            std::vector<EvidenceRecord> records;
            records.reserve(recovery.metadataRecords.size() +
                            recovery.carvingResult.candidates.size());

            const bool metadataVerificationAligned =
                recovery.metadataVerificationResults.size() ==
                recovery.metadataRecords.size();
            for (std::size_t index = 0;
                 index < recovery.metadataRecords.size(); ++index) {
                const auto& metadata = recovery.metadataRecords[index];
                const Carving::VerificationResult* verification = nullptr;
                if (metadataVerificationAligned) {
                    verification = &recovery.metadataVerificationResults[index];
                }
                records.push_back(EvidenceRecord::FromFileRecord(
                    "metadata-" + std::to_string(metadata.id),
                    metadata,
                    verification));
            }

            for (std::size_t index = 0;
                 index < recovery.carvingResult.candidates.size(); ++index) {
                records.push_back(EvidenceRecord::FromCarvingCandidate(
                    "carving-" + std::to_string(index),
                    recovery.carvingResult.candidates[index]));
            }
            return records;
        }
    };

} // namespace Audit
} // namespace Recovery
