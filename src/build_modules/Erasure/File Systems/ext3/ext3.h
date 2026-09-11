#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../../Core/IFileSystemDriver.h"
#include "../../Core/IHardwareController.h"
#include "ext3_Structures.h"

namespace Erasure {
namespace FileSystems {

    class Ext3Driver : public Core::IFileSystemDriver {
    private:
        Core::IHardwareController* m_hardware;

        Ext3SuperBlock m_superblock;

        uint32_t m_block_size;
        uint16_t m_inode_size;

        uint32_t m_blocksPerGroup;
        uint32_t m_inodesPerGroup;

        // Sector & Address Translation Helpers
        uint64_t InodeToSector(uint32_t inode) const;
        uint64_t BlockToSector(uint32_t blockNum) const;

        // Inode I/O Helpers
        bool ReadInode(uint32_t inodeNum, Ext3Inode& outInode) const;
        bool WriteInode(uint32_t inodeNum, const Ext3Inode& inode);

        // Bitmap Manipulation Helpers
        bool ClearBlockBitMapBit(uint32_t blockNum);
        bool ClearInodeBitMapBit(uint32_t inodeNum);

        // Group Descriptor Table Helper
        bool GetGroupDescriptor(uint32_t groupNumber, Ext3GroupDescriptor& outDescriptor) const;

        // Block Resolution Helpers (Direct, Indirect, Double/Triple Indirect)
        std::vector<uint32_t> GetInodeBlocks(const Ext3Inode& inode) const;
        std::vector<uint32_t> GetInodeBlocks(uint32_t inodeNum) const;
        std::vector<uint32_t> GetAllInodeBlocksIncludingIndirect(const Ext3Inode& inode) const;
        std::vector<uint32_t> ReadBlockAsPointers(uint32_t blockNum) const;

        // Path & Directory Traversal Helpers
        std::vector<std::string> TokenizePath(const std::string& path) const;

        struct SearchResult {
            bool found;
            bool isDirectory;

            uint32_t targetInode;      
            uint32_t dirBlockNumber;   
            size_t entryOffset;        
            uint16_t previousRecLen;   
            size_t previousEntryOffset;
        };

        SearchResult FindEntryInDirectory(const std::vector<uint32_t>& dirBlocks, const std::string& targetName) const;

    public:
        explicit Ext3Driver(Core::IHardwareController* hardware);
        ~Ext3Driver() override = default;

        bool Mount() override;
        bool EraseFile(const std::string& relativePath) override;
        bool WipeVolume() override;

        void PrintSuperBlockInfo() const;
    };

} // namespace FileSystems
} // namespace Erasure
