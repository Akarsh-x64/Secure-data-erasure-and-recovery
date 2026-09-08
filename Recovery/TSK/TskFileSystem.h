#pragma once
#include "TskFeature.h"
#include "TskImageBridge.h"
#include "TskMetadata.h"
#include "../Filesystems/IRecoveryFileSystem.h"
#include "../Core/IReadOnlyStorage.h"
#include "../Core/StorageRegion.h"
#include "../Metadata/MetadataStore.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
namespace Recovery { namespace TSK {
class TskFileSystem : public Filesystems::IRecoveryFileSystem {
public:
    TskFileSystem(Core::IReadOnlyStorage&,const Core::StorageRegion&);
    ~TskFileSystem() override;
    TskFileSystem(const TskFileSystem&)=delete; TskFileSystem& operator=(const TskFileSystem&)=delete;
    bool Mount() override; Core::FileSystemType GetType() const override;
    bool EnumerateFiles(std::vector<Core::FileRecord>&) override;
    bool GetFileMetadata(uint64_t,Core::FileRecord&) override;
    bool GetDataRanges(uint64_t,std::vector<Core::DataRange>&) override;
    bool ReadFile(uint64_t,std::vector<uint8_t>&) override;
    bool IsTskAvailable() const; std::string GetTskVersion() const;
    const TskMetadataScanStatus& GetScanStatus() const{return m_status;}
    const std::string& GetLastError() const{return m_lastError;}
    const Metadata::MetadataStore& GetMetadataStore() const{return m_store;}
private:
    Core::IReadOnlyStorage& m_storage; Core::StorageRegion m_partition; TskImageBridge m_bridge;
    bool m_mounted=false; uint64_t m_nextRecordId=1; std::string m_lastError;
    std::vector<Core::FileRecord> m_records; std::unordered_map<uint64_t,size_t> m_recordIndexById;
    std::unordered_map<uint64_t,uint64_t> m_recordIdByTskMeta; Metadata::MetadataStore m_store; TskMetadataScanStatus m_status;
    void SetError(const std::string&);
    void SyncStore();
#if RECOVERY_HAS_LIBTSK
    TSK_FS_INFO* m_fs=nullptr;
    static TSK_WALK_RET_ENUM DirWalkCallback(TSK_FS_FILE*,const char*,void*);
    static TSK_WALK_RET_ENUM MetaWalkCallback(TSK_FS_FILE*,void*);
    void ProcessDirectoryEntry(TSK_FS_FILE*,const char*); void ProcessMetadataEntry(TSK_FS_FILE*);
    bool EnsureRecordForMetadata(TSK_FS_FILE*,uint64_t&); void PopulateMetadata(Core::FileRecord&,TSK_FS_FILE*);
    void PopulateNamesFromMetadata(Core::FileRecord&,TSK_FS_FILE*); void MergeDirectoryName(Core::FileRecord&,TSK_FS_FILE*,const char*);
    bool ExtractAttributes(TSK_FS_FILE*,Core::FileRecord&); bool ExtractDataRanges(TSK_FS_FILE*,std::vector<Core::DataRange>&) const;
    bool AddDataRange(std::vector<Core::DataRange>&,uint64_t,uint64_t,uint64_t,bool,bool,uint16_t) const;
    Core::FileSystemType MapFsType(TSK_FS_TYPE_ENUM) const; std::string BuildPath(const char*,const char*) const;
    void SetTskError(const std::string&);
    bool ValidatePartition() const; bool ValidateRange(uint64_t,uint64_t) const;
#endif
};
}}
