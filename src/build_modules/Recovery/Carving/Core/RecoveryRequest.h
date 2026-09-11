#pragma once

#include "../../Core/RecoveryEnums.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Recovery {
namespace Carving {

    struct RecoveryRequest {
        std::string sourcePath;
        std::string outputDir;
        std::optional<uint32_t> selectedPartitionIndex;
        std::vector<std::string> requestedFileTypes;
        Core::RecoveryMethod recoveryMethod = Core::RecoveryMethod::Carving;
        bool verifyResults = true;
    };

} // namespace Carving
} // namespace Recovery
