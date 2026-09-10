#include "RecoveryOrchestrator.h"

#include "../../Audit/AuditCollector.h"
#include "../../Audit/EvidenceBuilder.h"
#include "../../Audit/EvidenceManifest.h"
#ifdef _WIN32
#include "../../Acquisition/WindowsReadOnlyStorage.h"
#else
#include "../../Acquisition/LinuxReadOnlyStorage.h"
#endif
#include "../../Core/ByteReader.h"
#include "../../Partitions/GPTParser.h"
#include "../../Partitions/MBRParser.h"
#include "../../TSK/TskFileSystem.h"
#include "../PhotoRec/PhotoRecCarver.h"
#include "../Verification/VerificationEngine.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace Recovery {
namespace Carving {

    namespace {
        std::string ExtensionFor(const Core::FileRecord& record) {
            if (!record.extension.empty()) {
                return record.extension;
            }
            const auto dot = record.filename.find_last_of('.');
            return dot == std::string::npos ? std::string()
                                            : record.filename.substr(dot + 1);
        }

        VerificationResult UnavailableVerification(const std::string& path,
                                                    const std::string& reason) {
            VerificationResult verification;
            verification.path = path;
            verification.classification =
                VerificationClassification::UNKNOWN;
            verification.explanation = reason;
            verification.checks.emplace_back(
                "Candidate reconstruction", CheckStatus::NOT_PERFORMED, reason);
            return verification;
        }

        VerificationResult VerifyMetadataRecord(Core::IReadOnlyStorage& storage,
                                                const Core::FileRecord& record) {
            if (record.dataRangeStatus != Core::DataRangeStatus::Complete ||
                record.dataRanges.empty()) {
                return UnavailableVerification(
                    record.path,
                    "Verification not performed: complete data ranges are unavailable.");
            }

            std::vector<uint8_t> bytes(record.size);
            for (const auto& range : record.dataRanges) {
                if (range.sparse || range.compressed ||
                    range.logicalOffset >= bytes.size() ||
                    range.length > UINT32_MAX) {
                    return UnavailableVerification(
                        record.path,
                        "Verification not performed: file data could not be reconstructed.");
                }
                const uint64_t readableLength =
                    std::min<uint64_t>(range.length,
                                       bytes.size() - range.logicalOffset);
                if (!storage.Read(range.offset,
                                  static_cast<uint32_t>(readableLength),
                                  bytes.data() + range.logicalOffset)) {
                    return UnavailableVerification(
                        record.path,
                        "Verification not performed: file data could not be reconstructed.");
                }
            }

            Verification::VerificationEngine engine;
            return engine.VerifyBytes(bytes, ExtensionFor(record));
        }

        class TskMetadataRecoveryBackend final : public IMetadataRecoveryBackend {
        public:
            bool IsAvailable() const override {
                return RECOVERY_HAS_LIBTSK != 0;
            }

            std::string CapabilityMessage() const override {
                return IsAvailable()
                    ? "TSK_AVAILABLE"
                    : "TSK_UNAVAILABLE: libtsk is not available at compile time.";
            }

            bool Recover(Core::IReadOnlyStorage& storage,
                         const Core::StorageRegion& region,
                         std::vector<Core::FileRecord>& records,
                         std::string& errorMessage) override {
                TSK::TskFileSystem filesystem(storage, region);
                if (!filesystem.IsTskAvailable()) {
                    errorMessage = filesystem.GetLastError();
                    return false;
                }
                if (!filesystem.Mount() || !filesystem.EnumerateFiles(records)) {
                    errorMessage = filesystem.GetLastError();
                    if (errorMessage.empty()) {
                        errorMessage = "TSK metadata recovery failed.";
                    }
                    return false;
                }
                return true;
            }
        };
    }

    RecoveryOrchestrator::RecoveryOrchestrator(
        std::shared_ptr<Core::IReadOnlyStorage> storage,
        std::shared_ptr<ICarver> carver,
        std::shared_ptr<IMetadataRecoveryBackend> metadataBackend)
        : m_storage(std::move(storage)),
          m_carver(std::move(carver)) {
        if (!m_storage) {
#ifdef _WIN32
            m_storage = std::make_shared<Acquisition::WindowsReadOnlyStorage>();
#else
            m_storage = std::make_shared<Acquisition::LinuxReadOnlyStorage>();
#endif
        }
        if (!m_carver) {
            m_carver = std::make_shared<PhotoRecCarver>();
        }
        m_metadataBackend = std::move(metadataBackend);
        if (!m_metadataBackend) {
            m_metadataBackend = std::make_shared<TskMetadataRecoveryBackend>();
        }
    }

