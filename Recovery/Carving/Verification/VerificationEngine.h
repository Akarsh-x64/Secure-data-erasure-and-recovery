#pragma once

#include "../Core/CarvingCandidate.h"
#include "../Core/VerificationResult.h"
#include "IsoBmffVerifier.h"
#include "JpegVerifier.h"
#include "OoxmlVerifier.h"
#include "PdfVerifier.h"
#include "PngVerifier.h"
#include "ZipVerifier.h"
#include <string>

namespace Recovery { namespace Carving { namespace Verification {

class VerificationEngine {
public:
    VerificationResult VerifyPath(const std::string& path,
                                  const std::string& expectedExtension = {}) const;
    VerificationResult Verify(const std::string& path,
                              const std::string& expectedExtension = {}) const {
        return VerifyPath(path, expectedExtension);
    }
    VerificationResult Verify(const CarvingCandidate& candidate,
                              bool compareExtension = true) const;
    VerificationResult VerifyBytes(const std::vector<uint8_t>& bytes,
                                   const std::string& hint = {}) const;

private:
    static void AddExtensionCheck(VerificationResult& result,
                                  const std::string& expected);
};

}}}

// The surrounding carving namespace is retained for consistency with existing
// CarvingResult/CarvingCandidate APIs.
namespace Recovery { namespace Carving {
using VerificationEngine = Verification::VerificationEngine;
}}
