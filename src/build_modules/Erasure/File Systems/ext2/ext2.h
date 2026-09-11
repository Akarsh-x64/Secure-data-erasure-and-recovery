#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../../Core/IFileSystemDriver.h"
#include "../../Core/IHardwareController.h"
#include "ext2_Structures.h"

namespace Erasure {
namespace FileSystems {

    class Ext2Driver : public Core::IFileSystemDriver {
    private:
        Core::IHardwareController* m_hardware;

        Ext2SuperBlock m_superblock;

        uint32_t m_block_size;
        uint16_t m_inode_size;

        uint32_t m_blocksPerGroup;
        uint32_t m_inodesPerGroup;

        // Sector & Address Translation Helpers
        uint64_t InodeToSector(uint32_t inode) const;
        uint64_t BlockToSector(uint32_t blockNum) const;

        // Inode I/O Helpers
        bool ReadInode(uint32_t inodeNum, Ext2Inode& outInode) const;
        bool WriteInode(uint32_t inodeNum, const Ext2Inode& inode);

        // Bitmap Manipulation Helpers
        bool ClearBlockBitMapBit(uint32_t blockNum);
        bool ClearInodeBitMapBit(uint32_t inodeNum);

        // Group Descriptor Table Helper
        bool GetGroupDescriptor(uint32_t groupNumber, Ext2GroupDescriptor& outDescriptor) const;

        // Block Resolution Helpers (Direct, Indirect, Double/Triple Indirect)
        std::vector<uint32_t> GetInodeBlocks(const Ext2Inode& inode) const;
        std::vector<uint32_t> GetInodeBlocks(uint32_t inodeNum) const;
        std::vector<uint32_t> GetAllInodeBlocksIncludingIndirect(const Ext2Inode& inode) const;
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
        explicit Ext2Driver(Core::IHardwareController* hardware);
        ~Ext2Driver() override = default;

        bool Mount() override;
        bool EraseFile(const std::string& relativePath) override;
        bool WipeVolume() override;

        void PrintSuperBlockInfo() const;
    };

} // namespace FileSystems
} // namespace Erasure