    RecoveryResult RecoveryOrchestrator::Recover(const RecoveryRequest& request) {
        RecoveryResult result;
        result.sourcePath = request.sourcePath;
        result.recoveryMethod = request.recoveryMethod;
        Audit::AuditLog auditLog;
        Audit::AuditCollector auditCollector(auditLog);
        const std::string sessionId =
            request.sourcePath.empty() ? "recovery" : request.sourcePath;
        auditCollector.RecordRecoveryStarted(
            sessionId, "Recovery started for source: " + request.sourcePath);

        const auto finalize = [&]() {
            result.evidenceRecords = Audit::EvidenceBuilder().Build(result);
            result.evidenceManifest =
                Audit::EvidenceManifest::Canonicalize(result.evidenceRecords);
            result.evidenceManifestHash =
                Audit::EvidenceManifest::Hash(result.evidenceRecords);
            if (result.success) {
                auditCollector.RecordRecoveryCompleted(
                    sessionId, "Recovery completed.");
            } else {
                auditCollector.RecordRecoveryFailed(
                    sessionId,
                    result.errorMessage.empty()
                        ? "Recovery failed."
                        : result.errorMessage);
            }
            result.auditLog = auditLog;
            return result;
        };

        if (request.sourcePath.empty()) {
            result.errorMessage = "Source path is empty.";
            return finalize();
        }
        if (request.recoveryMethod == Core::RecoveryMethod::Carving &&
            request.outputDir.empty()) {
            result.errorMessage = "Output directory path is empty.";
            return finalize();
        }

        if (!m_storage->Open(request.sourcePath)) {
            result.errorMessage = "Failed to open source storage.";
            return finalize();
        }

        struct StorageCloser {
            Core::IReadOnlyStorage* storage;
            ~StorageCloser() { storage->Close(); }
        } closeStorage{m_storage.get()};

        Core::ByteReader reader(m_storage.get());
        const uint32_t sectorSize = m_storage->GetSectorSize();

        Partitions::MBRParser mbr;
        if (mbr.CanParse(reader)) {
            result.partitions = mbr.Parse(reader, sectorSize);
            result.detectedPartitionScheme = Core::PartitionScheme::MBR;

            if (mbr.HasProtectiveMBR()) {
                Partitions::GPTParser gpt;
                if (gpt.CanParse(reader)) {
                    result.partitions = gpt.Parse(reader, sectorSize);
                    result.detectedPartitionScheme = Core::PartitionScheme::GPT;
                }
            }
        } else {
            Partitions::GPTParser gpt;
            if (gpt.CanParse(reader)) {
                result.partitions = gpt.Parse(reader, sectorSize);
                result.detectedPartitionScheme = Core::PartitionScheme::GPT;
            }
        }

        CarvingRequest carvingRequest;
        carvingRequest.sourcePath = request.sourcePath;
        carvingRequest.outputDir = request.outputDir;
        carvingRequest.requestedFileTypes = request.requestedFileTypes;

        if (request.selectedPartitionIndex.has_value()) {
            const uint32_t requestedIndex = *request.selectedPartitionIndex;
            if (requestedIndex >= result.partitions.size()) {
                result.errorMessage = "Selected partition index does not exist.";
                return finalize();
            }

            const Core::PartitionInfo& partition = result.partitions[requestedIndex];
            result.selectedPartition = partition;
        }

        const bool runMetadata =
            request.recoveryMethod == Core::RecoveryMethod::Metadata ||
            request.recoveryMethod == Core::RecoveryMethod::Combined;
        const bool runCarving =
            request.recoveryMethod == Core::RecoveryMethod::Carving ||
            request.recoveryMethod == Core::RecoveryMethod::Combined;

        if (runMetadata) {
            result.tskAttempted = true;
            auditCollector.RecordBackendStarted(
                sessionId, Core::RecoveryBackend::TSK_METADATA,
                "TSK metadata recovery started.");
            if (!request.selectedPartitionIndex.has_value()) {
                result.tskErrorMessage =
                    "TSK metadata recovery requires a selected partition.";
            } else {
                const Core::PartitionInfo& partition = *result.selectedPartition;
                result.capabilityAvailable = m_metadataBackend->IsAvailable();
                result.capabilityMessage = m_metadataBackend->CapabilityMessage();
                if (result.capabilityAvailable) {
                    result.tskSucceeded = m_metadataBackend->Recover(
                        *m_storage,
                        Core::StorageRegion(partition.startOffset, partition.sizeBytes),
                        result.metadataRecords,
                        result.tskErrorMessage);
                    if (result.tskSucceeded) {
                        for (auto& record : result.metadataRecords) {
                            record.recoveryBackend = Core::RecoveryBackend::TSK_METADATA;
                            record.sourcePath = request.sourcePath;
                            record.partitionIndex = request.selectedPartitionIndex;
                            record.partitionOffset = partition.startOffset;
                            record.partitionSize = partition.sizeBytes;
                            auditCollector.RecordCandidateRecovered(
                                sessionId, record, record.id);
                        }
                    }
                } else {
                    result.tskErrorMessage = result.capabilityMessage;
                }
            }
            if (result.tskSucceeded) {
                auditCollector.RecordBackendCompleted(
                    sessionId, Core::RecoveryBackend::TSK_METADATA, true,
                    "TSK metadata recovery completed.");
            } else {
                auditCollector.RecordBackendFailed(
                    sessionId, Core::RecoveryBackend::TSK_METADATA,
                    result.tskErrorMessage.empty()
                        ? "TSK metadata recovery failed."
                        : result.tskErrorMessage);
            }
        }

        {
            const Core::PartitionInfo* selectedPartition = nullptr;
            if (request.selectedPartitionIndex.has_value()) {
                selectedPartition = &*result.selectedPartition;
            }
            if (selectedPartition != nullptr) {
                const Core::PartitionInfo& partition = *selectedPartition;
                carvingRequest.sourceOffset = partition.startOffset;
                carvingRequest.sourceLength = partition.sizeBytes;
                result.limitationMessage =
                    "PhotoRecCarver currently ignores CarvingRequest sourceOffset and "
                    "sourceLength; carving remains whole-source.";
            }
        }

        if (runCarving) {
            result.photoRecAttempted = true;
            auditCollector.RecordBackendStarted(
                sessionId, Core::RecoveryBackend::PHOTOREC_CARVING,
                "PhotoRec carving started.");
            result.carvingResult = m_carver->Carve(carvingRequest);
            result.photoRecSucceeded = result.carvingResult.success;
            for (std::size_t index = 0;
                 index < result.carvingResult.candidates.size(); ++index) {
                auto& candidate = result.carvingResult.candidates[index];
                candidate.recoveryBackend = Core::RecoveryBackend::PHOTOREC_CARVING;
                candidate.sourcePath = request.sourcePath;
                candidate.partitionIndex = request.selectedPartitionIndex;
                candidate.partitionOffset = carvingRequest.sourceOffset;
                candidate.partitionSize = carvingRequest.sourceLength;
                candidate.sourceOffsetKnown = false;
                auditCollector.RecordCandidateRecovered(
                    sessionId, candidate, static_cast<uint64_t>(index));
                if (request.verifyResults) {
                    Verification::VerificationEngine engine;
                    candidate.verification = engine.Verify(candidate);
                    auditCollector.RecordVerificationCompleted(
                        sessionId, candidate.verification,
                        static_cast<uint64_t>(index));
                }
            }
            if (!result.photoRecSucceeded) {
                result.photoRecErrorMessage = result.carvingResult.errorMessage;
                if (result.photoRecErrorMessage.empty()) {
                    result.photoRecErrorMessage = "Carving failed.";
                }
                auditCollector.RecordBackendFailed(
                    sessionId, Core::RecoveryBackend::PHOTOREC_CARVING,
                    result.photoRecErrorMessage);
            } else {
                auditCollector.RecordBackendCompleted(
                    sessionId, Core::RecoveryBackend::PHOTOREC_CARVING, true,
                    "PhotoRec carving completed.");
            }
        }

        if (runMetadata && request.verifyResults) {
            result.metadataVerificationResults.reserve(result.metadataRecords.size());
            for (const auto& record : result.metadataRecords) {
                result.metadataVerificationResults.push_back(
                    VerifyMetadataRecord(*m_storage, record));
            }
            for (std::size_t index = 0;
                 index < result.metadataVerificationResults.size(); ++index) {
                const auto& verification =
                    result.metadataVerificationResults[index];
                const bool performed = std::any_of(
                    verification.checks.begin(), verification.checks.end(),
                    [](const VerificationCheck& check) {
                        return check.status != CheckStatus::NOT_PERFORMED;
                    });
                if (performed) {
                    auditCollector.RecordVerificationCompleted(
                        sessionId, verification,
                        result.metadataRecords[index].id);
                }
            }
        }

        result.success = (result.tskSucceeded || result.photoRecSucceeded);
        if (!result.success) {
            result.errorMessage = result.tskErrorMessage.empty()
                ? result.photoRecErrorMessage
                : result.tskErrorMessage;
        } else if (result.tskAttempted && result.photoRecAttempted &&
                   (!result.tskSucceeded || !result.photoRecSucceeded)) {
            result.limitationMessage = "Recovery completed partially: one backend failed.";
        }
        return finalize();
    }

} // namespace Carving
} // namespace Recovery
