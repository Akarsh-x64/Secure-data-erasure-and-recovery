#include "VerificationEngine.h"
#include "Sha256.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>

namespace Recovery { namespace Carving { namespace Verification {
namespace {
std::string Lower(std::string s) {
    for(char& c:s)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string Extension(const std::string& path) {
    const size_t slash=path.find_last_of("/\\");
    const size_t dot=path.find_last_of('.');
    return dot==std::string::npos || (slash!=std::string::npos && dot<slash) ?
        std::string() : Lower(path.substr(dot));
}
bool Is(const std::string& e,const char* a,const char* b=nullptr) {
    return e==a || (b!=nullptr && e==b);
}
void Add(VerificationResult& r,const char* n,CheckStatus s,const std::string& x,double w=1.0) {
    r.AddCheck(VerificationCheck(n,s,x,w));
}
}

VerificationResult VerificationEngine::VerifyPath(const std::string& path,
                                                  const std::string& expected) const {
    VerificationResult r;
    r.path=path;
    std::ifstream input(path.c_str(),std::ios::binary|std::ios::ate);
    if(!input) {
        r.classification=VerificationClassification::UNKNOWN;
        r.explanation="Unable to open the candidate path.";
        Add(r,"File read",CheckStatus::FAIL,"Unable to open candidate path.",1.0);
        return r;
    }
    const std::streamoff end=input.tellg();
    if(end<0 || static_cast<uint64_t>(end)>std::numeric_limits<size_t>::max() ||
       static_cast<uint64_t>(end)>static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max())) {
        r.classification=VerificationClassification::UNKNOWN;
        r.explanation="Candidate is too large for this verifier.";
        Add(r,"File read",CheckStatus::FAIL,"Candidate size cannot be represented safely.",1.0);
        return r;
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(end));
    input.seekg(0,std::ios::beg);
    if(!bytes.empty() && !input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()))) {
        r.classification=VerificationClassification::UNKNOWN;
        r.explanation="Candidate could not be read completely.";
        Add(r,"File read",CheckStatus::FAIL,"Short read while reading candidate.",1.0);
        return r;
    }
    r.fileSize = bytes.size();
    r=VerifyBytes(bytes,expected.empty()?Extension(path):expected);
    r.path=path;
    r.fileSize = bytes.size();
    Add(r,"Existence/readability",CheckStatus::PASS,
        "Candidate exists and was read completely.",1.0);
    Add(r,"File size",CheckStatus::PASS,
        "Candidate size is " + std::to_string(bytes.size()) + " bytes.",0.5);
    r.RecalculateScore();
    return r;
}

VerificationResult VerificationEngine::Verify(const CarvingCandidate& candidate,
                                              bool compareExtension) const {
    const std::string hint=!candidate.fileType.empty()?candidate.fileType:
        Extension(candidate.recoveredPath);
    VerificationResult r=VerifyPath(candidate.recoveredPath,hint);
    if(!compareExtension) {
        // VerifyPath receives a hint for detection, but the comparison itself is optional.
        for(auto it=r.checks.begin();it!=r.checks.end();) {
            if(it->name=="Extension match") it=r.checks.erase(it); else ++it;
        }
        r.RecalculateScore();
    }
    return r;
}

VerificationResult VerificationEngine::VerifyBytes(const std::vector<uint8_t>& bytes,
                                                   const std::string& hint) const {
    VerificationResult base;
    base.fileSize = bytes.size();
    base.sha256 = Sha256::Compute(bytes);
    Add(base,"File size",CheckStatus::PASS,
        "Bounded input contains " + std::to_string(bytes.size()) + " bytes.",0.5);
    Add(base,"SHA-256",CheckStatus::PASS,"Computed SHA-256: "+base.sha256,0.5);
    OoxmlVerifier ooxml; PngVerifier png; JpegVerifier jpeg; PdfVerifier pdf;
    IsoBmffVerifier iso; ZipVerifier zip;
    const IFileVerifier* verifier=nullptr;
    // OOXML must precede generic ZIP because it is intentionally a ZIP subtype.
    if(ooxml.CanVerify(bytes,hint)) verifier=&ooxml;
    else if(png.CanVerify(bytes,hint)) verifier=&png;
    else if(jpeg.CanVerify(bytes,hint)) verifier=&jpeg;
    else if(pdf.CanVerify(bytes,hint)) verifier=&pdf;
    else if(iso.CanVerify(bytes,hint)) verifier=&iso;
    else if(zip.CanVerify(bytes,hint)) verifier=&zip;
    if(verifier==nullptr) {
        VerificationResult r = base; r.detectedType="UNKNOWN";
        r.classification=VerificationClassification::UNKNOWN;
        r.explanation="No supported format signature was detected.";
        Add(r,"Format signature",CheckStatus::FAIL,"No JPEG, PNG, PDF, ZIP, OOXML, or ISO BMFF signature.",2.0);
        r.RecalculateScore();
        return r;
    }
    VerificationResult result=verifier->Verify(bytes);
    result.fileSize = base.fileSize;
    result.sha256 = base.sha256;
    result.checks.insert(result.checks.begin(),base.checks.begin(),base.checks.end());
    if(!hint.empty()) AddExtensionCheck(result,hint);
    result.RecalculateScore();
    return result;
}

void VerificationEngine::AddExtensionCheck(VerificationResult& r,
                                            const std::string& expected) {
    std::string e=Lower(expected);
    if(!e.empty() && e[0]!='.') e="."+e;
    bool match=false;
    if(r.detectedType=="JPEG") match=Is(e,".jpg",".jpeg");
    else if(r.detectedType=="PNG") match=e==".png";
    else if(r.detectedType=="PDF") match=e==".pdf";
    else if(r.detectedType=="ZIP") match=e==".zip";
    else if(r.detectedType=="OOXML") match=Is(e,".docx",".xlsx")||e==".pptx";
    else if(r.detectedType=="DOCX") match=e==".docx";
    else if(r.detectedType=="XLSX") match=e==".xlsx";
    else if(r.detectedType=="PPTX") match=e==".pptx";
    else if(r.detectedType=="ISO BMFF") match=Is(e,".mp4",".mov")||Is(e,".m4v",".m4a");
    if(match) {
        Add(r,"Extension match",CheckStatus::PASS,"Expected extension matches detected content.",1.0);
    } else {
        Add(r,"Extension match",CheckStatus::FAIL,
            "Expected extension does not match detected content.",1.0);
        if(r.classification==VerificationClassification::VALID)
            r.classification=VerificationClassification::PARTIAL;
    }
}
}}}
