#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Recovery {
namespace Carving {
namespace Verification {

/** Small FIPS 180-4 SHA-256 implementation; no platform crypto dependency. */
class Sha256 {
public:
    static std::string Compute(const std::vector<uint8_t>& data);
    static std::string Compute(const uint8_t* data, size_t size);
};

} // namespace Verification
} // namespace Carving
} // namespace Recovery

namespace Recovery { namespace Carving {
using Sha256 = Verification::Sha256;
}}
