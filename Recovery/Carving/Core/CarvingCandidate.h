#pragma once

#include <cstdint>
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
        uint64_t recoveredSize = 0;  // File size in bytes
        std::string validationStatus;// "VALIDATED", "PROMISING", "INPROGRESS", "UNKNOWN"
        bool isValidated = false;    // True if fully validated by carver
    };

} // namespace Carving
} // namespace Recovery
