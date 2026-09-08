#include "IsoBmffVerifier.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace Recovery { namespace Carving { namespace Verification {
namespace {
uint32_t U32(const std::vector<uint8_t>& b,size_t p) {
    return (uint32_t(b[p])<<24)|(uint32_t(b[p+1])<<16)|(uint32_t(b[p+2])<<8)|b[p+3];
}
bool FourCC(const std::vector<uint8_t>& b,size_t p,const char* x) {
    return p+4<=b.size() && std::memcmp(b.data()+p,x,4)==0;
}
bool Ext(const std::string& h,const char* s) {
    const std::string normalized = (!h.empty() && h[0] == '.') ? h : "." + h;
    if(normalized.size()<std::strlen(s)) return false;
    std::string a=normalized.substr(normalized.size()-std::strlen(s));
    for(char& c:a)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return a==s;
}
void Check(VerificationResult& r,const char* n,CheckStatus s,const std::string& x,double w=1.0) {
    r.AddCheck(VerificationCheck(n,s,x,w));
}
}
bool IsoBmffVerifier::CanVerify(const std::vector<uint8_t>& b,const std::string& hint) const {
    return (b.size()>=12 && FourCC(b,4,"ftyp")) || Ext(hint,".mp4") || Ext(hint,".mov") ||
           Ext(hint,".m4v") || Ext(hint,".m4a");
}
VerificationResult IsoBmffVerifier::Verify(const std::vector<uint8_t>& b,const std::string& path) const {
    VerificationResult r; r.detectedType="ISO BMFF"; r.path=path;
    bool ftyp=false,moov=false,mdat=false,bad=false,brand=false;
    size_t p=0;
    while(p<b.size()) {
        if(b.size()-p<8) { bad=true; break; }
        uint64_t size=U32(b,p);
        const size_t header= size==1 ? 16u : 8u;
        if(size==1) {
            if(b.size()-p<16) { bad=true; break; }
            size=(uint64_t(U32(b,p+8))<<32)|U32(b,p+12);
        } else if(size==0) size=b.size()-p;
        if(size<header || size>b.size()-p) { bad=true; break; }
        if(FourCC(b,p+4,"ftyp")) {
            ftyp=true;
            if(size>=16) {
                const size_t end=p+static_cast<size_t>(size);
                for(size_t q=p+8;q+4<=end;q+=4) {
                    if(FourCC(b,q,"isom")||FourCC(b,q,"iso2")||FourCC(b,q,"mp41")||
                       FourCC(b,q,"mp42")||FourCC(b,q,"avc1")||FourCC(b,q,"qt  ")) brand=true;
                }
            }
        } else if(FourCC(b,p+4,"moov")) moov=true;
        else if(FourCC(b,p+4,"mdat")) mdat=true;
        p += static_cast<size_t>(size);
    }
    Check(r,"Box bounds",bad?CheckStatus::FAIL:CheckStatus::PASS,
          bad?"An ISO BMFF box exceeds the bounded input.":"Top-level boxes are bounded.",3.0);
    Check(r,"File type box",ftyp?CheckStatus::PASS:CheckStatus::FAIL,
          ftyp?"ftyp box is present.":"Missing ftyp box.",2.0);
    Check(r,"Recognized brand",brand?CheckStatus::PASS:CheckStatus::NOT_PERFORMED,
          brand?"A common MP4/MOV compatible brand is present.":
                "Brand is non-standard; box structure is still checked.");
    Check(r,"Movie box",moov?CheckStatus::PASS:CheckStatus::FAIL,
          moov?"moov box is present.":"Missing moov box.",2.0);
    Check(r,"Media data",mdat?CheckStatus::PASS:CheckStatus::FAIL,
          mdat?"mdat box is present.":"Missing mdat box.",1.0);
    if(bad) r.classification=VerificationClassification::CORRUPTED;
    else if(ftyp&&moov&&mdat) r.classification=VerificationClassification::VALID;
    else if(ftyp) r.classification=VerificationClassification::PARTIAL;
    else r.classification=VerificationClassification::INVALID;
    r.explanation=r.classification==VerificationClassification::VALID?
        "ISO BMFF type, movie and media boxes are bounded.":"ISO BMFF is incomplete or malformed.";
    return r;
}
}}}
