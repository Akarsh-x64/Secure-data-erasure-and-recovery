#include "OoxmlVerifier.h"
#include <algorithm>
#include <cctype>

namespace Recovery { namespace Carving { namespace Verification {
namespace {
void Check(VerificationResult& r,const char* n,CheckStatus s,const std::string& x,double w=1.0) {
    r.AddCheck(VerificationCheck(n,s,x,w));
}
std::string Lower(std::string s) {
    for (char& c:s) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool Ext(const std::string& hint,const char* suffix) {
    const std::string h=Lower((!hint.empty() && hint[0] == '.') ? hint : "." + hint), s=suffix;
    return h.size()>=s.size() && h.compare(h.size()-s.size(),s.size(),s)==0;
}
}
bool OoxmlVerifier::CanVerify(const std::vector<uint8_t>& b,const std::string& hint) const {
    if (Ext(hint,".docx")||Ext(hint,".xlsx")||Ext(hint,".pptx")) return true;
    std::vector<ZipEntry> e;
    if (!ZipVerifier::ListEntries(b,e)) return false;
    bool types=false, document=false;
    for (const auto& x:e) {
        types = types || x.name=="[Content_Types].xml";
        document = document || x.name=="word/document.xml" ||
            x.name=="xl/workbook.xml" || x.name=="ppt/presentation.xml";
    }
    return types && document;
}
VerificationResult OoxmlVerifier::Verify(const std::vector<uint8_t>& b,const std::string& path) const {
    VerificationResult r; r.detectedType="OOXML"; r.path=path;
    std::vector<ZipEntry> entries; std::string error;
    const bool zip=ZipVerifier::ListEntries(b,entries,&error);
    Check(r,"ZIP container",zip?CheckStatus::PASS:CheckStatus::FAIL,
          zip?"OOXML container has a bounded ZIP directory.":error,3.0);
    if (!zip) {
        r.classification=VerificationClassification::CORRUPTED;
        r.explanation="OOXML requires a structurally complete ZIP container.";
        return r;
    }
    bool types=false, doc=false, sheet=false, presentation=false, rels=false;
    for (const auto& e:entries) {
        types |= e.name=="[Content_Types].xml";
        doc |= e.name=="word/document.xml";
        sheet |= e.name=="xl/workbook.xml";
        presentation |= e.name=="ppt/presentation.xml";
        rels |= e.name=="_rels/.rels";
    }
    const bool known=doc||sheet||presentation;
    if (doc) r.detectedType="DOCX";
    else if (sheet) r.detectedType="XLSX";
    else if (presentation) r.detectedType="PPTX";
    Check(r,"Content types",types?CheckStatus::PASS:CheckStatus::FAIL,
          types?"[Content_Types].xml is present.":"Required OOXML content types part is missing.",2.0);
    Check(r,"Package part",known?CheckStatus::PASS:CheckStatus::FAIL,
          doc?"Word document part is present.":(sheet?"Excel workbook part is present.":
          (presentation?"PowerPoint presentation part is present.":"No recognized main OOXML part.")),3.0);
    Check(r,"Package relationships",rels?CheckStatus::PASS:CheckStatus::NOT_APPLICABLE,
          rels?"Root relationships part is present.":"Relationships are optional for this structural check.");
    r.classification=(types&&known)?VerificationClassification::VALID:
        VerificationClassification::PARTIAL;
    r.explanation=(types&&known)?"ZIP and required OOXML package parts are present.":
        "ZIP is readable but required OOXML parts are incomplete.";
    return r;
}
}}}
