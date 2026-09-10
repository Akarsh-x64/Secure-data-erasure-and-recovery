#pragma once

#include "../../Audit/AuditLog.h"
#include "../../Audit/EvidenceRecord.h"
#include "../../Core/FileRecord.h"
#include "../../Core/PartitionInfo.h"
#include "CarvingResult.h"
#include "VerificationResult.h"

#include <optional>
#include <string>
#include <vector>

namespace Recovery {
namespace Carving {

    struct RecoveryResult {
        bool success = false;
        std::string sourcePath;
        std::optional<Core::PartitionScheme> detectedPartitionScheme;
        std::vector<Core::PartitionInfo> partitions;
        std::optional<Core::PartitionInfo> selectedPartition;
        Core::RecoveryMethod recoveryMethod = Core::RecoveryMethod::Carving;
        bool capabilityAvailable = true;
        std::string capabilityMessage;
        std::vector<Core::FileRecord> metadataRecords;
        // Entries correspond to metadataRecords by index.
        std::vector<VerificationResult> metadataVerificationResults;
        CarvingResult carvingResult;
        bool tskAttempted = false;
        bool tskSucceeded = false;
        bool photoRecAttempted = false;
        bool photoRecSucceeded = false;
        std::string tskErrorMessage;
        std::string photoRecErrorMessage;
        std::string errorMessage;
        std::string limitationMessage;
        Audit::AuditLog auditLog;
        std::vector<Audit::EvidenceRecord> evidenceRecords;
        std::string evidenceManifest;
        std::string evidenceManifestHash;
    };

} // namespace Carving
} // namespace Recovery
