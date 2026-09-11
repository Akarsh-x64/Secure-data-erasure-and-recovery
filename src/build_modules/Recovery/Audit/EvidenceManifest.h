#pragma once

#include "EvidenceRecord.h"

#include <string>
#include <vector>

namespace Recovery {
namespace Audit {

    /**
     * @brief Builds the versioned canonical text and digest for evidence records.
     *
     * Record order is significant. Strings are encoded as UTF-8 bytes rendered
     * as length-prefixed hexadecimal, and optional values use "-" for absent
     * or "v:<decimal>" for present values. This is an integrity digest of the
     * canonical representation, not a chain-of-custody record.
     */
    class EvidenceManifest {
    public:
        static std::string Canonicalize(
            const std::vector<EvidenceRecord>& records);

        static std::string Hash(const std::vector<EvidenceRecord>& records);
    };

} // namespace Audit
} // namespace Recovery
