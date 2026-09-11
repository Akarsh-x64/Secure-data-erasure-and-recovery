#pragma once

#include <cstdint>
#include <string>

namespace Erasure {
namespace Core {

    // Common error codes for the engine
    enum class ErrorCode {
        Success = 0,
        AccessDenied,
        DeviceNotFound,
        UnsupportedHardware,
        FileSystemNotSupported,
        ReadError,
        WriteError,
        UnknownError
    };

    // Defines the physical characteristics of the attached device
    struct DeviceGeometry {
        uint32_t bytesPerSector;
        uint64_t totalSectors;
        std::string devicePath;
    };

} // namespace Core
} // namespace Erasure
