#pragma once

#include "IPhotoRecProcessRunner.h"

namespace Recovery {
namespace Carving {

    class PhotoRecProcessRunner : public IPhotoRecProcessRunner {
    public:
        PhotoRecProcessRunner() = default;
        ~PhotoRecProcessRunner() override = default;

        PhotoRecProcessResult RunCommand(
            const std::string& command,
            const std::vector<std::string>& envVars = {}) override;
    };

} // namespace Carving
} // namespace Recovery
