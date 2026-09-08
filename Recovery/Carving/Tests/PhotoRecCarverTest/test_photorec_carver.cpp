#include "../../PhotoRec/PhotoRecCarver.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <cstdlib>
#include <vector>

namespace fs = std::filesystem;
using namespace Recovery::Carving;

namespace {

class MockRunner : public IPhotoRecProcessRunner {
public:
    PhotoRecProcessResult response;
    std::string outputDir;
    std::string command;
    bool createRecoveredFile = false;

    PhotoRecProcessResult RunCommand(const std::string& requestedCommand,
                                     const std::vector<std::string>&) override {
        command = requestedCommand;
        if (createRecoveredFile) {
            const fs::path recoveredDir = fs::path(outputDir) / "recup_dir.1";
            fs::create_directories(recoveredDir);
            std::ofstream(recoveredDir / "f000001.jpg") << "recovered";
        }
        return response;
    }
};

CarvingRequest ValidRequest(const fs::path& root) {
    CarvingRequest request;
    request.sourcePath = (root / "source.img").string();
    request.outputDir = (root / "output").string();
    std::ofstream(request.sourcePath) << "source";
    return request;
}

void TestMissingSource() {
    PhotoRecCarver carver(nullptr, "photorec");
    CarvingRequest request;
    request.outputDir = "photorec-test-output";
    const CarvingResult result = carver.Carve(request);
    assert(!result.success);
    assert(!result.errorMessage.empty());
}

void TestMockRecovery() {
    const fs::path root = fs::temp_directory_path() / "photorec-carver-mock";
    fs::remove_all(root);
    fs::create_directories(root);
    auto runner = std::make_shared<MockRunner>();
    runner->response.executed = true;
    runner->response.exitCode = 0;
    runner->outputDir = (root / "output").string();
    runner->createRecoveredFile = true;
    CarvingRequest request = ValidRequest(root);
    request.requestedFileTypes = {"jpg"};
    PhotoRecCarver carver(runner, "photorec");
    const CarvingResult result = carver.Carve(request);
    assert(result.success);
    assert(result.candidatesFound == 1);
    assert(result.candidates[0].fileType == "jpg");
    assert(result.candidates[0].sourceOffset == 0);
    assert(runner->command.find(" /log /d ") != std::string::npos);
    assert(runner->command.find(" /cmd ") != std::string::npos);
    assert(runner->command.find("fileopt,everything,disable") != std::string::npos);
    assert(runner->command.find("fileopt,jpg,enable") != std::string::npos);
    assert(runner->command.find(
               "partition_none,fileopt,everything,disable,fileopt,jpg,enable,search") !=
           std::string::npos);
    fs::remove_all(root);
}

void TestEmptyRequestedFileTypesUsesDefaultCommand() {
    const fs::path root = fs::temp_directory_path() / "photorec-default-types";
    fs::remove_all(root);
    fs::create_directories(root);
    auto runner = std::make_shared<MockRunner>();
    runner->response.executed = true;
    runner->response.exitCode = 0;
    CarvingRequest request = ValidRequest(root);
    request.requestedFileTypes.clear();
    PhotoRecCarver carver(runner, "photorec");
    carver.Carve(request);
    assert(runner->command.find("fileopt,") == std::string::npos);
    assert(runner->command.find(" partition_none,search") != std::string::npos);
    fs::remove_all(root);
}

void TestUnsupportedRequestedFileType() {
    const fs::path root = fs::temp_directory_path() / "photorec-unsupported-type";
    fs::remove_all(root);
    fs::create_directories(root);
    auto runner = std::make_shared<MockRunner>();
    runner->response.executed = true;
    runner->response.exitCode = 0;
    CarvingRequest request = ValidRequest(root);
    request.requestedFileTypes = {"pdf"};
    const CarvingResult result = PhotoRecCarver(runner, "photorec").Carve(request);
    assert(!result.success);
    assert(result.errorMessage.find("Unsupported PhotoRec requested file type") !=
           std::string::npos);
    assert(runner->command.empty());
    fs::remove_all(root);
}

void TestProcessFailure(bool executed, int exitCode, bool expectCandidate,
                        bool expectedSuccess) {
    const fs::path root = fs::temp_directory_path() /
                          (executed ? "photorec-process-failure" :
                                      "photorec-launch-failure");
    fs::remove_all(root);
    fs::create_directories(root);
    auto runner = std::make_shared<MockRunner>();
    runner->response.executed = executed;
    runner->response.exitCode = exitCode;
    runner->outputDir = (root / "output").string();
    runner->createRecoveredFile = expectCandidate;
    const CarvingResult result = PhotoRecCarver(runner, "photorec").Carve(ValidRequest(root));
    assert(result.success == expectedSuccess);
    fs::remove_all(root);
}

void TestEmptySuccessfulRun() {
    const fs::path root = fs::temp_directory_path() / "photorec-empty";
    fs::remove_all(root);
    fs::create_directories(root);
    auto runner = std::make_shared<MockRunner>();
    runner->response.executed = true;
    runner->response.exitCode = 0;
    const CarvingResult result =
        PhotoRecCarver(runner, "photorec").Carve(ValidRequest(root));
    assert(!result.success);
    assert(result.candidatesFound == 0);
    assert(result.errorMessage.find("executed successfully") != std::string::npos);
    fs::remove_all(root);
}

std::vector<uint8_t> ReadBytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(input)),
                                std::istreambuf_iterator<char>());
}

fs::path LocateRealTestImage(const fs::path& executablePath) {
    const char* configuredImage = std::getenv("PHOTOREC_TEST_IMAGE");
    if (configuredImage != nullptr && *configuredImage != '\0') {
        return configuredImage;
    }
    return executablePath.parent_path() / "test.img";
}

