#pragma once

#include "IHardwareController.h"
#include <string>

namespace Erasure {
namespace Core {

    /**
     * @brief The File System layer contract.
     * Contains the "Finding Logic" to parse filesystem structures.
     */
    class IFileSystemDriver {
    public:
        virtual ~IFileSystemDriver() = default;

        // Mounts the filesystem by reading the boot record and initializing internal state
        virtual bool Mount() = 0;

        // Locates the file, tells Hardware to erase its sectors, and zeroes out metadata
        virtual bool DeleteFile(const std::string& relativePath) = 0;

        // Traverses the entire directory tree and obliterates all file metadata and data
        virtual bool WipeVolume() = 0;
    };

} // namespace Core
} // namespace Erasure
