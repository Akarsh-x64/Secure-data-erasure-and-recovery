#pragma once

#include "../../Core/FileRecord.h"
#include "../../Core/IReadOnlyStorage.h"
#include "../../Core/StorageRegion.h"

#include <string>
#include <vector>

namespace Recovery {
namespace Carving {

    class IMetadataRecoveryBackend {
    public:
        virtual ~IMetadataRecoveryBackend() = default;

        virtual bool IsAvailable() const = 0;
        virtual std::string CapabilityMessage() const = 0;
        virtual bool Recover(Core::IReadOnlyStorage& storage,
                             const Core::StorageRegion& region,
                             std::vector<Core::FileRecord>& records,
                             std::string& errorMessage) = 0;
    };

} // namespace Carving
} // namespace Recovery
