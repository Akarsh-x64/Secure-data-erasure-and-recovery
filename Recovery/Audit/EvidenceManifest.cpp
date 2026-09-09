#include "EvidenceManifest.h"

#include "../Carving/Verification/Sha256.h"

#include <iomanip>
#include <locale>
#include <sstream>

namespace Recovery {
namespace Audit {
    namespace {
        std::string EncodeString(const std::string& value) {
            std::ostringstream out;
            out << "str:" << value.size() << ":"
                << std::hex << std::setfill('0');
            for (unsigned char byte : value) {
                out << std::setw(2) << static_cast<unsigned int>(byte);
            }
            return out.str();
        }

        std::string OptionalNumber(const std::optional<uint64_t>& value) {
            return value.has_value() ? "v:" + std::to_string(*value) : "-";
        }

        std::string OptionalIndex(const std::optional<uint32_t>& value) {
            return value.has_value() ? "v:" + std::to_string(*value) : "-";
        }

        const char* BackendName(Core::RecoveryBackend value) {
            switch (value) {
            case Core::RecoveryBackend::Unknown: return "UNKNOWN";
            case Core::RecoveryBackend::TSK_METADATA: return "TSK_METADATA";
            case Core::RecoveryBackend::PHOTOREC_CARVING:
                return "PHOTOREC_CARVING";
            }
            return "UNKNOWN";
        }

        const char* FilesystemName(Core::FileSystemType value) {
            switch (value) {
            case Core::FileSystemType::Unknown: return "UNKNOWN";
            case Core::FileSystemType::NTFS: return "NTFS";
            case Core::FileSystemType::ExFAT: return "EXFAT";
            case Core::FileSystemType::FAT32: return "FAT32";
            case Core::FileSystemType::EXT2: return "EXT2";
            case Core::FileSystemType::EXT3: return "EXT3";
            case Core::FileSystemType::EXT4: return "EXT4";
            case Core::FileSystemType::XFS: return "XFS";
            case Core::FileSystemType::HFSPlus: return "HFSPLUS";
            case Core::FileSystemType::APFS: return "APFS";
            }
            return "UNKNOWN";
        }

        const char* ClassificationName(
            Carving::VerificationClassification value) {
            return Carving::ToString(value);
        }

        const char* CheckStatusName(Carving::CheckStatus value) {
            return Carving::ToString(value);
        }

        std::string OptionalClassification(
            const std::optional<Carving::VerificationClassification>& value) {
            return value.has_value()
                ? "v:" + std::string(ClassificationName(*value)) : "-";
        }

        std::string OptionalFilesystem(
            const std::optional<Core::FileSystemType>& value) {
            return value.has_value()
                ? "v:" + std::string(FilesystemName(*value)) : "-";
        }

        void AddField(std::ostringstream& out, const char* name,
                      const std::string& value) {
            out << name << "=" << value << "\n";
        }

        void AddRecord(std::ostringstream& out, const EvidenceRecord& record,
                       std::size_t index) {
            out << "record[" << index << "]\n";
            AddField(out, "evidenceId", EncodeString(record.evidenceId));
            AddField(out, "recoveryBackend", BackendName(record.recoveryBackend));
            AddField(out, "sourcePath", EncodeString(record.sourcePath));
            AddField(out, "partitionIndex", OptionalIndex(record.partitionIndex));
            AddField(out, "partitionOffset", OptionalNumber(record.partitionOffset));
            AddField(out, "partitionSize", OptionalNumber(record.partitionSize));
            AddField(out, "filesystem", OptionalFilesystem(record.filesystem));
            AddField(out, "name", EncodeString(record.name));
            AddField(out, "path", EncodeString(record.path));
            AddField(out, "fileType", EncodeString(record.fileType));
            AddField(out, "recoveredSize", OptionalNumber(record.recoveredSize));
            AddField(out, "sourceOffset", OptionalNumber(record.sourceOffset));
            AddField(out, "detectedType", EncodeString(record.detectedType));
            AddField(out, "verificationClassification",
                     OptionalClassification(record.verificationClassification));
            if (record.verificationScore.has_value()) {
                std::ostringstream score;
                score.imbue(std::locale::classic());
                score << std::setprecision(17) << *record.verificationScore;
                AddField(out, "verificationScore", "v:" + score.str());
            } else {
                AddField(out, "verificationScore", "-");
            }
            AddField(out, "sha256", EncodeString(record.sha256));
            AddField(out, "verificationExplanation",
                     EncodeString(record.verificationExplanation));
            out << "verificationChecks.count=" << record.verificationChecks.size()
                << "\n";
            for (std::size_t checkIndex = 0;
                 checkIndex < record.verificationChecks.size(); ++checkIndex) {
                const auto& check = record.verificationChecks[checkIndex];
                out << "verificationCheck[" << checkIndex << "]\n";
                AddField(out, "name", EncodeString(check.name));
                AddField(out, "status", CheckStatusName(check.status));
                AddField(out, "explanation", EncodeString(check.explanation));
                std::ostringstream weight;
                weight.imbue(std::locale::classic());
                weight << std::setprecision(17) << check.weight;
                AddField(out, "weight", weight.str());
            }
        }
    }

    std::string EvidenceManifest::Canonicalize(
        const std::vector<EvidenceRecord>& records) {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << "evidence_manifest_version=1\n";
        out << "record_count=" << records.size() << "\n";
        for (std::size_t index = 0; index < records.size(); ++index) {
            AddRecord(out, records[index], index);
        }
        return out.str();
    }

    std::string EvidenceManifest::Hash(
        const std::vector<EvidenceRecord>& records) {
        const std::string canonical = Canonicalize(records);
        return Carving::Verification::Sha256::Compute(
            reinterpret_cast<const uint8_t*>(canonical.data()), canonical.size());
    }
} // namespace Audit
} // namespace Recovery
