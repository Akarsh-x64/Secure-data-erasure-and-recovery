#pragma once

#include "../../Core/RecoveryEnums.h"
#include "VerificationResult.h"

#include <cstdint>
#include <optional>
#include <string>

namespace Recovery {
namespace Carving {

    /**
     * @brief Represents a file candidate recovered via carving.
     */
    struct CarvingCandidate {
        std::string recoveredPath;   // Path to carved output file on disk
        std::string fileType;        // Extension/format (e.g., "png", "jpg")
        uint64_t sourceOffset = 0;   // Source byte offset if available
        bool sourceOffsetKnown = false;
        uint64_t recoveredSize = 0;  // File size in bytes
        std::string validationStatus;// "VALIDATED", "PROMISING", "INPROGRESS", "UNKNOWN"
        bool isValidated = false;    // True if fully validated by carver
        Core::RecoveryBackend recoveryBackend = Core::RecoveryBackend::PHOTOREC_CARVING;
        std::string sourcePath;
        std::optional<uint32_t> partitionIndex;
        uint64_t partitionOffset = 0;
        uint64_t partitionSize = 0;
        VerificationResult verification;
    };

} // namespace Carving
} // namespace Recovery
