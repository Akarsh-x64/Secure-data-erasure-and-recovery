#include "ZipVerifier.h"
#include <algorithm>
#include <cstring>

namespace Recovery { namespace Carving { namespace Verification {
namespace {
uint16_t U16(const std::vector<uint8_t>& b,size_t p) {
    return uint16_t(b[p]) | (uint16_t(b[p+1])<<8);
}
uint32_t U32(const std::vector<uint8_t>& b,size_t p) {
    return uint32_t(b[p]) | (uint32_t(b[p+1])<<8) |
           (uint32_t(b[p+2])<<16) | (uint32_t(b[p+3])<<24);
}
uint32_t Crc32(const uint8_t* p,size_t n) {
    uint32_t crc=0xffffffffu;
    for(size_t i=0;i<n;++i) {
        crc^=p[i];
        for(unsigned bit=0;bit<8;++bit)
            crc=(crc>>1)^(0xedb88320u&-(crc&1u));
    }
    return ~crc;
}
bool Sig(const std::vector<uint8_t>& b,size_t p,uint32_t s) {
    return p <= b.size() && b.size()-p >= 4 && U32(b,p)==s;
}
void Check(VerificationResult& r,const char* n,CheckStatus s,const std::string& x,double w=1.0) {
    r.AddCheck(VerificationCheck(n,s,x,w));
}
}

bool ZipVerifier::ListEntries(const std::vector<uint8_t>& b,std::vector<ZipEntry>& entries,std::string* error) {
    entries.clear();
    const size_t start=b.size()>65557?b.size()-65557:0;
    size_t eocd=b.size();
    for (size_t p=b.size(); p>=start+4; --p) {
        const size_t at=p-4;
        if (Sig(b,at,0x06054b50u)) { eocd=at; break; }
        if (p==start+4) break;
    }
    if (eocd==b.size()) { if(error)*error="End of central directory is missing."; return false; }
    if (eocd+22>b.size()) { if(error)*error="Truncated end of central directory."; return false; }
    const uint16_t disk=U16(b,eocd+4), cdDisk=U16(b,eocd+6), countDisk=U16(b,eocd+8);
    const uint16_t count=U16(b,eocd+10), comment=U16(b,eocd+20);
    if (disk != 0 || cdDisk != 0 || countDisk != count) {
        if(error)*error="Multi-disk ZIP is not supported.";
        return false;
    }
    if (eocd+22u+comment>b.size()) { if(error)*error="ZIP comment exceeds input."; return false; }
    const uint32_t cdSize=U32(b,eocd+12), cdOffset=U32(b,eocd+16);
    if (uint64_t(cdOffset)+cdSize>b.size()) { if(error)*error="Central directory exceeds input."; return false; }
    size_t p=cdOffset;
    for (uint16_t i=0;i<count;++i) {
        if (!Sig(b,p,0x02014b50u) || b.size()-p<46) { if(error)*error="Invalid central directory entry."; return false; }
        const uint16_t nameLen=U16(b,p+28), extraLen=U16(b,p+30), commentLen=U16(b,p+32);
        const size_t header=46u+nameLen+extraLen+commentLen;
        if (header>b.size()-p || p+header>uint64_t(cdOffset)+cdSize) {
            if(error)*error="Central directory entry is truncated.";
            return false;
        }
        ZipEntry entry;
        entry.name.assign(reinterpret_cast<const char*>(b.data()+p+46),nameLen);
        entry.crc32=U32(b,p+16);
        entry.compressedSize=U32(b,p+20); entry.uncompressedSize=U32(b,p+24);
        entry.localOffset=U32(b,p+42); entry.method=U16(b,p+10);
        entries.push_back(entry);
        p += header;
    }
    if (p != uint64_t(cdOffset)+cdSize) {
        if(error)*error="Central directory size does not match its entries.";
        return false;
    }
    for (const auto& entry:entries) {
        const size_t at=entry.localOffset;
        if (!Sig(b,at,0x04034b50u) || b.size()-at<30) {
            if(error)*error="Local file header is missing.";
            return false;
        }
        const size_t localHeader=30u+U16(b,at+26)+U16(b,at+28);
        if (localHeader>b.size()-at || uint64_t(at)+localHeader+entry.compressedSize>b.size()) {
            if(error)*error="Compressed entry exceeds input.";
            return false;
        }
        const uint16_t method=U16(b,at+8);
        if (method==0 && entry.compressedSize==entry.uncompressedSize) {
            const uint8_t* payload=b.data()+at+localHeader;
            if (Crc32(payload,entry.compressedSize)!=entry.crc32) {
                if(error)*error="Stored entry CRC does not match.";
                return false;
            }
        }
    }
    return true;
}

bool ZipVerifier::CanVerify(const std::vector<uint8_t>& b,const std::string& hint) const {
    const bool local=b.size()>=4 && (Sig(b,0,0x04034b50u)||Sig(b,0,0x06054b50u));
    if (local) return true;
    const std::string normalized = (!hint.empty() && hint[0] == '.') ? hint : "." + hint;
    return normalized.size()>=4 &&
        std::tolower(static_cast<unsigned char>(normalized[normalized.size()-4]))=='.' &&
        std::tolower(static_cast<unsigned char>(normalized[normalized.size()-3]))=='z' &&
        std::tolower(static_cast<unsigned char>(normalized[normalized.size()-2]))=='i' &&
        std::tolower(static_cast<unsigned char>(normalized[normalized.size()-1]))=='p';
}

VerificationResult ZipVerifier::Verify(const std::vector<uint8_t>& b,const std::string& path) const {
    VerificationResult r; r.detectedType="ZIP"; r.path=path;
    const bool local=b.size()>=4 && Sig(b,0,0x04034b50u);
    const bool eocd=b.size()>=22 && [&]() {
        const size_t start=b.size()>65557?b.size()-65557:0;
        for(size_t p=b.size();p>=start+4;--p) {
            if(Sig(b,p-4,0x06054b50u)) return true;
            if(p==start+4) break;
        }
        return false;
    }();
    Check(r,"ZIP signature",(local||eocd)?CheckStatus::PASS:CheckStatus::FAIL,
          (local||eocd)?"ZIP record signature is present.":"No ZIP record signature.",2.0);
    std::vector<ZipEntry> entries; std::string error;
    const bool structure=ListEntries(b,entries,&error);
    if (structure) {
        Check(r,"Central directory",CheckStatus::PASS,
              "Central directory and local headers are bounded.",3.0);
        Check(r,"Entry count",CheckStatus::PASS,
              std::to_string(entries.size())+" entries are structurally addressable.");
        Check(r,"Entry payload integrity",CheckStatus::PASS,
              "Stored entry CRCs match; compressed entries are boundary-checked.",1.0);
        r.classification=VerificationClassification::VALID;
        r.explanation="ZIP central directory and every referenced local entry are bounded.";
    } else {
        Check(r,"Central directory",eocd?CheckStatus::FAIL:CheckStatus::NOT_PERFORMED,
              error.empty()?"ZIP is incomplete.":error,3.0);
        r.classification=local?VerificationClassification::PARTIAL:VerificationClassification::INVALID;
        r.explanation=local?"A local ZIP record exists but the archive is incomplete.":
                              "No complete ZIP archive was found.";
    }
    return r;
}
}}}
