#include "PhotoRecCarver.h"
#include "PhotoRecProcessRunner.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace Recovery {
namespace Carving {

    PhotoRecCarver::PhotoRecCarver(
        std::shared_ptr<IPhotoRecProcessRunner> runner,
        std::string executablePath)
        : m_runner(std::move(runner)),
          m_executablePath(std::move(executablePath)) {
        if (!m_runner) {
            m_runner = std::make_shared<PhotoRecProcessRunner>();
        }
        if (m_executablePath.empty()) {
            m_executablePath = kPhotoRecExecutablePath;
        }
    }

    CarvingResult PhotoRecCarver::Carve(const CarvingRequest& request) {
        CarvingResult result;
        result.outputDir = request.outputDir;

        if (request.sourcePath.empty()) {
            result.errorMessage = "Source path is empty.";
            return result;
        }
        if (request.outputDir.empty()) {
            result.errorMessage = "Output directory path is empty.";
            return result;
        }

        std::error_code error;
        if (!fs::exists(request.sourcePath, error)) {
            result.errorMessage = "Source path does not exist.";
            return result;
        }

        if (!ValidateRequestedFileTypes(request.requestedFileTypes,
                                        result.errorMessage)) {
            return result;
        }

        const fs::path outputPath(request.outputDir);
        const fs::path outputParent =
            outputPath.has_parent_path() ? outputPath.parent_path() : fs::path(".");
        fs::create_directories(outputParent, error);
        if (error) {
            result.errorMessage = "Failed to create output parent directory: " + error.message();
            return result;
        }
        if (m_executablePath.empty()) {
            result.errorMessage = "PhotoRec executable path is not configured.";
            return result;
        }

        const PhotoRecProcessResult process =
            m_runner->RunCommand(BuildCommand(m_executablePath, request));
        result.exitCode = process.exitCode;
        if (!process.executed) {
            result.errorMessage = "Failed to execute PhotoRec: " + process.stdErr;
            return result;
        }
        if (process.exitCode != 0) {
            result.errorMessage = "PhotoRec failed with exit code " +
                                  std::to_string(process.exitCode) + ".";
            if (!process.stdOut.empty()) {
                result.errorMessage += " Output: " + process.stdOut;
            }
            return result;
        }

        // PhotoRec appends a numeric suffix to the /d base path. Keep the
        // unsuffixed scan for injected runners that model output directly.
        DiscoverCandidates(request.outputDir, result.candidates);
        for (unsigned int index = 1;; ++index) {
            const fs::path numberedPath =
                fs::path(request.outputDir + "." + std::to_string(index));
            std::error_code numberedError;
            if (!fs::is_directory(numberedPath, numberedError)) {
                break;
            }
            DiscoverCandidates(numberedPath.string(), result.candidates);
        }
        result.candidatesFound = static_cast<uint32_t>(result.candidates.size());
        // CarvingResult has one success flag, so retain the existing adapter
        // convention that success means at least one candidate was recovered.
        result.success = !result.candidates.empty();
        if (!result.success) {
            result.errorMessage =
                "PhotoRec executed successfully, but no recovered files were found.";
        }
        return result;
    }

    std::string PhotoRecCarver::QuoteArgument(const std::string& value) {
        std::string quoted = "\"";
        for (const char character : value) {
            if (character == '"') {
                quoted += '\\';
            }
            quoted += character;
        }
        quoted += '"';
        return quoted;
    }

    std::string PhotoRecCarver::BuildCommand(
        const std::string& executablePath,
        const CarvingRequest& request) {
        std::ostringstream command;
        command << QuoteArgument(executablePath)
                << " /log /d " << QuoteArgument(request.outputDir)
                << " /cmd " << QuoteArgument(request.sourcePath) << " ";
        if (!request.requestedFileTypes.empty()) {
            command << "partition_none,fileopt,everything,disable,";
            for (size_t index = 0; index < request.requestedFileTypes.size(); ++index) {
                if (index > 0) {
                    command << ",";
                }
                std::string normalizedType = request.requestedFileTypes[index];
                std::transform(normalizedType.begin(), normalizedType.end(),
                               normalizedType.begin(),
                               [](unsigned char character) {
                                   return static_cast<char>(std::tolower(character));
                               });
                command << "fileopt," << normalizedType
                        << ",enable";
            }
            command << ",search";
        } else {
            command << "partition_none,search";
        }
        return command.str();
    }

    bool PhotoRecCarver::ValidateRequestedFileTypes(
        const std::vector<std::string>& requestedFileTypes,
        std::string& errorMessage) {
        for (const std::string& requestedType : requestedFileTypes) {
            std::string normalizedType = requestedType;
            std::transform(normalizedType.begin(), normalizedType.end(),
                           normalizedType.begin(),
                           [](unsigned char character) {
                               return static_cast<char>(std::tolower(character));
                           });
            if (normalizedType != "jpg") {
                errorMessage = "Unsupported PhotoRec requested file type: " +
                               requestedType +
                               ". Supported types: jpg.";
                return false;
            }
        }
        return true;
    }

    void PhotoRecCarver::DiscoverCandidates(
        const std::string& outputDir,
        std::vector<CarvingCandidate>& candidates) {
        std::error_code error;
        if (!fs::is_directory(outputDir, error)) {
            return;
        }

        for (fs::recursive_directory_iterator it(outputDir, error), end;
             it != end && !error; it.increment(error)) {
            if (!it->is_regular_file(error)) {
                continue;
            }
            const fs::path path = it->path();
            std::string filename = path.filename().string();
            std::transform(filename.begin(), filename.end(), filename.begin(),
                           [](unsigned char character) {
                               return static_cast<char>(std::tolower(character));
                           });
            if (path.extension() == ".log" || filename == "photorec.log" ||
                filename == "report.xml") {
                continue;
            }
            CarvingCandidate candidate;
            candidate.recoveredPath = path.string();
            candidate.recoveredSize = it->file_size(error);
            candidate.sourceOffset = 0;
            candidate.validationStatus = "UNKNOWN";
            candidate.isValidated = false;
            candidate.fileType = path.has_extension()
                ? path.extension().string().substr(1)
                : "unknown";
            std::transform(candidate.fileType.begin(), candidate.fileType.end(),
                           candidate.fileType.begin(),
                           [](unsigned char character) {
                               return static_cast<char>(std::tolower(character));
                           });
            candidates.push_back(std::move(candidate));
        }
    }

} // namespace Carving
} // namespace Recovery
