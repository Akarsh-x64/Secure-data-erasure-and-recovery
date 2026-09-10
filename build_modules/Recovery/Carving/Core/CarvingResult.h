#pragma once

#include "CarvingCandidate.h"
#include <cstdint>
#include <string>
#include <vector>

namespace Recovery {
namespace Carving {

    /**
     * @brief Result object returned by ICarver upon completing a carving run.
     */
    struct CarvingResult {
        bool success = false;
        int exitCode = -1;
        uint32_t candidatesFound = 0;
        std::vector<CarvingCandidate> candidates;
        std::string errorMessage;
        std::string outputDir;
    };

} // namespace Carving
} // namespace Recovery
