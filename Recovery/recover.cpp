#include "recover.h"
#ifdef _WIN32
#include "Acquisition/WindowsReadOnlyStorage.h"
#else
#include "Acquisition/LinuxReadOnlyStorage.h"
#endif
#include "Audit/AuditCollector.h"
#include "Audit/AuditLog.h"
#include "Carving/PhotoRec/PhotoRecCarver.h"
#include "Carving/Verification/VerificationEngine.h"
#include "Core/FileRecord.h"
#include "Core/StorageRegion.h"
#include "TSK/TskFileSystem.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace Recovery {
namespace {

struct ManifestEntry {
    std::string id;
    std::string method;
    std::string status;
    std::string name;
    std::string sourcePath;
    std::string outputPath;
    uint64_t size = 0;
    std::string fileType;
    bool metadataOnly = false;
    Carving::VerificationResult verification;
};

struct RunState {
    std::string diskImage;
    fs::path outputRoot;
    std::vector<ManifestEntry> entries;
    Audit::AuditLog audit;
    std::string metadataError;
    std::string carvingError;
    bool metadataAttempted = false;
    bool metadataSucceeded = false;
    bool carvingAttempted = false;
    bool carvingSucceeded = false;
};

RunState NewRunState(const std::string& diskImage, const fs::path& outputRoot) {
    RunState state;
    state.diskImage = diskImage;
    state.outputRoot = outputRoot;
    return state;
}

fs::path DefaultOutputDirectory() {
    return fs::path("Recovery") / "output";
}

std::string Json(const std::string& value) {
    std::string result = "\"";
    for (const unsigned char character : value) {
        switch (character) {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result += character < 0x20 ? '?' : static_cast<char>(character);
        }
    }
    result += '"';
    return result;
}

std::string RelativePath(const fs::path& root, const fs::path& path) {
    std::error_code error;
    const fs::path absoluteRoot = fs::absolute(root, error);
    const fs::path absolutePath = fs::absolute(path, error);
    const fs::path relative = fs::relative(absolutePath, absoluteRoot, error);
    return error ? path.generic_string() : relative.generic_string();
}

std::string SafeName(const Core::FileRecord& record) {
    std::string name = record.filename.empty() ? "unnamed" : record.filename;
    for (char& character : name) {
        if (character == '/' || character == '\\' || character == ':' ||
            character == '\n' || character == '\r') {
            character = '_';
        }
    }
    return std::to_string(record.id) + "_" + name;
}

void WriteVerification(std::ostream& output,
                       const Carving::VerificationResult& verification) {
    output << "{\"classification\":"
           << Json(Carving::ToString(verification.classification))
           << ",\"detectedType\":" << Json(verification.detectedType)
           << ",\"fileSize\":" << verification.fileSize
           << ",\"sha256\":" << Json(verification.sha256)
           << ",\"score\":" << verification.score
           << ",\"explanation\":" << Json(verification.explanation)
           << ",\"checks\":[";
    for (size_t index = 0; index < verification.checks.size(); ++index) {
        if (index != 0) output << ',';
        const auto& check = verification.checks[index];
        output << "{\"name\":" << Json(check.name)
               << ",\"status\":" << Json(Carving::ToString(check.status))
               << ",\"explanation\":" << Json(check.explanation)
               << ",\"weight\":" << check.weight << '}';
    }
    output << "]}";
}

void WriteRecord(std::ostream& output, const Core::FileRecord& record,
                 const std::string& status, const std::string& outputPath,
                 const Carving::VerificationResult* verification) {
    output << "{\"id\":" << record.id
           << ",\"status\":" << Json(status)
           << ",\"recoveryMethod\":\"TSK_METADATA\""
           << ",\"name\":" << Json(record.filename)
           << ",\"filesystemPath\":" << Json(record.path)
           << ",\"relativeOutputPath\":" << Json(outputPath)
           << ",\"size\":" << record.size
           << ",\"deleted\":" << (record.deleted ? "true" : "false")
           << ",\"orphaned\":" << (record.orphaned ? "true" : "false")
           << ",\"allocated\":" << (record.allocated ? "true" : "false")
           << ",\"filesystemRecordId\":" << record.filesystemRecordId
           << ",\"metadataOnly\":" << (outputPath.empty() ? "true" : "false");
    if (verification != nullptr) {
        output << ",\"verification\":";
        WriteVerification(output, *verification);
    }
    output << '}';
}

bool WriteMetadataJson(const fs::path& outputRoot,
                       const std::vector<std::pair<Core::FileRecord,
                                                   std::pair<std::string,
                                                             std::string>>>& records) {
    std::ofstream output(outputRoot / "metadata" / "metadata.json");
    if (!output) return false;
    output << "{\"records\":[";
    for (size_t index = 0; index < records.size(); ++index) {
        if (index != 0) output << ',';
        const auto& record = records[index].first;
        const auto& details = records[index].second;
        WriteRecord(output, record, details.first, details.second, nullptr);
    }
    output << "]}\n";
    return true;
}

void WriteAudit(const fs::path& outputRoot, const Audit::AuditLog& audit) {
    std::ofstream output(outputRoot / "audit" / "events.jsonl");
    if (!output) return;
    for (const auto& event : audit.Events()) {
        output << "{\"type\":" << static_cast<int>(event.type)
               << ",\"sessionId\":" << Json(event.sessionId)
               << ",\"message\":" << Json(event.message);
        if (event.candidateId.has_value()) {
            output << ",\"candidateId\":" << *event.candidateId;
        }
        if (event.dataLength.has_value()) {
            output << ",\"dataLength\":" << *event.dataLength;
        }
        output << "}\n";
    }
}

void WriteVerificationResults(const fs::path& outputRoot,
                              const std::vector<ManifestEntry>& entries) {
    std::ofstream output(outputRoot / "verification" / "results.json");
    if (!output) return;
    output << "{\"results\":[";
    for (size_t index = 0; index < entries.size(); ++index) {
        if (index != 0) output << ',';
        const auto& entry = entries[index];
        output << "{\"id\":" << Json(entry.id)
               << ",\"relativeOutputPath\":" << Json(entry.outputPath)
               << ",\"recoveryMethod\":" << Json(entry.method)
               << ",\"result\":";
        WriteVerification(output, entry.verification);
        output << '}';
    }
    output << "]}\n";
}

bool RecoverMetadataInternal(const std::string& diskImage, RunState& state) {
    state.metadataAttempted = true;
    Audit::AuditCollector audit(state.audit);
    const std::string session = "recovery";
    audit.RecordBackendStarted(session, Core::RecoveryBackend::TSK_METADATA,
                               "Starting filesystem metadata recovery.");

#ifdef _WIN32
    Acquisition::WindowsReadOnlyStorage storage;
#else
    Acquisition::LinuxReadOnlyStorage storage;
#endif
    if (!storage.Open(diskImage)) {
        state.metadataError = "Failed to open source image read-only.";
        audit.RecordBackendFailed(session, Core::RecoveryBackend::TSK_METADATA,
                                  state.metadataError);
        return false;
    }
    TSK::TskFileSystem filesystem(storage,
                                  Core::StorageRegion(0, storage.GetSize()));
    if (!filesystem.IsTskAvailable() || !filesystem.Mount()) {
        state.metadataError = filesystem.GetLastError();
        if (state.metadataError.empty()) state.metadataError = "TSK metadata recovery failed.";
        audit.RecordBackendFailed(session, Core::RecoveryBackend::TSK_METADATA,
                                  state.metadataError);
        return false;
    }

    std::vector<Core::FileRecord> records;
    if (!filesystem.EnumerateFiles(records)) {
        state.metadataError = "TSK file enumeration failed.";
        audit.RecordBackendFailed(session, Core::RecoveryBackend::TSK_METADATA,
                                  state.metadataError);
        return false;
    }

    std::vector<std::pair<Core::FileRecord, std::pair<std::string, std::string>>> persisted;
    for (auto& record : records) {
        record.recoveryBackend = Core::RecoveryBackend::TSK_METADATA;
        record.sourcePath = diskImage;
        std::string status = "metadata-only";
        std::string relativePath;
        std::vector<uint8_t> bytes;
        if (!record.isDirectory && filesystem.ReadFile(record.id, bytes)) {
            const fs::path destination = state.outputRoot / "metadata" / "recovered" /
                                         SafeName(record);
            std::ofstream file(destination, std::ios::binary);
            if (file && (bytes.empty() || file.write(
                    reinterpret_cast<const char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size())))) {
                status = "recovered";
                relativePath = RelativePath(state.outputRoot, destination);
                Carving::Verification::VerificationEngine engine;
                const auto verification = engine.VerifyBytes(bytes, record.extension);
                Audit::AuditCollector(state.audit).RecordVerificationCompleted(
                    session, verification, record.id);
            }
        }
        persisted.emplace_back(record, std::make_pair(status, relativePath));
        ManifestEntry entry;
        entry.id = "metadata-" + std::to_string(record.id);
        entry.method = "TSK_METADATA";
        entry.status = status;
        entry.name = record.filename;
        entry.sourcePath = record.path;
        entry.outputPath = relativePath;
        entry.size = record.size;
        entry.fileType = record.extension;
        entry.metadataOnly = status == "metadata-only";
        if (status == "recovered") {
            Carving::Verification::VerificationEngine engine;
            std::vector<uint8_t> recoveredBytes;
            if (filesystem.ReadFile(record.id, recoveredBytes)) {
                entry.verification = engine.VerifyBytes(recoveredBytes, record.extension);
                entry.verification.path = relativePath;
            }
        }
        state.entries.push_back(std::move(entry));
    }
    if (!WriteMetadataJson(state.outputRoot, persisted)) {
        state.metadataError = "Failed to write metadata/metadata.json.";
        return false;
    }
    state.metadataSucceeded = true;
    audit.RecordBackendCompleted(session, Core::RecoveryBackend::TSK_METADATA,
                                 true, "Metadata recovery completed.");
    return true;
}

