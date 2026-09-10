#pragma once

#include "../../Core/IFileSystemDriver.h"
#include "../../Core/IHardwareController.h"
#include "ext4_Structures.h"
#include <vector>
#include <string>

namespace Erasure {
namespace FileSystems {

class Ext4Driver : public Core::IFileSystemDriver {
private:
    Core::IHardwareController* m_hardware;

    // Cached Superblock & Geometry
    Ext4::Ext4Superblock m_sb;
    uint32_t m_blockSize;
    uint32_t m_sectorsPerBlock;
    uint32_t m_bytesPerSector;
    uint32_t m_groupCount;
    uint16_t m_inodeSize;
    bool     m_is64Bit;

    // In-memory Group Descriptor Table
    std::vector<Ext4::Ext4GroupDesc64> m_groupDescriptors;

    // Conversions
    uint64_t BlockToSector(uint64_t block) const;
    bool ReadBlock(uint64_t block, void* buffer) const;
    bool WriteBlock(uint64_t block, const void* buffer);

    // Group Descriptor accessors (handles 32-bit vs 64-bit descriptors)
    uint64_t GetBlockBitmapBlock(uint32_t group) const;
    uint64_t GetInodeBitmapBlock(uint32_t group) const;
    uint64_t GetInodeTableBlock(uint32_t group) const;

    // Inode operations
    bool ReadInode(uint32_t inodeNum, Ext4::Ext4Inode& outInode) const;
    bool WriteInode(uint32_t inodeNum, const Ext4::Ext4Inode& inInode);
    bool WipeInodeOnDisk(uint32_t inodeNum);

    // Extent & block resolution
    std::vector<uint64_t> GetInodeAllocatedBlocks(const Ext4::Ext4Inode& inode) const;
    void CollectExtentBlocks(uint64_t extentBlock, std::vector<uint64_t>& outBlocks) const;

    // Bitmap manipulation
    bool ClearBlockBitmapBit(uint64_t blockNum);
    bool ClearInodeBitmapBit(uint32_t inodeNum);

    // Path & Directory traversal
    std::vector<std::string> TokenizePath(const std::string& path) const;

    struct DirectorySearchResult {
        bool found;
        bool isDirectory;
        uint32_t inodeNum;
        uint64_t dirBlock;
        size_t entryOffsetInBlock;
        uint16_t recLen;
    };

    DirectorySearchResult FindEntryInDirectory(const Ext4::Ext4Inode& dirInode, const std::string& targetName) const;

public:
    explicit Ext4Driver(Core::IHardwareController* hardware);
    ~Ext4Driver() override = default;

    bool Mount() override;
    bool EraseFile(const std::string& relativePath) override;
    bool WipeVolume() override;

    // Diagnostics & Inspection
    void PrintSuperblockInfo() const;
    const Ext4::Ext4Superblock& GetSuperblock() const { return m_sb; }
    uint32_t GetBlockSize() const { return m_blockSize; }
    uint32_t GetGroupCount() const { return m_groupCount; }
};

} // namespace FileSystems
} // namespace Erasure
