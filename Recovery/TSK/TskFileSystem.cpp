#include "TskFileSystem.h"
#include <algorithm>
#include <limits>
namespace Recovery { namespace TSK {
TskFileSystem::TskFileSystem(Core::IReadOnlyStorage&s,const Core::StorageRegion&p):m_storage(s),m_partition(p),m_bridge(s){}
TskFileSystem::~TskFileSystem() {
#if RECOVERY_HAS_LIBTSK
    if (m_fs) { tsk_fs_close(m_fs); m_fs = nullptr; }
#endif
    m_bridge.Close();
}
bool TskFileSystem::IsTskAvailable() const {
#if RECOVERY_HAS_LIBTSK
    return true;
#else
    return false;
#endif
}
std::string TskFileSystem::GetTskVersion() const {
#if RECOVERY_HAS_LIBTSK
    const char* v = tsk_version_get_str();
    return v ? v : "unknown";
#else
    return "libtsk-not-compiled";
#endif
}
bool TskFileSystem::Mount(){
 if(m_mounted)return true;
#if RECOVERY_HAS_LIBTSK
 m_lastError.clear();m_status={};m_records.clear();m_recordIndexById.clear();m_recordIdByTskMeta.clear();m_store.Clear();m_nextRecordId=1;
 if(!ValidatePartition()){SetError("Invalid partition/storage region");return false;}
 if(!m_bridge.Open()){SetError("Unable to open TSK image bridge");return false;}
 m_fs=tsk_fs_open_img(m_bridge.GetImageHandle(),static_cast<TSK_OFF_T>(m_partition.startOffset),TSK_FS_TYPE_DETECT);
 if(!m_fs){SetTskError("TSK filesystem open failed");return false;}
 const auto mf=static_cast<TSK_FS_META_FLAG_ENUM>(TSK_FS_META_FLAG_ALLOC|TSK_FS_META_FLAG_UNALLOC|TSK_FS_META_FLAG_ORPHAN);
 if(m_fs->last_inum>=m_fs->first_inum){
   if(tsk_fs_meta_walk(m_fs,m_fs->first_inum,m_fs->last_inum,mf,&MetaWalkCallback,this)!=0)SetTskError("TSK metadata walk reported an error");
   else m_status.metadataWalkCompleted=true;
 }
 const auto df=static_cast<TSK_FS_DIR_WALK_FLAG_ENUM>(TSK_FS_DIR_WALK_FLAG_RECURSE|TSK_FS_DIR_WALK_FLAG_ALLOC|TSK_FS_DIR_WALK_FLAG_UNALLOC);
 if(tsk_fs_dir_walk(m_fs,m_fs->root_inum,df,&DirWalkCallback,this)!=0)SetTskError("TSK directory walk reported an error");else m_status.directoryWalkCompleted=true;
 SyncStore();m_status.recordsProduced=m_records.size();m_mounted=true;return true;
#else
 SetError("libtsk was not available at compile time");return false;
#endif
}
Core::FileSystemType TskFileSystem::GetType() const {
#if RECOVERY_HAS_LIBTSK
    return m_fs ? MapFsType(m_fs->ftype) : Core::FileSystemType::Unknown;
#else
    return Core::FileSystemType::Unknown;
#endif
}
bool TskFileSystem::EnumerateFiles(std::vector<Core::FileRecord>&r){if(!m_mounted)return false;r.insert(r.end(),m_records.begin(),m_records.end());return true;}
bool TskFileSystem::GetFileMetadata(uint64_t id,Core::FileRecord&r){auto i=m_recordIndexById.find(id);if(!m_mounted||i==m_recordIndexById.end())return false;r=m_records[i->second];return true;}
bool TskFileSystem::GetDataRanges(uint64_t id,std::vector<Core::DataRange>&r){auto i=m_recordIndexById.find(id);if(!m_mounted||i==m_recordIndexById.end())return false;r=m_records[i->second].dataRanges;return true;}
bool TskFileSystem::ReadFile(uint64_t id,std::vector<uint8_t>&out){
#if RECOVERY_HAS_LIBTSK
 out.clear();if(!m_mounted||!m_fs)return false;auto i=m_recordIndexById.find(id);if(i==m_recordIndexById.end())return false;const auto&r=m_records[i->second];if(r.size>std::numeric_limits<size_t>::max())return false;
 TSK_FS_FILE*f=tsk_fs_file_open_meta(m_fs,nullptr,static_cast<TSK_INUM_T>(r.filesystemRecordId));if(!f||!f->meta)return false;out.resize((size_t)r.size);uint64_t off=0;const size_t chunk=1024*1024;
 while(off<r.size){size_t want=(size_t)std::min<uint64_t>(chunk,r.size-off);ssize_t got=tsk_fs_file_read(f,(TSK_OFF_T)off,(char*)out.data()+off,want,(TSK_FS_FILE_READ_FLAG_ENUM)0);if(got<0){tsk_fs_file_close(f);out.clear();return false;}if(got==0)break;off+=(uint64_t)got;}tsk_fs_file_close(f);return true;
#else
(void)id;out.clear();return false;
#endif
}
#if RECOVERY_HAS_LIBTSK
TSK_WALK_RET_ENUM TskFileSystem::MetaWalkCallback(TSK_FS_FILE*f,void*c){if(!c)return TSK_WALK_CONT;static_cast<TskFileSystem*>(c)->ProcessMetadataEntry(f);return TSK_WALK_CONT;}
TSK_WALK_RET_ENUM TskFileSystem::DirWalkCallback(TSK_FS_FILE*f,const char*p,void*c){if(!c)return TSK_WALK_CONT;static_cast<TskFileSystem*>(c)->ProcessDirectoryEntry(f,p);return TSK_WALK_CONT;}
void TskFileSystem::ProcessMetadataEntry(TSK_FS_FILE*f){++m_status.metadataEntriesSeen;if(!f||!f->meta){++m_status.recordsSkipped;return;}uint64_t id;if(!EnsureRecordForMetadata(f,id)){++m_status.recordsSkipped;return;}auto&i=m_records[m_recordIndexById[id]];PopulateMetadata(i,f);PopulateNamesFromMetadata(i,f);std::vector<Core::DataRange>r;if(ExtractDataRanges(f,r))i.dataRanges=std::move(r);}
void TskFileSystem::ProcessDirectoryEntry(TSK_FS_FILE*f,const char*p){++m_status.directoryEntriesSeen;if(!f||!f->meta||!f->name||!f->name->name){++m_status.recordsSkipped;return;}std::string n=f->name->name;if(n=="."||n=="..")return;uint64_t id;if(!EnsureRecordForMetadata(f,id)){++m_status.recordsSkipped;return;}auto&i=m_records[m_recordIndexById[id]];PopulateMetadata(i,f);MergeDirectoryName(i,f,p);std::vector<Core::DataRange>r;if(ExtractDataRanges(f,r)&&!r.empty())i.dataRanges=std::move(r);}
bool TskFileSystem::EnsureRecordForMetadata(TSK_FS_FILE*f,uint64_t&id){if(!f||!f->meta)return false;uint64_t a=(uint64_t)f->meta->addr;auto x=m_recordIdByTskMeta.find(a);if(x!=m_recordIdByTskMeta.end()){id=x->second;++m_status.recordsMerged;return true;}Core::FileRecord r;r.id=m_nextRecordId++;r.filesystemRecordId=a;PopulateMetadata(r,f);id=r.id;m_recordIdByTskMeta[a]=id;m_recordIndexById[id]=m_records.size();m_records.push_back(std::move(r));return true;}
void TskFileSystem::PopulateMetadata(Core::FileRecord&r,TSK_FS_FILE*f){const auto*m=f->meta;r.filesystem=MapFsType(f->fs_info->ftype);r.filesystemRecordId=(uint64_t)m->addr;r.sequenceNumber=m->seq;r.metadataFlags=(uint32_t)m->flags;r.metadataType=(uint32_t)m->type;r.size=m->size>0?(uint64_t)m->size:0;r.createdTime=m->crtime>0?(uint64_t)m->crtime:0;r.modifiedTime=m->mtime>0?(uint64_t)m->mtime:0;r.accessedTime=m->atime>0?(uint64_t)m->atime:0;r.changeTime=m->ctime>0?(uint64_t)m->ctime:0;r.deletionTime=0;r.createdTimeNanos=m->crtime_nano;r.modifiedTimeNanos=m->mtime_nano;r.accessedTimeNanos=m->atime_nano;r.changeTimeNanos=m->ctime_nano;r.uid=(uint64_t)m->uid;r.gid=(uint64_t)m->gid;r.linkCount=m->nlink;r.isDirectory=(m->type==TSK_FS_META_TYPE_DIR||m->type==TSK_FS_META_TYPE_VIRT_DIR);r.allocated=(m->flags&TSK_FS_META_FLAG_ALLOC)!=0;r.deleted=(m->flags&TSK_FS_META_FLAG_UNALLOC)!=0;r.orphaned=(m->flags&TSK_FS_META_FLAG_ORPHAN)!=0;r.isCompressed=(m->flags&TSK_FS_META_FLAG_COMP)!=0;r.symbolicLinkTarget=m->link?m->link:"";if(r.deleted)r.allocated=false;if(f->name){r.nameFlags=(uint32_t)f->name->flags;r.parentRecordId=(uint64_t)f->name->par_addr;r.parentSequence=f->name->par_seq;if(f->name->flags&TSK_FS_NAME_FLAG_UNALLOC){r.deleted=true;r.allocated=false;}}ExtractAttributes(f,r);}
void TskFileSystem::PopulateNamesFromMetadata(Core::FileRecord&r,TSK_FS_FILE*f){for(auto*n=f->meta->name2;n;n=n->next){if(!n->name[0])continue;Core::FileNameRecord x;x.name=n->name;x.parentRecordId=(uint64_t)n->par_inode;x.parentSequence=n->par_seq;x.metadataSequence=f->meta->seq;x.allocated=r.allocated;x.deleted=r.deleted;if(!r.parentRecordId){r.parentRecordId=x.parentRecordId;r.parentSequence=x.parentSequence;}auto d=std::find_if(r.names.begin(),r.names.end(),[&](const auto&e){return e.name==x.name&&e.parentRecordId==x.parentRecordId;});if(d==r.names.end())r.names.push_back(std::move(x));}if(r.filename.empty()&&!r.names.empty())r.filename=r.names.front().name;}
void TskFileSystem::MergeDirectoryName(Core::FileRecord&r,TSK_FS_FILE*f,const char*p){Core::FileNameRecord x;x.name=f->name->name;x.shortName=f->name->shrt_name?f->name->shrt_name:"";x.path=BuildPath(p,x.name.c_str());x.parentRecordId=(uint64_t)f->name->par_addr;x.parentSequence=f->name->par_seq;x.metadataSequence=f->name->meta_seq;x.allocated=(f->name->flags&TSK_FS_NAME_FLAG_ALLOC)!=0;x.deleted=(f->name->flags&TSK_FS_NAME_FLAG_UNALLOC)!=0;r.nameFlags=(uint32_t)f->name->flags;r.parentRecordId=x.parentRecordId;r.parentSequence=x.parentSequence;if(r.filename.empty()){r.filename=x.name;r.path=x.path;auto d=r.filename.find_last_of('.');if(d!=std::string::npos&&d+1<r.filename.size())r.extension=r.filename.substr(d+1);}else if(r.path.empty())r.path=x.path;if(x.deleted){r.deleted=true;r.allocated=false;}r.orphaned=r.orphaned||(x.parentRecordId==0||x.parentRecordId==(uint64_t)r.filesystemRecordId);auto d=std::find_if(r.names.begin(),r.names.end(),[&](const auto&e){return e.name==x.name&&e.parentRecordId==x.parentRecordId&&e.path==x.path;});if(d==r.names.end())r.names.push_back(std::move(x));}
bool TskFileSystem::ExtractAttributes(TSK_FS_FILE*f,Core::FileRecord&r){r.attributes.clear();int n=tsk_fs_file_attr_getsize(f);if(n<0)return false;bool any=false;for(int i=0;i<n;i++){const auto*a=tsk_fs_file_attr_get_idx(f,i);if(!a)continue;Core::FileAttributeRecord x;x.id=a->id;x.type=(uint32_t)a->type;x.flags=(uint32_t)a->flags;x.name=a->name?a->name:"";x.size=a->size>0?(uint64_t)a->size:0;x.allocatedSize=a->nrd.allocsize>0?(uint64_t)a->nrd.allocsize:0;x.initializedSize=a->nrd.initsize>0?(uint64_t)a->nrd.initsize:0;x.compressionSize=a->nrd.compsize;x.skipLength=a->nrd.skiplen;x.resident=(a->flags&TSK_FS_ATTR_RES)!=0;x.compressed=(a->flags&TSK_FS_ATTR_COMP)!=0;if(!x.resident&&a->nrd.run)for(auto*run=a->nrd.run;run;run=run->next)if(run->flags&TSK_FS_ATTR_RUN_FLAG_SPARSE)x.sparse=true;r.isCompressed|=x.compressed;r.isSparse|=x.sparse;r.attributes.push_back(std::move(x));any=true;}return any;}
bool TskFileSystem::ExtractDataRanges(TSK_FS_FILE*f,std::vector<Core::DataRange>&out)const{out.clear();if(!f||!f->meta||!m_fs||!m_fs->block_size)return false;int n=tsk_fs_file_attr_getsize(f);if(n<0)return false;for(int i=0;i<n;i++){const auto*a=tsk_fs_file_attr_get_idx(f,i);if(!a||(a->flags&TSK_FS_ATTR_NONRES)==0||!a->nrd.run)continue;uint64_t logical=0;for(auto*run=a->nrd.run;run;run=run->next){if(!run->len)continue;uint64_t blocks=(uint64_t)run->len;if(blocks>UINT64_MAX/(uint64_t)m_fs->block_size)continue;uint64_t len=blocks*(uint64_t)m_fs->block_size;bool sparse=(run->flags&TSK_FS_ATTR_RUN_FLAG_SPARSE)!=0||run->addr==0;bool comp=(a->flags&TSK_FS_ATTR_COMP)!=0;if(sparse){AddDataRange(out,0,len,logical,true,comp,a->id);}else{uint64_t rel=(uint64_t)run->addr;if(rel>UINT64_MAX/(uint64_t)m_fs->block_size)continue;rel*=(uint64_t)m_fs->block_size;if(rel>m_partition.size||len>m_partition.size-rel)continue;if(m_partition.startOffset>UINT64_MAX-rel)continue;AddDataRange(out,m_partition.startOffset+rel,len,logical,false,comp,a->id);}if(logical>UINT64_MAX-len)break;logical+=len;}}std::sort(out.begin(),out.end(),[](const auto&a,const auto&b){return a.logicalOffset<b.logicalOffset;});return true;}
bool TskFileSystem::AddDataRange(std::vector<Core::DataRange>&v,uint64_t o,uint64_t l,uint64_t lo,bool s,bool c,uint16_t a)const{if(!l||(!s&&!ValidateRange(o,l)))return false;v.emplace_back(o,l,lo,s,c,a);return true;}
Core::FileSystemType TskFileSystem::MapFsType(TSK_FS_TYPE_ENUM t)const{if(TSK_FS_TYPE_ISNTFS(t))return Core::FileSystemType::NTFS;if(t==TSK_FS_TYPE_EXFAT)return Core::FileSystemType::ExFAT;if(TSK_FS_TYPE_ISFAT(t))return Core::FileSystemType::FAT32;if(TSK_FS_TYPE_ISEXT(t)){if(t==TSK_FS_TYPE_EXT2)return Core::FileSystemType::EXT2;if(t==TSK_FS_TYPE_EXT3)return Core::FileSystemType::EXT3;return Core::FileSystemType::EXT4;}if(TSK_FS_TYPE_ISHFS(t))return Core::FileSystemType::HFSPlus;if(TSK_FS_TYPE_ISAPFS(t))return Core::FileSystemType::APFS;return Core::FileSystemType::Unknown;}
std::string TskFileSystem::BuildPath(const char*b,const char*f)const{if(!f)return{};if(!b||!*b)return f;std::string s=b;if(s.back()!='/'&&s.back()!='\\')s+='/';s+=f;return s;}
#endif
void TskFileSystem::SetError(const std::string&s){m_lastError=s;m_status.lastError=s;}
#if RECOVERY_HAS_LIBTSK
void TskFileSystem::SetTskError(const std::string&p){const char*e=tsk_error_get();SetError(e&&*e?p+": "+e:p);}
#endif
void TskFileSystem::SyncStore(){m_store.Clear();for(auto&r:m_records)m_store.Upsert(r);}
#if RECOVERY_HAS_LIBTSK
bool TskFileSystem::ValidatePartition()const{uint64_t n=m_storage.GetSize();return m_partition.size&&m_partition.startOffset<=n&&m_partition.size<=n-m_partition.startOffset;}
bool TskFileSystem::ValidateRange(uint64_t o,uint64_t l)const{uint64_t n=m_storage.GetSize();if(!l||o>n||l>n-o)return false;uint64_t pe=m_partition.startOffset+m_partition.size;if(pe<m_partition.startOffset)return false;if(o<m_partition.startOffset||o>pe)return false;return l<=pe-o;}
#endif
}}
