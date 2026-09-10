#include "../../Core/VerificationResult.h"
#include "../../Verification/VerificationEngine.h"
#include "../../Verification/Sha256.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using Recovery::Carving::CheckStatus;
using Recovery::Carving::VerificationClassification;
using Recovery::Carving::VerificationEngine;
using Recovery::Carving::VerificationResult;

namespace {
void LE16(std::vector<uint8_t>& b,uint16_t x) { b.push_back(uint8_t(x)); b.push_back(uint8_t(x>>8)); }
void LE32(std::vector<uint8_t>& b,uint32_t x) {
    b.push_back(uint8_t(x)); b.push_back(uint8_t(x>>8));
    b.push_back(uint8_t(x>>16)); b.push_back(uint8_t(x>>24));
}
void BE32(std::vector<uint8_t>& b,uint32_t x) {
    b.push_back(uint8_t(x>>24)); b.push_back(uint8_t(x>>16));
    b.push_back(uint8_t(x>>8)); b.push_back(uint8_t(x));
}
uint32_t Crc(const uint8_t* p,size_t n) {
    uint32_t c=0xffffffffu;
    for(size_t i=0;i<n;++i) { c^=p[i]; for(int j=0;j<8;++j)c=(c>>1)^(0xedb88320u&-(c&1u)); }
    return ~c;
}
void Bytes(std::vector<uint8_t>& b,const char* s) {
    b.insert(b.end(),s,s+std::strlen(s));
}
std::vector<uint8_t> Png() {
    std::vector<uint8_t> b={0x89,'P','N','G',0x0d,0x0a,0x1a,0x0a};
    BE32(b,13); Bytes(b,"IHDR"); BE32(b,1); BE32(b,1);
    b.push_back(8); b.push_back(2); b.push_back(0); b.push_back(0); b.push_back(0);
    const size_t ihdr=12; BE32(b,Crc(b.data()+ihdr,17));
    BE32(b,3); Bytes(b,"IDAT"); b.push_back(0x78); b.push_back(0x9c); b.push_back(0x03);
    BE32(b,Crc(b.data()+b.size()-7,7));
    BE32(b,0); Bytes(b,"IEND"); BE32(b,Crc(b.data()+b.size()-4,4));
    return b;
}
std::vector<uint8_t> Zip(const std::vector<std::string>& names) {
    std::vector<uint8_t> b; std::vector<uint32_t> offsets;
    for(const auto& name:names) {
        offsets.push_back(static_cast<uint32_t>(b.size()));
        LE32(b,0x04034b50); LE16(b,20); LE16(b,0); LE16(b,0);
        LE16(b,0); LE16(b,0); LE32(b,0); LE32(b,0); LE32(b,0);
        LE16(b,static_cast<uint16_t>(name.size())); LE16(b,0); Bytes(b,name.c_str());
    }
    const uint32_t cdOffset=static_cast<uint32_t>(b.size());
    for(size_t i=0;i<names.size();++i) {
        const auto& name=names[i];
        LE32(b,0x02014b50); LE16(b,20); LE16(b,20); LE16(b,0); LE16(b,0);
        LE16(b,0); LE16(b,0); LE32(b,0); LE32(b,0); LE32(b,0);
        LE16(b,static_cast<uint16_t>(name.size())); LE16(b,0); LE16(b,0);
        LE16(b,0); LE16(b,0); LE32(b,0); LE32(b,offsets[i]); Bytes(b,name.c_str());
    }
    const uint32_t cdSize=static_cast<uint32_t>(b.size())-cdOffset;
    LE32(b,0x06054b50); LE16(b,0); LE16(b,0); LE16(b,static_cast<uint16_t>(names.size()));
    LE16(b,static_cast<uint16_t>(names.size())); LE32(b,cdSize); LE32(b,cdOffset); LE16(b,0);
    return b;
}
std::vector<uint8_t> StoredZip(const std::string& name, const std::string& payload,
                               bool corruptCrc = false) {
    std::vector<uint8_t> b;
    const uint32_t crc = corruptCrc ? 1u :
        Crc(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());
    LE32(b,0x04034b50); LE16(b,20); LE16(b,0); LE16(b,0);
    LE16(b,0); LE16(b,0); LE32(b,crc); LE32(b,payload.size()); LE32(b,payload.size());
    LE16(b,static_cast<uint16_t>(name.size())); LE16(b,0); Bytes(b,name.c_str());
    Bytes(b,payload.c_str());
    const uint32_t cdOffset = static_cast<uint32_t>(b.size());
    LE32(b,0x02014b50); LE16(b,20); LE16(b,20); LE16(b,0); LE16(b,0);
    LE16(b,0); LE16(b,0); LE32(b,crc); LE32(b,payload.size()); LE32(b,payload.size());
    LE16(b,static_cast<uint16_t>(name.size())); LE16(b,0); LE16(b,0);
    LE16(b,0); LE16(b,0); LE32(b,0); LE32(b,0); Bytes(b,name.c_str());
    const uint32_t cdSize = static_cast<uint32_t>(b.size()) - cdOffset;
    LE32(b,0x06054b50); LE16(b,0); LE16(b,0); LE16(b,1); LE16(b,1);
    LE32(b,cdSize); LE32(b,cdOffset); LE16(b,0);
    return b;
}
std::vector<uint8_t> Iso() {
    std::vector<uint8_t> b;
    BE32(b,20); Bytes(b,"ftyp"); Bytes(b,"isom"); BE32(b,0); Bytes(b,"isom");
    BE32(b,8); Bytes(b,"moov"); BE32(b,8); Bytes(b,"mdat"); return b;
}
void AssertHas(const VerificationResult& r,const std::string& name,CheckStatus status) {
    for(const auto& c:r.checks) if(c.name==name) { assert(c.status==status); return; }
    assert(false);
}
void TestSha() {
    std::vector<uint8_t> abc={'a','b','c'};
    assert(Recovery::Carving::Sha256::Compute(abc) ==
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}
void TestImageAndPdf() {
    VerificationEngine engine;
    const auto jpeg=engine.VerifyBytes({0xff,0xd8,0xff,0xd9},"jpg");
    assert(jpeg.classification==VerificationClassification::VALID);
    const auto damagedJpeg=engine.VerifyBytes({0xff,0xd8},"jpg");
    assert(damagedJpeg.classification==VerificationClassification::PARTIAL);
    assert(engine.VerifyBytes({0xff,0xd8,0xff,0x00,0xd9},"jpg").classification==
           VerificationClassification::CORRUPTED);
    assert(engine.VerifyBytes({0x00,0xd8,0xff,0xd9},"jpg").classification==
           VerificationClassification::INVALID);
    const auto png=engine.VerifyBytes(Png(),"png");
    assert(png.classification==VerificationClassification::VALID);
    auto bad=Png(); bad[29]^=1;
    assert(engine.VerifyBytes(bad,"png").classification==VerificationClassification::CORRUPTED);
    auto missingIend=Png(); missingIend.resize(missingIend.size()-12);
    assert(engine.VerifyBytes(missingIend,"png").classification==
           VerificationClassification::PARTIAL);
    auto badLength=Png(); badLength[8]=0xff;
    assert(engine.VerifyBytes(badLength,"png").classification==
           VerificationClassification::CORRUPTED);
    assert(engine.VerifyBytes({'%','P','D','F','-','1','.','7','\n'},"pdf").classification==
           VerificationClassification::PARTIAL);
    const std::string validPdf =
        "%PDF-1.7\n1 0 obj\n<< /Type /Catalog >>\nendobj\n"
        "xref\n0 1\n0000000000 65535 f \ntrailer\n<< /Root 1 0 R >>\n"
        "startxref\n9\n%%EOF\n";
    assert(engine.VerifyBytes(
        std::vector<uint8_t>(validPdf.begin(),validPdf.end()),"pdf").classification ==
        VerificationClassification::VALID);
    auto noEof=std::vector<uint8_t>(validPdf.begin(),validPdf.end());
    noEof.resize(noEof.size()-6);
    assert(engine.VerifyBytes(noEof,"pdf").classification==
           VerificationClassification::PARTIAL);
    AssertHas(engine.VerifyBytes({0xff,0xd8,0xff,0xd9},"png"),
              "Extension match",CheckStatus::FAIL);
}
void TestArchivesAndMovie() {
    VerificationEngine engine;
    const auto zip=Zip({"hello.txt"});
    assert(engine.VerifyBytes(zip,"zip").classification==VerificationClassification::VALID);
    auto shortZip=zip; shortZip.resize(shortZip.size()-2);
    assert(engine.VerifyBytes(shortZip,"zip").classification==VerificationClassification::PARTIAL);
    assert(engine.VerifyBytes(StoredZip("a.txt","payload"),"zip").classification==
           VerificationClassification::VALID);
    assert(engine.VerifyBytes(StoredZip("a.txt","payload",true),"zip").classification==
           VerificationClassification::PARTIAL);
    const auto docx=Zip({"[Content_Types].xml","_rels/.rels","word/document.xml"});
    assert(engine.VerifyBytes(docx,"docx").detectedType=="DOCX");
    assert(engine.VerifyBytes(docx,"docx").classification==VerificationClassification::VALID);
    const auto incompleteDocx=Zip({"[Content_Types].xml","_rels/.rels"});
    assert(engine.VerifyBytes(incompleteDocx,"docx").classification==
           VerificationClassification::PARTIAL);
    assert(engine.VerifyBytes(Iso(),"mp4").classification==VerificationClassification::VALID);
    auto bad=Iso(); bad[3]=0xff;
    assert(engine.VerifyBytes(bad,"mp4").classification==VerificationClassification::CORRUPTED);
    auto truncated=Iso(); truncated.resize(truncated.size()-1);
    assert(engine.VerifyBytes(truncated,"mp4").classification==
           VerificationClassification::CORRUPTED);
    const auto unknown=engine.VerifyBytes({'u','n','k'},"bin");
    assert(unknown.classification==VerificationClassification::UNKNOWN);
    assert(unknown.detectedType=="UNKNOWN");
}
void TestPathAndCandidate() {
    const std::string file="Recovery\\Carving\\Tests\\VerificationTest\\verification_fixture.png";
    { std::ofstream out(file.c_str(),std::ios::binary); const auto p=Png(); out.write(
        reinterpret_cast<const char*>(p.data()),static_cast<std::streamsize>(p.size())); }
    VerificationEngine engine;
    const auto pathResult=engine.VerifyPath(file);
    assert(pathResult.classification==VerificationClassification::VALID);
    Recovery::Carving::CarvingCandidate candidate; candidate.recoveredPath=file; candidate.fileType="png";
    assert(engine.Verify(candidate).classification==VerificationClassification::VALID);
    const auto first=engine.VerifyBytes(Png(),"png");
    const auto second=engine.VerifyBytes(Png(),"png");
    assert(first.score==second.score);
    assert(first.sha256==second.sha256);
    std::remove(file.c_str());
}
}
int main() {
    TestSha(); TestImageAndPdf(); TestArchivesAndMovie(); TestPathAndCandidate();
    return 0;
}
