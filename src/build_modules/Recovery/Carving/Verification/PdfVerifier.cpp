#include "PdfVerifier.h"
#include <algorithm>
#include <cstring>

namespace Recovery { namespace Carving { namespace Verification {
namespace {
void Check(VerificationResult& r,const char* n,CheckStatus s,const std::string& x,double w=1.0) {
    r.AddCheck(VerificationCheck(n,s,x,w));
}
bool Contains(const std::vector<uint8_t>& b,const char* text,size_t begin,size_t end) {
    const size_t n=std::strlen(text);
    if (end < begin || n > end-begin) return false;
    for (size_t i=begin; i+n<=end; ++i)
        if (std::memcmp(b.data()+i,text,n)==0) return true;
    return false;
}
}
bool PdfVerifier::CanVerify(const std::vector<uint8_t>& b,const std::string&) const {
    return b.size()>=5 && std::memcmp(b.data(),"%PDF-",5)==0;
}
VerificationResult PdfVerifier::Verify(const std::vector<uint8_t>& b,const std::string& path) const {
    VerificationResult r; r.detectedType="PDF"; r.path=path;
    const bool header=b.size()>=5 && std::memcmp(b.data(),"%PDF-",5)==0;
    Check(r,"PDF header",header?CheckStatus::PASS:CheckStatus::FAIL,
          header?"PDF header is present.":"Missing %PDF- header.",2.0);
    if (!header) { r.classification=VerificationClassification::INVALID; r.explanation="Not a PDF stream."; return r; }
    const size_t windowStart=b.size()>1024?b.size()-1024:0;
    const bool eof=Contains(b,"%%EOF",windowStart,b.size());
    const bool startxref=Contains(b,"startxref",windowStart,b.size());
    const bool objects=Contains(b," obj",0,b.size()) || Contains(b," obj\n",0,b.size());
    const bool xref=Contains(b,"xref",0,b.size()) || Contains(b,"/XRef",0,b.size());
    const bool trailer=Contains(b,"trailer",0,b.size()) || Contains(b,"/Root",0,b.size());
    Check(r,"EOF marker",eof?CheckStatus::PASS:CheckStatus::FAIL,
          eof?"%%EOF appears near the end of the file.":"Missing %%EOF marker.",2.0);
    Check(r,"startxref",startxref?CheckStatus::PASS:CheckStatus::NOT_PERFORMED,
          startxref?"startxref appears near the trailer.":"No xref trailer (may be a linearized/minimal PDF).");
    Check(r,"Objects/xref",objects || xref ? CheckStatus::PASS : CheckStatus::NOT_PERFORMED,
          objects || xref ? "Objects or an xref structure is present." :
          "No object or xref structure was found.");
    Check(r,"Trailer",trailer ? CheckStatus::PASS : CheckStatus::NOT_PERFORMED,
          trailer ? "A trailer or root dictionary marker is present." :
          "No trailer dictionary marker was found.");
    if (!eof) {
        r.classification=VerificationClassification::PARTIAL;
        r.explanation="PDF header is present but the file is truncated.";
    } else if (!objects && !xref) {
        r.classification=VerificationClassification::PARTIAL;
        r.explanation="PDF terminal marker is present but no object/xref structure was found.";
    } else {
        r.classification=VerificationClassification::VALID;
        r.explanation="PDF header, structural markers and terminal marker are present.";
    }
    return r;
}
}}}
