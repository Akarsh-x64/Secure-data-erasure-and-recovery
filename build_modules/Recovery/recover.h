#pragma once

#include <filesystem>
#include <string>

namespace Recovery {

bool RecoverMetadata(
    const std::string& diskImage,
    const std::filesystem::path& outputRoot =
        std::filesystem::path("Recovery") / "output");

bool RecoverCarving(
    const std::string& diskImage,
    const std::filesystem::path& outputRoot =
        std::filesystem::path("Recovery") / "output");

int RunRecoveryCommand(int argc, char** argv);

} // namespace Recovery