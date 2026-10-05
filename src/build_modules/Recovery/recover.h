#pragma once

#include <filesystem>
#include <string>

namespace Recovery {

// Run TSK (tsk_recover.exe) via subprocess and format output
bool RecoverMetadata(
    const std::string& diskImage,
    const std::filesystem::path& outputRoot = std::filesystem::path("Recovery") / "output");

// Run PhotoRec (photorec_win.exe) via subprocess and format output
bool RecoverCarving(
    const std::string& diskImage,
    const std::filesystem::path& outputRoot = std::filesystem::path("Recovery") / "output");

} // namespace Recovery
