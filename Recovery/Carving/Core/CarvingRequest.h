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
        std::string configFile;                      // Optional path to Scalpel configuration file
        std::string scalpelHome;                     // Path to SCALPEL3_HOME directory
        std::string scalpelExecutable = "scalpel3";  // Executable name or path
        std::string crblockmapExecutable = "crblockmap"; // Blockmap creation tool path
    };

} // namespace Carving
} // namespace Recovery
