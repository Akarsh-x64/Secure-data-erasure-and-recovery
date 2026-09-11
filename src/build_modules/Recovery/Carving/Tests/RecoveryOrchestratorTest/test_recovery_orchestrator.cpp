#include "../../Core/RecoveryOrchestrator.h"
#include "../../Core/IMetadataRecoveryBackend.h"
#include "../../../Audit/EvidenceManifest.h"
#include "../../Verification/VerificationEngine.h"
#include "../../../TSK/TskFeature.h"

#include <cassert>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <memory>
#include <vector>

namespace {

class MemoryStorage : public Recovery::Core::IReadOnlyStorage {
public:
    explicit MemoryStorage(std::vector<uint8_t> bytes)
        : m_bytes(std::move(bytes)) {}

    bool Open(const std::string&) override {
        opened = true;
        return true;
    }

    void Close() override {
        closed = true;
    }

    bool Read(uint64_t offset, uint32_t size, void* buffer) override {
        if (offset + size > m_bytes.size()) {
            return false;
        }
        std::memcpy(buffer, m_bytes.data() + offset, size);
        return true;
    }

    uint64_t GetSize() const override { return m_bytes.size(); }
    uint32_t GetSectorSize() const override { return 512; }

    bool opened = false;
    bool closed = false;

private:
    std::vector<uint8_t> m_bytes;
};

class FakeCarver : public Recovery::Carving::ICarver {
public:
    explicit FakeCarver(bool succeeds)
        : m_succeeds(succeeds) {}

    Recovery::Carving::CarvingResult Carve(
        const Recovery::Carving::CarvingRequest& request) override {
        received = request;
        called = true;
        Recovery::Carving::CarvingResult result;
        result.success = m_succeeds;
        result.outputDir = request.outputDir;
        if (m_succeeds) {
            Recovery::Carving::CarvingCandidate candidate;
            candidate.recoveredPath =
                (std::filesystem::temp_directory_path() / "recovered.jpg").string();
            std::ofstream output(candidate.recoveredPath, std::ios::binary);
            const unsigned char jpeg[] = {0xff, 0xd8, 0xff, 0xd9};
            output.write(reinterpret_cast<const char*>(jpeg), sizeof(jpeg));
            candidate.recoveredSize = sizeof(jpeg);
            candidate.fileType = "jpg";
            result.candidates.push_back(candidate);
            result.candidatesFound = 1;
        }
        if (!m_succeeds) {
            result.errorMessage = "synthetic carving failure";
        }
        return result;
    }

    Recovery::Carving::CarvingRequest received;
    bool called = false;

private:
    bool m_succeeds;
};

class FakeMetadataBackend : public Recovery::Carving::IMetadataRecoveryBackend {
public:
    FakeMetadataBackend(bool available, bool succeeds)
        : m_available(available), m_succeeds(succeeds) {}

