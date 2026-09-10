#pragma once

#include <string>
#include <vector>

namespace Recovery {
namespace Carving {

    struct PhotoRecProcessResult {
        int exitCode = -1;
        std::string stdOut;
        std::string stdErr;
        bool executed = false;
    };

    class IPhotoRecProcessRunner {
    public:
        virtual ~IPhotoRecProcessRunner() = default;

        virtual PhotoRecProcessResult RunCommand(
            const std::string& command,
            const std::vector<std::string>& envVars = {}) = 0;
    };

} // namespace Carving
} // namespace Recovery
