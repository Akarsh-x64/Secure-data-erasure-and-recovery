#pragma once
#include "../Core/FileRecord.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
namespace Recovery { namespace Metadata {
class MetadataStore {
    std::unordered_map<uint64_t,Core::FileRecord> m_records;
public:
    void Clear(); bool Add(const Core::FileRecord&); bool Upsert(const Core::FileRecord&);
    bool Get(uint64_t,Core::FileRecord&) const; size_t Size() const { return m_records.size(); }
    std::vector<Core::FileRecord> All() const;
    std::vector<Core::FileRecord> FindFilename(const std::string&) const;
    std::vector<Core::FileRecord> FindExtension(const std::string&) const;
    std::vector<Core::FileRecord> FindPathContains(const std::string&) const;
    std::vector<Core::FileRecord> FindSizeBetween(uint64_t,uint64_t) const;
    std::vector<Core::FileRecord> FindDeleted(bool) const;
    std::vector<Core::FileRecord> FindOrphaned(bool) const;
};
}}
