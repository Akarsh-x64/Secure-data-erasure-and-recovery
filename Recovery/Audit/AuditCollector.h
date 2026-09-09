#pragma once

#include "AuditLog.h"
#include "../Carving/Core/CarvingCandidate.h"

#include <chrono>
#include <optional>
#include <string>

namespace Recovery {
namespace Audit {

    /**
     * @brief Records typed recovery lifecycle events into a caller-owned log.
     *
     * The collector does not perform recovery, own an orchestrator, persist
     * events, or invent unavailable provenance. It does not establish legal
     * admissibility or forensic validity.
     */
    class AuditCollector {
    public:
        explicit AuditCollector(AuditLog& log)
            : m_log(log) {}

        void RecordRecoveryStarted(const std::string& sessionId,
                                   const std::string& message) {
            Add(AuditEventType::RecoveryStarted, sessionId, std::nullopt,
                message);
        }

        void RecordBackendStarted(const std::string& sessionId,
                                  Core::RecoveryBackend backend,
                                  const std::string& message) {
            Add(AuditEventType::BackendStarted, sessionId, backend, message);
        }

        void RecordBackendCompleted(const std::string& sessionId,
                                    Core::RecoveryBackend backend,
                                    bool success,
                                    const std::string& message) {
            Add(AuditEventType::BackendCompleted, sessionId, backend,
                message + (success ? " (succeeded)." : " (failed)."));
        }

        void RecordBackendFailed(const std::string& sessionId,
                                 Core::RecoveryBackend backend,
                                 const std::string& message) {
            Add(AuditEventType::BackendFailed, sessionId, backend, message);
        }

        void RecordCandidateRecovered(
            const std::string& sessionId,
            const Carving::CarvingCandidate& candidate,
            std::optional<uint64_t> candidateId = std::nullopt) {
            AuditEvent event = NewEvent(
                AuditEventType::CandidateRecovered, sessionId,
                candidate.recoveryBackend, "Candidate recovered.");
            event.candidateId = candidateId;
            event.dataLength = candidate.recoveredSize;
            if (candidate.sourceOffsetKnown) {
                event.sourceOffset = candidate.sourceOffset;
            }
            m_log.Add(std::move(event));
        }

        void RecordVerificationCompleted(
            const std::string& sessionId,
            const Carving::VerificationResult& verification,
            std::optional<uint64_t> candidateId = std::nullopt) {
            AuditEvent event = NewEvent(
                AuditEventType::VerificationCompleted, sessionId,
                std::nullopt, "Verification completed: " +
                    std::string(Carving::ToString(verification.classification)));
            event.candidateId = candidateId;
            event.verification = verification;
            m_log.Add(std::move(event));
        }

        void RecordRecoveryCompleted(const std::string& sessionId,
                                     const std::string& message) {
            Add(AuditEventType::RecoveryCompleted, sessionId, std::nullopt,
                message);
        }

        void RecordRecoveryFailed(const std::string& sessionId,
                                  const std::string& message) {
            Add(AuditEventType::RecoveryFailed, sessionId, std::nullopt,
                message);
        }

    private:
        static AuditEvent NewEvent(
            AuditEventType type,
            const std::string& sessionId,
            std::optional<Core::RecoveryBackend> backend,
            const std::string& message) {
            AuditEvent event;
            event.timestamp = std::chrono::system_clock::now();
            event.type = type;
            event.sessionId = sessionId;
            event.backend = backend;
            event.message = message;
            return event;
        }

        void Add(AuditEventType type,
                 const std::string& sessionId,
                 std::optional<Core::RecoveryBackend> backend,
                 const std::string& message) {
            m_log.Add(NewEvent(type, sessionId, backend, message));
        }

        AuditLog& m_log;
    };

} // namespace Audit
} // namespace Recovery
