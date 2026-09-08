#pragma once

#include "CarvingRequest.h"
#include "CarvingResult.h"

namespace Recovery {
namespace Carving {

    /**
     * @brief Abstract interface for file carvers in the Recovery module.
     */
    class ICarver {
    public:
        virtual ~ICarver() = default;

        /**
         * @brief Performs file carving on the raw storage source specified in request.
         *
         * @param request Parameters defining source path, output directory, and target formats.
         * @return CarvingResult containing success status, exit code, and recovered candidates.
         */
        virtual CarvingResult Carve(const CarvingRequest& request) = 0;
    };

} // namespace Carving
} // namespace Recovery
