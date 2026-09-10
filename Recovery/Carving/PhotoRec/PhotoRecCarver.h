#pragma once

#include "../Core/ICarver.h"
#include "IPhotoRecProcessRunner.h"

#include <memory>
#include <string>

namespace Recovery {
namespace Carving {

    inline constexpr const char* kPhotoRecExecutablePath =
        R"(C:\Users\Amit\Desktop\photorec\testdisk-7.3-WIP\photorec_win.exe)";

    class PhotoRecCarver : public ICarver {
    public:
        explicit PhotoRecCarver(
            std::shared_ptr<IPhotoRecProcessRunner> runner = nullptr,
            std::string executablePath = {});
        ~PhotoRecCarver() override = default;

        CarvingResult Carve(const CarvingRequest& request) override;

    private:
        std::shared_ptr<IPhotoRecProcessRunner> m_runner;
        std::string m_executablePath;

        static std::string QuoteArgument(const std::string& value);
        static std::string BuildCommand(const std::string& executablePath,
                                        const CarvingRequest& request);
        static bool ValidateRequestedFileTypes(
            const std::vector<std::string>& requestedFileTypes,
            std::string& errorMessage);
        static void DiscoverCandidates(const std::string& outputDir,
                                       std::vector<CarvingCandidate>& candidates);
    };

} // namespace Carving
} // namespace Recovery
