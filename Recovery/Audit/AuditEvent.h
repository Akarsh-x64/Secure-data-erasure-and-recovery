#pragma once

#include "../Core/RecoveryEnums.h"
#include "../Carving/Core/VerificationResult.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace Recovery {
namespace Audit {

    enum class AuditEventType {
        RecoveryStarted,
        BackendStarted,
        BackendCompleted,
        BackendFailed,
        CandidateRecovered,
        VerificationCompleted,
        RecoveryCompleted,
        RecoveryFailed
    };

    /**
     * @brief Records an action or state transition in the recovery workflow.
     *
     * Unavailable provenance remains unset. This record is an internal audit
     * model and does not establish legal admissibility or forensic validity.
     */
    struct AuditEvent {
        std::chrono::system_clock::time_point timestamp;
        AuditEventType type = AuditEventType::RecoveryStarted;
        std::string sessionId;
        std::optional<Core::RecoveryBackend> backend;
        std::string message;
        std::optional<uint64_t> candidateId;
        std::optional<uint64_t> sourceOffset;
        std::optional<uint64_t> dataLength;
        std::optional<Carving::VerificationResult> verification;
    };

} // namespace Audit
} // namespace Recovery
