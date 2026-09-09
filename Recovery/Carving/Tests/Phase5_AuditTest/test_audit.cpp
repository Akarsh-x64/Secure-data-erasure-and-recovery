#include "../../../Audit/AuditCollector.h"
#include "../../../Audit/EvidenceBuilder.h"
#include "../../../Audit/EvidenceManifest.h"
#include "../../../Audit/EvidenceRecord.h"

#include <cassert>
#include <chrono>
#include <string>

int main() {
    using namespace Recovery;
    using namespace Recovery::Audit;

    const auto timestamp = std::chrono::system_clock::now();
    AuditEvent event;
    event.timestamp = timestamp;
    event.type = AuditEventType::CandidateRecovered;
    event.sessionId = "session-1";
    event.backend = Core::RecoveryBackend::PHOTOREC_CARVING;
    event.message = "Candidate recovered";
    event.candidateId = 7;
    event.dataLength = 42;

    assert(event.timestamp == timestamp);
    assert(event.backend.has_value());
    assert(*event.backend == Core::RecoveryBackend::PHOTOREC_CARVING);
    assert(!event.sourceOffset.has_value());

    AuditLog log;
    log.Append(event);
    AuditEvent second = event;
    second.type = AuditEventType::VerificationCompleted;
    second.message = "Verification completed";
    log.Add(second);
    assert(log.Size() == 2);
    assert(log.Events()[0].type == AuditEventType::CandidateRecovered);
    assert(log.Events()[1].type == AuditEventType::VerificationCompleted);
    assert(!log.Empty());
    log.Clear();
    assert(log.Empty());
    assert(log.Size() == 0);

    Core::FileRecord file;
    file.id = 19;
    file.path = "/docs/report.pdf";
    file.filename = "report.pdf";
    file.size = 128;
    file.sourcePath = "source.img";
    file.partitionIndex = 1;
    file.partitionOffset = 4096;
    file.partitionSize = 8192;
    file.recoveryBackend = Core::RecoveryBackend::TSK_METADATA;
    file.filesystem = Core::FileSystemType::NTFS;

    Carving::VerificationResult verification;
    verification.detectedType = "PDF";
    verification.classification = Carving::VerificationClassification::VALID;
    verification.score = 95.0;
    verification.sha256 = "abc123";
    verification.explanation = "Valid PDF";
    verification.checks.emplace_back(
        "Signature", Carving::CheckStatus::PASS, "Header found");

    const EvidenceRecord fileEvidence =
        EvidenceRecord::FromFileRecord("file-19", file, &verification);
    assert(fileEvidence.evidenceId == "file-19");
    assert(fileEvidence.recoveryBackend == Core::RecoveryBackend::TSK_METADATA);
    assert(fileEvidence.partitionIndex == 1);
    assert(fileEvidence.partitionOffset == 4096);
    assert(fileEvidence.verificationClassification ==
           Carving::VerificationClassification::VALID);
    assert(fileEvidence.verificationScore == 95.0);
    assert(fileEvidence.sha256 == "abc123");
    assert(fileEvidence.verificationChecks.size() == 1);
    assert(!fileEvidence.sourceOffset.has_value());

    Carving::CarvingCandidate candidate;
    candidate.recoveredPath = "output/f0001.jpg";
    candidate.fileType = "jpg";
    candidate.recoveredSize = 64;
    candidate.sourcePath = "source.img";
    candidate.partitionIndex = 0;
    candidate.recoveryBackend = Core::RecoveryBackend::PHOTOREC_CARVING;
    candidate.sourceOffsetKnown = false;
    const EvidenceRecord carvingEvidence =
        EvidenceRecord::FromCarvingCandidate("carving-1", candidate);
    assert(carvingEvidence.recoveryBackend ==
           Core::RecoveryBackend::PHOTOREC_CARVING);
    assert(carvingEvidence.sourcePath == "source.img");
    assert(!carvingEvidence.sourceOffset.has_value());
    assert(!carvingEvidence.verificationClassification.has_value() ||
           carvingEvidence.verificationClassification ==
               Carving::VerificationClassification::UNKNOWN);

    AuditLog collected;
    AuditCollector collector(collected);
    collector.RecordRecoveryStarted("session-2", "Recovery started.");
    collector.RecordBackendStarted(
        "session-2", Core::RecoveryBackend::PHOTOREC_CARVING, "PhotoRec started.");

    Carving::CarvingCandidate knownOffset = candidate;
    knownOffset.sourceOffsetKnown = true;
    knownOffset.sourceOffset = 2048;
    collector.RecordCandidateRecovered("session-2", knownOffset, 11);
    collector.RecordVerificationCompleted("session-2", verification, 11);
    collector.RecordBackendCompleted(
        "session-2", Core::RecoveryBackend::PHOTOREC_CARVING, true,
        "PhotoRec completed");
    collector.RecordRecoveryCompleted("session-2", "Recovery completed.");

    assert(collected.Size() == 6);
    assert(collected.Events()[0].type == AuditEventType::RecoveryStarted);
    assert(collected.Events()[1].type == AuditEventType::BackendStarted);
    assert(collected.Events()[2].type == AuditEventType::CandidateRecovered);
    assert(collected.Events()[2].sourceOffset == 2048);
    assert(collected.Events()[2].candidateId == 11);
    assert(collected.Events()[3].verification.has_value());
    assert(collected.Events()[3].verification->classification ==
           Carving::VerificationClassification::VALID);
    assert(collected.Events()[4].type == AuditEventType::BackendCompleted);
    assert(collected.Events()[5].type == AuditEventType::RecoveryCompleted);
    assert(collected.Events()[0].sessionId == "session-2");

    AuditLog unavailableOffsetLog;
    AuditCollector unavailableOffsetCollector(unavailableOffsetLog);
    unavailableOffsetCollector.RecordCandidateRecovered(
        "session-3", candidate);
    assert(!unavailableOffsetLog.Events()[0].sourceOffset.has_value());

    const auto candidateBefore = candidate;
    const auto verificationBefore = verification;
    unavailableOffsetCollector.RecordVerificationCompleted(
        "session-3", verification);
    assert(candidate.recoveredPath == candidateBefore.recoveredPath);
    assert(verification.sha256 == verificationBefore.sha256);

    unavailableOffsetLog.Clear();
    unavailableOffsetCollector.RecordBackendFailed(
        "session-3", Core::RecoveryBackend::TSK_METADATA, "TSK failed.");
    collector.RecordRecoveryFailed("session-2", "Recovery failed.");
    assert(collected.Size() == 7);
    assert(collected.Events()[6].type == AuditEventType::RecoveryFailed);
    assert(unavailableOffsetLog.Size() == 1);
    assert(unavailableOffsetLog.Events()[0].type ==
           AuditEventType::BackendFailed);

    Carving::RecoveryResult recovery;
    recovery.metadataRecords.push_back(file);
    recovery.metadataVerificationResults.push_back(verification);
    recovery.carvingResult.candidates.push_back(knownOffset);

    EvidenceBuilder builder;
    const auto evidence = builder.Build(recovery);
    const auto evidenceAgain = builder.Build(recovery);
    assert(evidence.size() == 2);
    assert(evidenceAgain.size() == evidence.size());
    assert(evidence[0].evidenceId == "metadata-19");
    assert(evidence[0].path == "/docs/report.pdf");
    assert(evidence[0].recoveredSize == 128);
    assert(evidence[0].filesystem == Core::FileSystemType::NTFS);
    assert(evidence[0].verificationClassification ==
           Carving::VerificationClassification::VALID);
    assert(evidence[0].verificationScore == 95.0);
    assert(evidence[0].sha256 == "abc123");
    assert(evidence[0].verificationChecks.size() == 1);
    assert(evidence[1].evidenceId == "carving-0");
    assert(evidence[1].sourceOffset == 2048);
    assert(evidence[1].recoveryBackend ==
           Core::RecoveryBackend::PHOTOREC_CARVING);
    assert(evidence[0].evidenceId == evidenceAgain[0].evidenceId);
    assert(evidence[1].sourceOffset == evidenceAgain[1].sourceOffset);

    Carving::RecoveryResult inconsistent;
    inconsistent.metadataRecords.push_back(file);
    const auto inconsistentEvidence = builder.Build(inconsistent);
    assert(inconsistentEvidence.size() == 1);
    assert(!inconsistentEvidence[0].verificationClassification.has_value());

    Carving::RecoveryResult unknownOffset;
    unknownOffset.carvingResult.candidates.push_back(candidate);
    const auto unknownOffsetEvidence = builder.Build(unknownOffset);
    assert(unknownOffsetEvidence.size() == 1);
    assert(!unknownOffsetEvidence[0].sourceOffset.has_value());

    const auto manifest = EvidenceManifest::Canonicalize(evidence);
    assert(manifest == EvidenceManifest::Canonicalize(evidenceAgain));
    assert(EvidenceManifest::Hash(evidence) == EvidenceManifest::Hash(evidenceAgain));
    assert(manifest.find("evidence_manifest_version=1\n") == 0);
    assert(manifest.find("record_count=2\n") != std::string::npos);

    auto changed = evidence;
    changed[0].path = "/docs/changed.pdf";
    assert(EvidenceManifest::Hash(changed) != EvidenceManifest::Hash(evidence));

    auto reordered = evidence;
    std::swap(reordered[0], reordered[1]);
    assert(EvidenceManifest::Hash(reordered) != EvidenceManifest::Hash(evidence));

    auto absentOffset = candidate;
    absentOffset.sourceOffsetKnown = false;
    const auto absentEvidence =
        EvidenceRecord::FromCarvingCandidate("offset", absentOffset);
    auto zeroOffset = candidate;
    zeroOffset.sourceOffsetKnown = true;
    zeroOffset.sourceOffset = 0;
    const auto zeroEvidence =
        EvidenceRecord::FromCarvingCandidate("offset", zeroOffset);
    assert(EvidenceManifest::Hash({absentEvidence}) !=
           EvidenceManifest::Hash({zeroEvidence}));
    assert(EvidenceManifest::Hash({}) == EvidenceManifest::Hash({}));

    auto changedVerification = evidence;
    changedVerification[0].verificationScore = 94.0;
    assert(EvidenceManifest::Hash(changedVerification) !=
           EvidenceManifest::Hash(evidence));

    return 0;
}
