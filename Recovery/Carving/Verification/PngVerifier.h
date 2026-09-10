#pragma once
#include "../Core/IFileVerifier.h"

namespace Recovery { namespace Carving { namespace Verification {
class PngVerifier final : public IFileVerifier {
public:
    const char* FormatName() const override { return "PNG"; }
    bool CanVerify(const std::vector<uint8_t>& bytes, const std::string& hint = {}) const override;
    VerificationResult Verify(const std::vector<uint8_t>& bytes,
                              const std::string& path = {}) const override;
};
}}}

namespace Recovery { namespace Carving {
using PngVerifier = Verification::PngVerifier;
}}