bool RecoverCarvingInternal(const std::string& diskImage, RunState& state) {
    state.carvingAttempted = true;
    Audit::AuditCollector audit(state.audit);
    const std::string session = "recovery";
    audit.RecordBackendStarted(session, Core::RecoveryBackend::PHOTOREC_CARVING,
                               "Starting file carving.");
    Carving::CarvingRequest request;
    request.sourcePath = diskImage;
    request.outputDir = (state.outputRoot / "carved" / "recovered").string();
    Carving::PhotoRecCarver carver;
    const Carving::CarvingResult result = carver.Carve(request);
    state.carvingError = result.errorMessage;
    for (size_t index = 0; index < result.candidates.size(); ++index) {
        const auto& candidate = result.candidates[index];
        ManifestEntry entry;
        entry.id = "carving-" + std::to_string(index);
        entry.method = "PHOTOREC_CARVING";
        entry.status = "recovered";
        entry.name = fs::path(candidate.recoveredPath).filename().string();
        entry.outputPath = RelativePath(state.outputRoot, candidate.recoveredPath);
        entry.size = candidate.recoveredSize;
        entry.fileType = candidate.fileType;
        entry.verification = candidate.verification;
        state.entries.push_back(std::move(entry));
        audit.RecordCandidateRecovered(session, candidate, index);
        audit.RecordVerificationCompleted(session, candidate.verification, index);
    }
    state.carvingSucceeded = result.success;
    audit.RecordBackendCompleted(session, Core::RecoveryBackend::PHOTOREC_CARVING,
                                result.success, result.success
                                    ? "Carving completed." : result.errorMessage);
    return result.success;
}

