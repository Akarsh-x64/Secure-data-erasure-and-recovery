#include "PngVerifier.h"
#include <cstring>

namespace Recovery { namespace Carving { namespace Verification {
namespace {
uint32_t Read32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
uint32_t Crc32(const uint8_t* p, size_t n) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < n; ++i) {
        crc ^= p[i];
        for (unsigned j = 0; j < 8; ++j) crc = (crc >> 1) ^ (0xedb88320u & -(crc & 1u));
    }
    return ~crc;
}
void Check(VerificationResult& r, const char* n, CheckStatus s, const std::string& x, double w=1.0) {
    r.AddCheck(VerificationCheck(n, s, x, w));
}
}
bool PngVerifier::CanVerify(const std::vector<uint8_t>& b, const std::string&) const {
    static const uint8_t sig[] = {0x89,'P','N','G',0x0d,0x0a,0x1a,0x0a};
    return b.size() >= sizeof(sig) && std::memcmp(b.data(), sig, sizeof(sig)) == 0;
}
VerificationResult PngVerifier::Verify(const std::vector<uint8_t>& b, const std::string& path) const {
    VerificationResult r; r.detectedType="PNG"; r.path=path;
    static const uint8_t sig[] = {0x89,'P','N','G',0x0d,0x0a,0x1a,0x0a};
    const bool signature = b.size() >= sizeof(sig) && std::memcmp(b.data(), sig, sizeof(sig)) == 0;
    Check(r,"PNG signature",signature?CheckStatus::PASS:CheckStatus::FAIL,
          signature?"PNG signature is present.":"Invalid or truncated PNG signature.",2.0);
    if (!signature) { r.classification=VerificationClassification::INVALID; r.explanation="Not a PNG stream."; return r; }
    size_t p=8; bool ihdr=false, iend=false, bad=false, crcBad=false, idat=false, typeBad=false;
    uint32_t width=0, height=0;
    while (p < b.size()) {
        if (b.size()-p < 12) { bad=true; break; }
        const uint32_t len=Read32(b.data()+p);
        if (len > b.size()-p-12) { bad=true; break; }
        const uint8_t* type=b.data()+p+4;
        const uint8_t* data=type+4;
        for (unsigned i=0; i<4; ++i) {
            const uint8_t c = type[i];
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) {
                typeBad=true;
            }
        }
        const uint32_t stored=Read32(data+len);
        const uint32_t actual=Crc32(type, size_t(4)+len);
        if (stored != actual) crcBad=true;
        if (std::memcmp(type,"IHDR",4)==0) {
            if (len != 13 || ihdr) bad=true;
            else { ihdr=true; width=Read32(data); height=Read32(data+4); }
        } else if (std::memcmp(type,"IEND",4)==0) {
            if (len != 0) bad=true;
            iend=true; p += 12; break;
        } else if (std::memcmp(type,"IDAT",4)==0) {
            idat=true;
        } else if (!ihdr && std::memcmp(type,"PLTE",4)==0) {
            bad=true;
        }
        p += 12 + len;
    }
    const bool dimensions=ihdr && width != 0 && height != 0;
    Check(r,"IHDR",dimensions?CheckStatus::PASS:CheckStatus::FAIL,
          dimensions?"Dimensions are non-zero and IHDR is unique.":"Missing, duplicate, or invalid IHDR.",2.0);
    Check(r,"Chunk bounds",bad?CheckStatus::FAIL:CheckStatus::PASS,
          bad?"A PNG chunk exceeds the input bounds.":"All chunk lengths are bounded.",2.0);
    Check(r,"Chunk CRC",crcBad?CheckStatus::FAIL:CheckStatus::PASS,
          crcBad?"At least one chunk CRC does not match.":"Chunk CRC values match.",1.0);
    Check(r,"Chunk type bytes",typeBad?CheckStatus::FAIL:CheckStatus::PASS,
          typeBad?"A chunk type contains a non-letter byte.":"Chunk type bytes are valid.",1.0);
    Check(r,"IDAT",idat?CheckStatus::PASS:CheckStatus::FAIL,
          idat?"At least one IDAT chunk is present.":"No IDAT chunk is present.",1.0);
    Check(r,"IEND",iend?CheckStatus::PASS:CheckStatus::FAIL,
          iend?"PNG end chunk is present.":"Missing IEND chunk.",2.0);
    if (bad || crcBad || typeBad) r.classification=VerificationClassification::CORRUPTED;
    else if (ihdr && iend && dimensions) r.classification=VerificationClassification::VALID;
    else r.classification=VerificationClassification::PARTIAL;
    r.explanation = r.classification == VerificationClassification::VALID ?
        "PNG signature, chunks, dimensions and CRCs are valid." :
        "PNG structure is incomplete or contains an integrity error.";
    return r;
}
}}}
