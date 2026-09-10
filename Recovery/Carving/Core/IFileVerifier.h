#pragma once

#include "VerificationResult.h"
#include <cstdint>
#include <string>
#include <vector>

namespace Recovery {
namespace Carving {

/** Format verifiers consume bounded bytes and never access the source device. */
class IFileVerifier {
public:
    virtual ~IFileVerifier() = default;
    virtual const char* FormatName() const = 0;
    virtual bool CanVerify(const std::vector<uint8_t>& bytes,
                           const std::string& hint = {}) const = 0;
    virtual VerificationResult Verify(const std::vector<uint8_t>& bytes,
                                      const std::string& path = {}) const = 0;
};

} // namespace Carving
} // namespace Recovery