void WriteManifest(const RunState& state) {
    std::ofstream output(state.outputRoot / "recovery_result.json");
    if (!output) return;
    output << "{\"sourcePath\":" << Json(state.diskImage)
           << ",\"success\":"
           << ((state.metadataSucceeded || state.carvingSucceeded) ? "true" : "false")
           << ",\"metadataAttempted\":" << (state.metadataAttempted ? "true" : "false")
           << ",\"carvingAttempted\":" << (state.carvingAttempted ? "true" : "false")
           << ",\"errors\":[";
    bool hasError = false;
    for (const std::string& error : {state.metadataError, state.carvingError}) {
        if (error.empty()) continue;
        if (hasError) output << ',';
        output << Json(error);
        hasError = true;
    }
    output << "],\"files\":[";
    for (size_t index = 0; index < state.entries.size(); ++index) {
        if (index != 0) output << ',';
        const auto& entry = state.entries[index];
        output << "{\"id\":" << Json(entry.id)
               << ",\"recoveryMethod\":" << Json(entry.method)
               << ",\"status\":" << Json(entry.status)
               << ",\"name\":" << Json(entry.name)
               << ",\"sourcePath\":" << Json(entry.sourcePath)
               << ",\"relativeOutputPath\":" << Json(entry.outputPath)
               << ",\"size\":" << entry.size
               << ",\"fileType\":" << Json(entry.fileType)
               << ",\"metadataOnly\":" << (entry.metadataOnly ? "true" : "false")
               << ",\"verification\":";
        WriteVerification(output, entry.verification);
        output << '}';
    }
    output << "]}\n";
}

} // namespace

