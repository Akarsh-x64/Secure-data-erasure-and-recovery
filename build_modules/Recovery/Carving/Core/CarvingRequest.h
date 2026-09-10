#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Recovery {
namespace Carving {

    /**
     * @brief Request parameters for raw data carving.
     */
    struct CarvingRequest {
        std::string sourcePath;                      // Path to raw image file or device
        std::string outputDir;                       // Directory to write carved output files
        uint64_t sourceOffset = 0;                  // Byte offset in source (0 = start)
        uint64_t sourceLength = 0;                  // Byte length to carve (0 = whole storage)
        std::vector<std::string> requestedFileTypes; // Requested extensions, e.g. {"png", "jpg"}
    };

} // namespace Carving
} // namespace Recovery
