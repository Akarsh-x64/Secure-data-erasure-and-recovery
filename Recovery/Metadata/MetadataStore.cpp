#include "MetadataStore.h"
#include <algorithm>
#include <cctype>
namespace Recovery { namespace Metadata {
static std::string Ext(std::string s){ if(!s.empty()&&s[0]=='.')s.erase(0,1); std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return(char)std::tolower(c);}); return s; }
void MetadataStore::Clear(){m_records.clear();}
bool MetadataStore::Add(const Core::FileRecord&r){return m_records.emplace(r.id,r).second;}
bool MetadataStore::Upsert(const Core::FileRecord&r){m_records[r.id]=r;return true;}
bool MetadataStore::Get(uint64_t id,Core::FileRecord&r)const{auto i=m_records.find(id);if(i==m_records.end())return false;r=i->second;return true;}
std::vector<Core::FileRecord> MetadataStore::All()const{std::vector<Core::FileRecord>v;for(auto&x:m_records)v.push_back(x.second);std::sort(v.begin(),v.end(),[](auto&a,auto&b){return a.id<b.id;});return v;}
std::vector<Core::FileRecord> MetadataStore::FindFilename(const std::string&s)const{std::vector<Core::FileRecord>v;for(auto&x:m_records)if(x.second.filename==s)v.push_back(x.second);return v;}
std::vector<Core::FileRecord> MetadataStore::FindExtension(const std::string&s)const{auto q=Ext(s);std::vector<Core::FileRecord>v;for(auto&x:m_records)if(Ext(x.second.extension)==q)v.push_back(x.second);return v;}
std::vector<Core::FileRecord> MetadataStore::FindPathContains(const std::string&s)const{std::vector<Core::FileRecord>v;for(auto&x:m_records)if(x.second.path.find(s)!=std::string::npos)v.push_back(x.second);return v;}
std::vector<Core::FileRecord> MetadataStore::FindSizeBetween(uint64_t a,uint64_t b)const{std::vector<Core::FileRecord>v;for(auto&x:m_records)if(x.second.size>=a&&x.second.size<=b)v.push_back(x.second);return v;}
std::vector<Core::FileRecord> MetadataStore::FindDeleted(bool q)const{std::vector<Core::FileRecord>v;for(auto&x:m_records)if(x.second.deleted==q)v.push_back(x.second);return v;}
std::vector<Core::FileRecord> MetadataStore::FindOrphaned(bool q)const{std::vector<Core::FileRecord>v;for(auto&x:m_records)if(x.second.orphaned==q)v.push_back(x.second);return v;}
}}