std::string QuoteCommandArgument(const std::string& value) {
    return "\"" + value + "\"";
}

void TestRealPhotoRec(const fs::path& executablePath) {
    const char* configuredExecutable = std::getenv("PHOTOREC_EXE");
    if (configuredExecutable == nullptr || *configuredExecutable == '\0') {
        std::cout << "Real PhotoRec test skipped: PHOTOREC_EXE not configured\n";
        return;
    }

    const fs::path imagePath = fs::weakly_canonical(LocateRealTestImage(executablePath));
    if (!fs::is_regular_file(imagePath)) {
        std::cerr << "Real PhotoRec test failed: test image not found: "
                  << imagePath.string() << "\n";
        std::exit(1);
    }

    const fs::path outputPath =
        executablePath.parent_path() / "real_photorec_output";
    fs::remove_all(outputPath);
    for (const fs::directory_entry& entry :
         fs::directory_iterator(outputPath.parent_path())) {
        const std::string name = entry.path().filename().string();
        if (name.find(outputPath.filename().string() + ".") == 0) {
            fs::remove_all(entry.path());
        }
    }
    const std::vector<uint8_t> sourceBefore = ReadBytes(imagePath);

    CarvingRequest request;
    request.sourcePath = imagePath.string();
    request.outputDir = outputPath.string();
    request.requestedFileTypes = {"jpg"};
    const std::string command = QuoteCommandArgument(configuredExecutable) +
        " /log /d " + QuoteCommandArgument(request.outputDir) +
        " /cmd " + QuoteCommandArgument(request.sourcePath) +
        " partition_none,fileopt,everything,disable,fileopt,jpg,enable,search";
    std::cout << "Base output path: " << request.outputDir << "\n"
              << "PhotoRec executable: " << configuredExecutable << "\n"
              << "Source image: " << request.sourcePath << "\n"
              << "Exact PhotoRec command: " << command << "\n";
    PhotoRecCarver carver(nullptr, configuredExecutable);
    const CarvingResult result = carver.Carve(request);

    std::cout << "Post-PhotoRec diagnostics:\n"
              << "  request.outputDir: " << request.outputDir << "\n"
              << "  current working directory: "
              << fs::current_path().string() << "\n"
              << "  exact PhotoRec command: " << command << "\n"
              << "  PhotoRec exit code: " << result.exitCode << "\n";

    std::cout << "  matching output directories:\n";
    const fs::path outputParent = fs::absolute(outputPath).parent_path();
    const std::string outputPrefix = outputPath.filename().string();
    std::vector<fs::path> outputDirectories;
    for (const fs::directory_entry& entry :
         fs::directory_iterator(outputParent)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind(outputPrefix, 0) == 0 && fs::is_directory(entry.path())) {
            const fs::path absoluteDirectory = fs::absolute(entry.path());
            outputDirectories.push_back(absoluteDirectory);
            std::cout << "    " << absoluteDirectory.string() << "\n";
        }
    }

    std::cout << "  recovered files:\n";
    for (const fs::path& directory : outputDirectories) {
        std::error_code traversalError;
        for (fs::recursive_directory_iterator it(directory, traversalError), end;
             it != end && !traversalError; it.increment(traversalError)) {
            if (!it->is_regular_file(traversalError)) {
                continue;
            }
            const fs::path absoluteFile = fs::absolute(it->path());
            const bool exists = fs::exists(absoluteFile);
            std::cout << "    absolute path: " << absoluteFile.string() << "\n"
                      << "      exists: " << (exists ? "true" : "false") << "\n"
                      << "      size: "
                      << (exists ? fs::file_size(absoluteFile) : 0) << "\n";
        }
    }

    if (!result.success || result.exitCode != 0) {
        std::cerr << "Real PhotoRec test failed: " << result.errorMessage << "\n";
        std::exit(1);
    }

    size_t jpegCount = 0;
    for (const CarvingCandidate& candidate : result.candidates) {
        if (candidate.fileType != "jpg" && candidate.fileType != "jpeg") {
            continue;
        }
        const std::vector<uint8_t> bytes = ReadBytes(candidate.recoveredPath);
        if (bytes.size() < 5 ||
            bytes[0] != 0xff || bytes[1] != 0xd8 || bytes[2] != 0xff ||
            bytes[bytes.size() - 2] != 0xff || bytes.back() != 0xd9) {
            std::cerr << "Real PhotoRec test failed: invalid JPEG: "
                      << candidate.recoveredPath << "\n";
            std::exit(1);
        }
        ++jpegCount;
        std::cout << "Recovered file: " << candidate.recoveredPath
                  << ", size: " << candidate.recoveredSize
                  << ", detected extension: " << candidate.fileType << "\n";
    }
    if (jpegCount == 0 || sourceBefore != ReadBytes(imagePath)) {
        std::cerr << "Real PhotoRec test failed: no valid JPEG or source changed\n";
        std::exit(1);
    }
    std::cout << "Real PhotoRec test passed: " << jpegCount
              << " JPEG(s), source unchanged\n";
}

} // namespace

int main(int argc, char* argv[]) {
    TestMissingSource();
    TestMockRecovery();
    TestEmptyRequestedFileTypesUsesDefaultCommand();
    TestUnsupportedRequestedFileType();
    TestProcessFailure(true, 2, false, false);
    TestProcessFailure(false, -1, false, false);
    TestEmptySuccessfulRun();
    TestRealPhotoRec(argc > 0 ? fs::absolute(argv[0]) : fs::current_path());
    std::cout << "PhotoRecCarver tests passed\n";
    return 0;
}