    bool IsAvailable() const override { return m_available; }
    std::string CapabilityMessage() const override {
        return m_available ? "TSK_AVAILABLE" : "TSK_UNAVAILABLE";
    }
    bool Recover(Recovery::Core::IReadOnlyStorage&,
                 const Recovery::Core::StorageRegion&,
                 std::vector<Recovery::Core::FileRecord>& records,
                 std::string& errorMessage) override {
        if (!m_succeeds) {
            errorMessage = "synthetic TSK failure";
            return false;
        }
        Recovery::Core::FileRecord record;
        record.path = "/metadata.jpg";
        record.filename = "metadata.jpg";
        record.extension = "jpg";
        record.size = 4;
        record.dataRangeStatus = Recovery::Core::DataRangeStatus::Complete;
        record.dataRanges.emplace_back(1024, 4);
        records.push_back(record);
        return true;
    }

private:
    bool m_available;
    bool m_succeeds;
};

std::vector<uint8_t> BuildMBRImage() {
    std::vector<uint8_t> image(4096, 0);
    image[510] = 0x55;
    image[511] = 0xAA;
    image[0x1BE + 4] = 0x07;
    image[0x1BE + 8] = 1;
    image[0x1BE + 12] = 2;
    image[1024] = 0xff;
    image[1025] = 0xd8;
    image[1026] = 0xff;
    image[1027] = 0xd9;
    return image;
}

void TestInvalidSourcePath() {
    Recovery::Carving::RecoveryOrchestrator orchestrator;
    Recovery::Carving::RecoveryRequest request;
    request.outputDir = "output";

    const auto result = orchestrator.Recover(request);
    assert(!result.success);
    assert(!result.errorMessage.empty());
    assert(result.auditLog.Events().front().type ==
           Recovery::Audit::AuditEventType::RecoveryStarted);
    assert(result.auditLog.Events().back().type ==
           Recovery::Audit::AuditEventType::RecoveryFailed);
}

void TestPartitionPropagationAndCarving() {
    auto storage = std::make_shared<MemoryStorage>(BuildMBRImage());
    auto carver = std::make_shared<FakeCarver>(true);
    Recovery::Carving::RecoveryOrchestrator orchestrator(storage, carver);

    Recovery::Carving::RecoveryRequest request;
    request.sourcePath = "synthetic.img";
    request.outputDir = "output";
    request.selectedPartitionIndex = 0;

    const auto result = orchestrator.Recover(request);
    assert(result.success);
    assert(storage->opened);
    assert(storage->closed);
    assert(result.detectedPartitionScheme.has_value());
    assert(*result.detectedPartitionScheme ==
           Recovery::Core::PartitionScheme::MBR);
    assert(result.partitions.size() == 1);
    assert(result.selectedPartition.has_value());
    assert(carver->received.sourceOffset == 512);
    assert(carver->received.sourceLength == 1024);
    assert(!result.limitationMessage.empty());
}

void TestCarvingFailureIsPropagated() {
    auto storage = std::make_shared<MemoryStorage>(BuildMBRImage());
    auto carver = std::make_shared<FakeCarver>(false);
    Recovery::Carving::RecoveryOrchestrator orchestrator(storage, carver);

    Recovery::Carving::RecoveryRequest request;
    request.sourcePath = "synthetic.img";
    request.outputDir = "output";

    const auto result = orchestrator.Recover(request);
    assert(!result.success);
    assert(result.errorMessage == "synthetic carving failure");
}

void TestTskModeDoesNotInvokeCarverWhenUnavailable() {
    auto storage = std::make_shared<MemoryStorage>(BuildMBRImage());
    auto carver = std::make_shared<FakeCarver>(true);
    Recovery::Carving::RecoveryOrchestrator orchestrator(storage, carver);

    Recovery::Carving::RecoveryRequest request;
    request.sourcePath = "synthetic.img";
    request.outputDir = "output";
    request.selectedPartitionIndex = 0;
    request.recoveryMethod = Recovery::Core::RecoveryMethod::Metadata;

    const auto result = orchestrator.Recover(request);
    assert(!carver->called);
    assert(result.recoveryMethod == Recovery::Core::RecoveryMethod::Metadata);
    assert(result.selectedPartition.has_value());
#if RECOVERY_HAS_LIBTSK
    assert(result.capabilityAvailable);
    assert(!result.success);
#else
    assert(!result.capabilityAvailable);
    assert(result.capabilityMessage.find("TSK_UNAVAILABLE") != std::string::npos);
#endif
}

void TestTskModeRejectsMissingPartition() {
    auto storage = std::make_shared<MemoryStorage>(BuildMBRImage());
    auto carver = std::make_shared<FakeCarver>(true);
    Recovery::Carving::RecoveryOrchestrator orchestrator(storage, carver);

    Recovery::Carving::RecoveryRequest request;
    request.sourcePath = "synthetic.img";
    request.outputDir = "output";
    request.recoveryMethod = Recovery::Core::RecoveryMethod::Metadata;

    const auto result = orchestrator.Recover(request);
    assert(!result.success);
    assert(result.errorMessage.find("selected partition") != std::string::npos);
    assert(!carver->called);
}

void TestCombinedFailureIsolation() {
    auto storage = std::make_shared<MemoryStorage>(BuildMBRImage());
    auto carver = std::make_shared<FakeCarver>(true);
    auto metadata = std::make_shared<FakeMetadataBackend>(false, false);
    Recovery::Carving::RecoveryOrchestrator orchestrator(storage, carver, metadata);

    Recovery::Carving::RecoveryRequest request;
    request.sourcePath = "synthetic.img";
    request.outputDir = "output";
    request.selectedPartitionIndex = 0;
    request.recoveryMethod = Recovery::Core::RecoveryMethod::Combined;

    const auto result = orchestrator.Recover(request);
    assert(result.success);
    assert(!result.tskSucceeded);
    assert(result.photoRecSucceeded);
    assert(result.metadataRecords.empty());
    assert(result.carvingResult.candidates.size() == 1);
    assert(result.carvingResult.candidates[0].verification.classification ==
           Recovery::Carving::VerificationClassification::VALID);
    assert(result.carvingResult.candidates[0].recoveryBackend ==
           Recovery::Core::RecoveryBackend::PHOTOREC_CARVING);
    assert(result.evidenceRecords.size() == 1);
    assert(result.evidenceRecords[0].recoveryBackend ==
           Recovery::Core::RecoveryBackend::PHOTOREC_CARVING);
    assert(!result.evidenceRecords[0].sourceOffset.has_value());
    assert(result.carvingResult.candidates[0].verification.classification ==
           Recovery::Carving::VerificationClassification::VALID);
    assert(!result.carvingResult.candidates[0].verification.sha256.empty());
    assert(result.carvingResult.candidates[0].verification.fileSize ==
           result.carvingResult.candidates[0].recoveredSize);
    assert(!result.carvingResult.candidates[0].verification.detectedType.empty());
    assert(!result.carvingResult.candidates[0].verification.checks.empty());
    assert(!result.evidenceManifest.empty());
    assert(result.evidenceManifestHash ==
           Recovery::Audit::EvidenceManifest::Hash(result.evidenceRecords));
    assert(result.auditLog.Events().front().type ==
           Recovery::Audit::AuditEventType::RecoveryStarted);
    assert(result.auditLog.Events().back().type ==
           Recovery::Audit::AuditEventType::RecoveryCompleted);
    bool tskFailed = false;
    bool photoRecStarted = false;
    bool candidateRecovered = false;
    bool verificationCompleted = false;
    for (const auto& event : result.auditLog.Events()) {
        tskFailed = tskFailed ||
            event.type == Recovery::Audit::AuditEventType::BackendFailed &&
            event.backend == Recovery::Core::RecoveryBackend::TSK_METADATA;
        photoRecStarted = photoRecStarted ||
            event.type == Recovery::Audit::AuditEventType::BackendStarted &&
            event.backend == Recovery::Core::RecoveryBackend::PHOTOREC_CARVING;
        candidateRecovered = candidateRecovered ||
            event.type == Recovery::Audit::AuditEventType::CandidateRecovered;
        verificationCompleted = verificationCompleted ||
            event.type == Recovery::Audit::AuditEventType::VerificationCompleted;
    }
    assert(tskFailed);
    assert(photoRecStarted);
    assert(candidateRecovered);
    assert(verificationCompleted);
    for (const auto& event : result.auditLog.Events()) {
        if (event.type == Recovery::Audit::AuditEventType::CandidateRecovered) {
            assert(!event.sourceOffset.has_value());
        }
    }
}

void TestCombinedPreservesMetadataWhenCarvingFails() {
    auto storage = std::make_shared<MemoryStorage>(BuildMBRImage());
    auto carver = std::make_shared<FakeCarver>(false);
    auto metadata = std::make_shared<FakeMetadataBackend>(true, true);
    Recovery::Carving::RecoveryOrchestrator orchestrator(storage, carver, metadata);

    Recovery::Carving::RecoveryRequest request;
    request.sourcePath = "synthetic.img";
    request.outputDir = "output";
    request.selectedPartitionIndex = 0;
    request.recoveryMethod = Recovery::Core::RecoveryMethod::Combined;

    const auto result = orchestrator.Recover(request);
    assert(result.success);
    assert(result.tskSucceeded);
    assert(!result.photoRecSucceeded);
    assert(result.metadataRecords.size() == 1);
    assert(result.metadataVerificationResults.size() == 1);
    assert(result.metadataVerificationResults[0].classification ==
           Recovery::Carving::VerificationClassification::VALID);
    assert(result.metadataRecords[0].recoveryBackend ==
           Recovery::Core::RecoveryBackend::TSK_METADATA);
    assert(result.evidenceRecords.size() == 1);
    assert(result.evidenceRecords[0].recoveryBackend ==
           Recovery::Core::RecoveryBackend::TSK_METADATA);
}

void TestCombinedProvenance() {
    auto storage = std::make_shared<MemoryStorage>(BuildMBRImage());
    auto carver = std::make_shared<FakeCarver>(true);
    auto metadata = std::make_shared<FakeMetadataBackend>(true, true);
    Recovery::Carving::RecoveryOrchestrator orchestrator(storage, carver, metadata);

    Recovery::Carving::RecoveryRequest request;
    request.sourcePath = "synthetic.img";
    request.outputDir = "output";
    request.selectedPartitionIndex = 0;
    request.recoveryMethod = Recovery::Core::RecoveryMethod::Combined;

    const auto result = orchestrator.Recover(request);
    assert(result.success);
    assert(result.metadataRecords[0].sourcePath == request.sourcePath);
    assert(result.metadataRecords[0].partitionIndex == 0);
    assert(result.carvingResult.candidates[0].sourcePath == request.sourcePath);
    assert(result.carvingResult.candidates[0].partitionIndex == 0);
    assert(!result.carvingResult.candidates[0].sourceOffsetKnown);
}

void TestAuditRespectsDisabledVerification() {
    auto storage = std::make_shared<MemoryStorage>(BuildMBRImage());
    auto carver = std::make_shared<FakeCarver>(true);
    Recovery::Carving::RecoveryOrchestrator orchestrator(storage, carver);

    Recovery::Carving::RecoveryRequest request;
    request.sourcePath = "synthetic.img";
    request.outputDir = "output";
    request.recoveryMethod = Recovery::Core::RecoveryMethod::Carving;
    request.verifyResults = false;

    const auto result = orchestrator.Recover(request);
    assert(result.success);
    assert(result.evidenceRecords.size() == 1);
    for (const auto& event : result.auditLog.Events()) {
        assert(event.type != Recovery::Audit::AuditEventType::VerificationCompleted);
    }
}

void TestVerificationClassificationsAndFailureRetention() {
    namespace fs = std::filesystem;
    const fs::path corrupt = fs::temp_directory_path() / "corrupt.jpg";
    std::ofstream(corrupt, std::ios::binary)
        << static_cast<char>(0xff) << static_cast<char>(0xd8);

    Recovery::Carving::Verification::VerificationEngine engine;
    const auto corruptResult = engine.VerifyPath(corrupt.string(), "jpg");
    assert(corruptResult.classification ==
           Recovery::Carving::VerificationClassification::PARTIAL);

    const auto unknownResult =
        engine.VerifyBytes(std::vector<uint8_t>{1, 2, 3}, "bin");
    assert(unknownResult.classification ==
           Recovery::Carving::VerificationClassification::UNKNOWN);

    fs::remove(corrupt);
}

} // namespace

int main() {
    TestInvalidSourcePath();
    TestPartitionPropagationAndCarving();
    TestCarvingFailureIsPropagated();
    TestTskModeDoesNotInvokeCarverWhenUnavailable();
    TestTskModeRejectsMissingPartition();
    TestCombinedFailureIsolation();
    TestCombinedPreservesMetadataWhenCarvingFails();
    TestCombinedProvenance();
    TestAuditRespectsDisabledVerification();
    TestVerificationClassificationsAndFailureRetention();
    return 0;
}