bool RecoverMetadata(const std::string& diskImage,
                     const fs::path& outputRoot) {
    fs::create_directories(outputRoot / "metadata" / "recovered");
    fs::create_directories(outputRoot / "audit");
    fs::create_directories(outputRoot / "verification");
    RunState state = NewRunState(diskImage, outputRoot);
    const bool success = RecoverMetadataInternal(diskImage, state);
    WriteAudit(outputRoot, state.audit);
    WriteVerificationResults(outputRoot, state.entries);
    WriteManifest(state);
    return success;
}

bool RecoverCarving(const std::string& diskImage,
                    const fs::path& outputRoot) {
    fs::create_directories(outputRoot / "carved");
    fs::create_directories(outputRoot / "audit");
    fs::create_directories(outputRoot / "verification");
    RunState state = NewRunState(diskImage, outputRoot);
    const bool success = RecoverCarvingInternal(diskImage, state);
    WriteAudit(outputRoot, state.audit);
    WriteVerificationResults(outputRoot, state.entries);
    WriteManifest(state);
    return success;
}

int RunRecoveryCommand(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: " << argv[0] << " <disk_image> [output_dir]\n";
        return 2;
    }
    const fs::path outputRoot = argc == 3 ? fs::path(argv[2]) : DefaultOutputDirectory();
    fs::create_directories(outputRoot / "metadata" / "recovered");
    fs::create_directories(outputRoot / "carved");
    fs::create_directories(outputRoot / "audit");
    fs::create_directories(outputRoot / "verification");
    RunState state = NewRunState(argv[1], outputRoot);
    RecoverMetadataInternal(argv[1], state);
    RecoverCarvingInternal(argv[1], state);
    WriteAudit(outputRoot, state.audit);
    WriteVerificationResults(outputRoot, state.entries);
    WriteManifest(state);
    std::cout << "Recovery output: " << outputRoot << '\n';
    return (state.metadataSucceeded || state.carvingSucceeded) ? 0 : 1;
}

} // namespace Recovery

int main(int argc, char** argv) {
    return Recovery::RunRecoveryCommand(argc, argv);
}