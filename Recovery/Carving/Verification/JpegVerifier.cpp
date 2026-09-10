#include "JpegVerifier.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace Recovery { namespace Carving { namespace Verification {
namespace {
void Check(VerificationResult& r, const char* name, CheckStatus s,
           const std::string& why, double weight = 1.0) {
    r.AddCheck(VerificationCheck(name, s, why, weight));
}
bool Has(const std::string& hint, const char* ext) {
    const std::string normalized = (!hint.empty() && hint[0] == '.') ? hint : "." + hint;
    if (normalized.size() < std::strlen(ext)) return false;
    const size_t start = normalized.size() - std::strlen(ext);
    for (size_t i = 0; i < std::strlen(ext); ++i) {
        if (std::tolower(static_cast<unsigned char>(normalized[start + i])) !=
            std::tolower(static_cast<unsigned char>(ext[i]))) return false;
    }
    return true;
}
}

bool JpegVerifier::CanVerify(const std::vector<uint8_t>& b, const std::string& hint) const {
    return (b.size() >= 2 && b[0] == 0xff && b[1] == 0xd8) ||
           Has(hint, ".jpg") || Has(hint, ".jpeg");
}

VerificationResult JpegVerifier::Verify(const std::vector<uint8_t>& b, const std::string& path) const {
    VerificationResult r; r.detectedType = "JPEG"; r.path = path;
    const bool soi = b.size() >= 2 && b[0] == 0xff && b[1] == 0xd8;
    Check(r, "SOI signature", soi ? CheckStatus::PASS : CheckStatus::FAIL,
          soi ? "JPEG SOI marker is present." : "Missing FF D8 start marker.", 2.0);
    if (!soi) { r.classification = VerificationClassification::INVALID; r.explanation = "Not a JPEG stream."; return r; }
    bool malformed = false, eoi = false, sawFrame = false;
    size_t p = 2;
    while (p < b.size()) {
        if (b[p] != 0xff) { malformed = true; break; }
        while (p < b.size() && b[p] == 0xff) ++p;
        if (p >= b.size()) break;
        const uint8_t marker = b[p++];
        if (marker == 0xd9) { eoi = true; break; }
        if (marker == 0xda) sawFrame = true; // SOS is a useful structural signal.
        if (marker == 0xd8 || (marker >= 0xd0 && marker <= 0xd7) || marker == 0x01) continue;
        if (p + 2 > b.size()) { malformed = true; break; }
        const size_t length = (size_t(b[p]) << 8) | b[p + 1];
        if (length < 2 || length > b.size() - p) { malformed = true; break; }
        p += length;
        if (marker == 0xda) {
            // Entropy data may contain FF00 and restart markers. Find the next marker.
            while (p + 1 < b.size()) {
                if (b[p] != 0xff) { ++p; continue; }
                if (b[p + 1] == 0x00 || (b[p + 1] >= 0xd0 && b[p + 1] <= 0xd7)) { p += 2; continue; }
                break;
            }
        }
    }
    Check(r, "Marker structure", malformed ? CheckStatus::FAIL : CheckStatus::PASS,
          malformed ? "A marker segment exceeds the bounded input." : "Marker segments are bounded.", 2.0);
    Check(r, "Frame marker", sawFrame ? CheckStatus::PASS : CheckStatus::NOT_PERFORMED,
          sawFrame ? "A start-of-scan marker is present." : "No scan marker (minimal/header-only JPEG).");
    Check(r, "EOI marker", eoi ? CheckStatus::PASS : CheckStatus::FAIL,
          eoi ? "JPEG end marker is present." : "Missing FF D9 end marker.", 2.0);
    r.classification = malformed ? VerificationClassification::CORRUPTED :
        (eoi ? VerificationClassification::VALID : VerificationClassification::PARTIAL);
    r.explanation = eoi ? "JPEG markers form a complete bounded stream." :
                         "JPEG header is present but the stream is incomplete.";
    return r;
}
}}}
