#pragma once

#include "ICarver.h"
#include "IMetadataRecoveryBackend.h"
#include "RecoveryRequest.h"
#include "RecoveryResult.h"
#include "../../Core/IReadOnlyStorage.h"

#include <memory>

namespace Recovery {
namespace Carving {

    class RecoveryOrchestrator {
    public:
        RecoveryOrchestrator(
            std::shared_ptr<Core::IReadOnlyStorage> storage = nullptr,
            std::shared_ptr<ICarver> carver = nullptr,
            std::shared_ptr<IMetadataRecoveryBackend> metadataBackend = nullptr);

        RecoveryResult Recover(const RecoveryRequest& request);

    private:
        std::shared_ptr<Core::IReadOnlyStorage> m_storage;
        std::shared_ptr<ICarver> m_carver;
        std::shared_ptr<IMetadataRecoveryBackend> m_metadataBackend;
    };

} // namespace Carving
} // namespace Recovery
