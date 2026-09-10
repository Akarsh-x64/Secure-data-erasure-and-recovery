#pragma once
#include "../Core/IFileVerifier.h"

namespace Recovery { namespace Carving { namespace Verification {
struct ZipEntry {
    std::string name;
    uint32_t compressedSize = 0;
    uint32_t uncompressedSize = 0;
    uint32_t crc32 = 0;
    uint32_t localOffset = 0;
    uint16_t method = 0;
};

class ZipVerifier final : public IFileVerifier {
public:
    const char* FormatName() const override { return "ZIP"; }
    bool CanVerify(const std::vector<uint8_t>& bytes, const std::string& hint = {}) const override;
    VerificationResult Verify(const std::vector<uint8_t>& bytes,
                              const std::string& path = {}) const override;

    // Used by the OOXML verifier after the ZIP structure has been checked.
    static bool ListEntries(const std::vector<uint8_t>& bytes,
                            std::vector<ZipEntry>& entries, std::string* error = nullptr);
};
}}}

namespace Recovery { namespace Carving {
using ZipVerifier = Verification::ZipVerifier;
using ZipEntry = Verification::ZipEntry;
}}
