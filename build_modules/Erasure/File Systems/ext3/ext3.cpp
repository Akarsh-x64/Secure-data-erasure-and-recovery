#include "ext3.h"
#include <iostream>
#include <cstring>
#include <sstream>
#include <algorithm>

namespace Erasure {
namespace FileSystems {

    static constexpr uint32_t EXT3_ROOT_INO = 2;

    Ext3Driver::Ext3Driver(Core::IHardwareController* hardware)
        : m_hardware(hardware),
          m_block_size(0),
          m_inode_size(0),
          m_blocksPerGroup(0),
          m_inodesPerGroup(0) {
        std::memset(&m_superblock, 0, sizeof(m_superblock));
    }

    uint64_t Ext3Driver::InodeToSector(uint32_t inode) const {
        if (inode == 0 || inode > m_superblock.inode_count || m_inodesPerGroup == 0) {
            return 0;
        }

        uint32_t blockGroup = (inode - 1) / m_inodesPerGroup;
        uint32_t localInodeIndex = (inode - 1) % m_inodesPerGroup;

        Ext3GroupDescriptor bgd;
        if (!GetGroupDescriptor(blockGroup, bgd)) {
            return 0;
        }

        uint64_t tableBaseByteOffset = static_cast<uint64_t>(bgd.bg_inode_table) * m_block_size;
        uint64_t exactInodeByteOffset = tableBaseByteOffset + (static_cast<uint64_t>(localInodeIndex) * m_inode_size);
        uint32_t bytesPerSector = m_hardware->GetGeometry().bytesPerSector;
        if (bytesPerSector == 0) return 0;

        return exactInodeByteOffset / bytesPerSector;
    }

    uint64_t Ext3Driver::BlockToSector(uint32_t blockNum) const {
        uint32_t bytesPerSector = m_hardware->GetGeometry().bytesPerSector;
        if (bytesPerSector == 0) return 0;

        return (static_cast<uint64_t>(blockNum) * m_block_size) / bytesPerSector;
    }

    bool Ext3Driver::Mount() {
        if (!m_hardware) return false;

        Core::DeviceGeometry geo = m_hardware->GetGeometry();
        if (geo.bytesPerSector == 0) return false;

        uint64_t startSector = 1024 / geo.bytesPerSector;
        uint32_t offsetInSector = 1024 % geo.bytesPerSector;
        uint32_t sectorsToRead = (1024 + geo.bytesPerSector - 1) / geo.bytesPerSector;

        std::vector<uint8_t> sectorBuffer(sectorsToRead * geo.bytesPerSector);
        if (!m_hardware->ReadSectors(startSector, sectorsToRead, sectorBuffer.data())) {
            return false;
        }

        std::memcpy(&m_superblock, &sectorBuffer[offsetInSector], sizeof(Ext3SuperBlock));

        // Ext3 Verification using Magic Number (0xEF53)
        if (m_superblock.s_magic != 0xEF53) {
            return false;
        }

        m_block_size = 1024 << m_superblock.log_block_size;
        m_blocksPerGroup = m_superblock.s_block_per_group;
        m_inodesPerGroup = m_superblock.s_inodes_per_group;

        if (m_superblock.s_rev_level == 0) {
            m_inode_size = 128;
        } else {
            m_inode_size = m_superblock.inode_size;
        }

        return true;
    }

    bool Ext3Driver::GetGroupDescriptor(uint32_t groupNumber, Ext3GroupDescriptor& outDescriptor) const {
        if (m_block_size == 0) return false;

        uint32_t superblockBlock = (m_block_size == 1024) ? 1 : 0;
        uint32_t descriptorTableBlock = superblockBlock + 1;
        uint64_t tableBaseByteOffset = static_cast<uint64_t>(descriptorTableBlock) * m_block_size;
        uint64_t exactDescriptorOffset = tableBaseByteOffset + (static_cast<uint64_t>(groupNumber) * sizeof(Ext3GroupDescriptor));
        uint32_t bytesPerSector = m_hardware->GetGeometry().bytesPerSector;

        if (bytesPerSector == 0) return false;

        uint64_t targetSector = exactDescriptorOffset / bytesPerSector;
        uint32_t offsetInsideSector = exactDescriptorOffset % bytesPerSector;

        std::vector<uint8_t> sectorBuffer(bytesPerSector);
        if (!m_hardware->ReadSectors(targetSector, 1, sectorBuffer.data())) {
            return false;
        }

        std::memcpy(&outDescriptor, &sectorBuffer[offsetInsideSector], sizeof(Ext3GroupDescriptor));
        return true;
    }

    bool Ext3Driver::ReadInode(uint32_t inodeNum, Ext3Inode& outInode) const {
        if (inodeNum == 0 || inodeNum > m_superblock.inode_count || m_inodesPerGroup == 0) {
            return false;
        }

        uint32_t blockGroup = (inodeNum - 1) / m_inodesPerGroup;
        uint32_t localInodeIndex = (inodeNum - 1) % m_inodesPerGroup;

        Ext3GroupDescriptor bgd;
        if (!GetGroupDescriptor(blockGroup, bgd)) {
            return false;
        }

        uint64_t tableBaseByteOffset = static_cast<uint64_t>(bgd.bg_inode_table) * m_block_size;
        uint64_t exactInodeByteOffset = tableBaseByteOffset + (static_cast<uint64_t>(localInodeIndex) * m_inode_size);
        uint32_t bytesPerSector = m_hardware->GetGeometry().bytesPerSector;
        if (bytesPerSector == 0) return false;

        uint64_t targetSector = exactInodeByteOffset / bytesPerSector;
        uint32_t offsetInsideSector = exactInodeByteOffset % bytesPerSector;

        std::vector<uint8_t> sectorBuffer(bytesPerSector);
        if (!m_hardware->ReadSectors(targetSector, 1, sectorBuffer.data())) {
            return false;
        }

        std::memcpy(&outInode, &sectorBuffer[offsetInsideSector], sizeof(Ext3Inode));
        return true;
    }

    bool Ext3Driver::WriteInode(uint32_t inodeNum, const Ext3Inode& inode) {
        if (inodeNum == 0 || inodeNum > m_superblock.inode_count || m_inodesPerGroup == 0) {
            return false;
        }

        uint32_t blockGroup = (inodeNum - 1) / m_inodesPerGroup;
        uint32_t localInodeIndex = (inodeNum - 1) % m_inodesPerGroup;

        Ext3GroupDescriptor bgd;
        if (!GetGroupDescriptor(blockGroup, bgd)) {
            return false;
        }

        uint64_t tableBaseByteOffset = static_cast<uint64_t>(bgd.bg_inode_table) * m_block_size;
        uint64_t exactInodeByteOffset = tableBaseByteOffset + (static_cast<uint64_t>(localInodeIndex) * m_inode_size);
        uint32_t bytesPerSector = m_hardware->GetGeometry().bytesPerSector;
        if (bytesPerSector == 0) return false;

        uint64_t targetSector = exactInodeByteOffset / bytesPerSector;
        uint32_t offsetInsideSector = exactInodeByteOffset % bytesPerSector;

        std::vector<uint8_t> sectorBuffer(bytesPerSector);
        if (!m_hardware->ReadSectors(targetSector, 1, sectorBuffer.data())) {
            return false;
        }

        std::memcpy(&sectorBuffer[offsetInsideSector], &inode, sizeof(Ext3Inode));
        return m_hardware->WriteSectors(targetSector, 1, sectorBuffer.data());
    }

    bool Ext3Driver::ClearBlockBitMapBit(uint32_t blockNum) {
        if (m_blocksPerGroup == 0) return false;

        uint32_t firstDataBlock = (m_block_size == 1024) ? 1 : 0;
        if (blockNum < firstDataBlock) return false;

        uint32_t relBlock = blockNum - firstDataBlock;
        uint32_t blockGroup = relBlock / m_blocksPerGroup;
        uint32_t localBitIndex = relBlock % m_blocksPerGroup;

        Ext3GroupDescriptor bgd;
        if (!GetGroupDescriptor(blockGroup, bgd)) {
            return false;
        }

        uint32_t bitmapBlock = bgd.bg_block_bitmap;
        uint32_t bytesPerSector = m_hardware->GetGeometry().bytesPerSector;
        if (bytesPerSector == 0) return false;

        uint64_t startSector = BlockToSector(bitmapBlock);
        uint32_t sectorsToRead = (m_block_size + bytesPerSector - 1) / bytesPerSector;

        std::vector<uint8_t> blockBuffer(sectorsToRead * bytesPerSector, 0);
        if (!m_hardware->ReadSectors(startSector, sectorsToRead, blockBuffer.data())) {
            return false;
        }

        uint32_t byteOffset = localBitIndex / 8;
        uint8_t bitMask = ~(1 << (localBitIndex % 8));
        blockBuffer[byteOffset] &= bitMask;

        std::cout << "[Block Bitmap] Cleared bit for Block " << blockNum
                  << " (Group " << blockGroup
                  << ", Sector " << startSector
                  << ", Byte " << byteOffset << ")\n";

        return m_hardware->WriteSectors(startSector, sectorsToRead, blockBuffer.data());
    }

    bool Ext3Driver::ClearInodeBitMapBit(uint32_t inodeNum) {
        if (inodeNum == 0 || inodeNum > m_superblock.inode_count || m_inodesPerGroup == 0) {
            return false;
        }

        uint32_t blockGroup = (inodeNum - 1) / m_inodesPerGroup;
        uint32_t localBitIndex = (inodeNum - 1) % m_inodesPerGroup;

        Ext3GroupDescriptor bgd;
        if (!GetGroupDescriptor(blockGroup, bgd)) {
            return false;
        }

        uint32_t bitmapBlock = bgd.bg_inode_bitmap;
        uint32_t bytesPerSector = m_hardware->GetGeometry().bytesPerSector;
        if (bytesPerSector == 0) return false;

        uint64_t startSector = BlockToSector(bitmapBlock);
        uint32_t sectorsToRead = (m_block_size + bytesPerSector - 1) / bytesPerSector;

        std::vector<uint8_t> blockBuffer(sectorsToRead * bytesPerSector, 0);
        if (!m_hardware->ReadSectors(startSector, sectorsToRead, blockBuffer.data())) {
            return false;
        }

        uint32_t byteOffset = localBitIndex / 8;
        uint8_t bitMask = ~(1 << (localBitIndex % 8));
        blockBuffer[byteOffset] &= bitMask;

        std::cout << "[Inode Bitmap] Cleared bit for Inode " << inodeNum
                  << " (Group " << blockGroup
                  << ", Sector " << startSector
                  << ", Byte " << byteOffset << ")\n";

        return m_hardware->WriteSectors(startSector, sectorsToRead, blockBuffer.data());
    }

    std::vector<std::string> Ext3Driver::TokenizePath(const std::string& path) const {
        std::vector<std::string> tokens;
        std::string normalizedPath = path;

        for (auto& c : normalizedPath) {
            if (c == '\\') c = '/';
        }

        std::stringstream ss(normalizedPath);
        std::string token;

        while (std::getline(ss, token, '/')) {
            if (!token.empty()) {
                tokens.push_back(token);
            }
        }

        return tokens;
    }

    std::vector<uint32_t> Ext3Driver::ReadBlockAsPointers(uint32_t blockNum) const {
        std::vector<uint32_t> pointers;
        if (blockNum == 0) return pointers;

        uint32_t bytesPerSector = m_hardware->GetGeometry().bytesPerSector;
        if (bytesPerSector == 0) return pointers;

        uint64_t startSector = BlockToSector(blockNum);
        uint32_t sectorsToRead = (m_block_size + bytesPerSector - 1) / bytesPerSector;

        std::vector<uint8_t> blockBuffer(sectorsToRead * bytesPerSector, 0);
        if (!m_hardware->ReadSectors(startSector, sectorsToRead, blockBuffer.data())) {
            return pointers;
        }

        uint32_t pointerCount = m_block_size / sizeof(uint32_t);
        pointers.resize(pointerCount);
        std::memcpy(pointers.data(), blockBuffer.data(), m_block_size);

        return pointers;
    }

    std::vector<uint32_t> Ext3Driver::GetInodeBlocks(const Ext3Inode& inode) const {
        std::vector<uint32_t> blocks;
        if (m_block_size == 0) return blocks;

        uint32_t requiredBlocks = (inode.i_size + m_block_size - 1) / m_block_size;
        uint32_t blocksFound = 0;

        // 1. Direct Blocks (0-11)
        for (int i = 0; i < 12 && blocksFound < requiredBlocks; ++i) {
            if (inode.i_block[i] != 0) {
                blocks.push_back(inode.i_block[i]);
                blocksFound++;
            }
        }

        // 2. Singly-Indirect Block (12)
        if (blocksFound < requiredBlocks && inode.i_block[12] != 0) {
            std::vector<uint32_t> indirectPointers = ReadBlockAsPointers(inode.i_block[12]);
            for (uint32_t ptr : indirectPointers) {
                if (blocksFound >= requiredBlocks) break;
                if (ptr != 0) {
                    blocks.push_back(ptr);
                    blocksFound++;
                }
            }
        }

        // 3. Doubly-Indirect Block (13)
        if (blocksFound < requiredBlocks && inode.i_block[13] != 0) {
            std::vector<uint32_t> doubleIndirectPointers = ReadBlockAsPointers(inode.i_block[13]);
            for (uint32_t singlePtrBlock : doubleIndirectPointers) {
                if (blocksFound >= requiredBlocks) break;
                if (singlePtrBlock == 0) continue;

                std::vector<uint32_t> indirectPointers = ReadBlockAsPointers(singlePtrBlock);
                for (uint32_t dataPtr : indirectPointers) {
                    if (blocksFound >= requiredBlocks) break;
                    if (dataPtr != 0) {
                        blocks.push_back(dataPtr);
                        blocksFound++;
                    }
                }
            }
        }

        // 4. Triply-Indirect Block (14)
        if (blocksFound < requiredBlocks && inode.i_block[14] != 0) {
            std::vector<uint32_t> tripleIndirectPointers = ReadBlockAsPointers(inode.i_block[14]);
            for (uint32_t doublePtrBlock : tripleIndirectPointers) {
                if (blocksFound >= requiredBlocks) break;
                if (doublePtrBlock == 0) continue;

                std::vector<uint32_t> doubleIndirectPointers = ReadBlockAsPointers(doublePtrBlock);
                for (uint32_t singlePtrBlock : doubleIndirectPointers) {
                    if (blocksFound >= requiredBlocks) break;
                    if (singlePtrBlock == 0) continue;

                    std::vector<uint32_t> indirectPointers = ReadBlockAsPointers(singlePtrBlock);
                    for (uint32_t dataPtr : indirectPointers) {
                        if (blocksFound >= requiredBlocks) break;
                        if (dataPtr != 0) {
                            blocks.push_back(dataPtr);
                            blocksFound++;
                        }
                    }
                }
            }
        }

        return blocks;
    }

    std::vector<uint32_t> Ext3Driver::GetInodeBlocks(uint32_t inodeNum) const {
        Ext3Inode inode;
        if (!ReadInode(inodeNum, inode)) {
            return {};
        }
        return GetInodeBlocks(inode);
    }

    std::vector<uint32_t> Ext3Driver::GetAllInodeBlocksIncludingIndirect(const Ext3Inode& inode) const {
        std::vector<uint32_t> allBlocks = GetInodeBlocks(inode);

        if (inode.i_block[12] != 0) {
            allBlocks.push_back(inode.i_block[12]);
        }
        if (inode.i_block[13] != 0) {
            allBlocks.push_back(inode.i_block[13]);
            std::vector<uint32_t> singleIndirects = ReadBlockAsPointers(inode.i_block[13]);
            for (uint32_t sib : singleIndirects) {
                if (sib != 0) allBlocks.push_back(sib);
            }
        }
        if (inode.i_block[14] != 0) {
            allBlocks.push_back(inode.i_block[14]);
            std::vector<uint32_t> doubleIndirects = ReadBlockAsPointers(inode.i_block[14]);
            for (uint32_t dib : doubleIndirects) {
                if (dib != 0) {
                    allBlocks.push_back(dib);
                    std::vector<uint32_t> singleIndirects = ReadBlockAsPointers(dib);
                    for (uint32_t sib : singleIndirects) {
                        if (sib != 0) allBlocks.push_back(sib);
                    }
                }
            }
        }

        return allBlocks;
    }

    Ext3Driver::SearchResult Ext3Driver::FindEntryInDirectory(const std::vector<uint32_t>& dirBlocks, const std::string& targetName) const {
        SearchResult result = { false, false, 0, 0, 0, 0, 0 };
        if (dirBlocks.empty()) return result;

        uint32_t bytesPerSector = m_hardware->GetGeometry().bytesPerSector;
        if (bytesPerSector == 0) return result;

        uint32_t sectorsPerBlock = (m_block_size + bytesPerSector - 1) / bytesPerSector;
        std::vector<uint8_t> blockBuffer(sectorsPerBlock * bytesPerSector);

        for (uint32_t block : dirBlocks) {
            if (block == 0) continue;

            uint64_t sector = BlockToSector(block);
            if (!m_hardware->ReadSectors(sector, sectorsPerBlock, blockBuffer.data())) {
                continue;
            }

            size_t offset = 0;
            uint16_t prevRecLen = 0;
            size_t prevOffset = 0;

            while (offset < m_block_size) {
                if (offset + sizeof(Ext3DirEntry) > m_block_size) break;

                Ext3DirEntry* entry = reinterpret_cast<Ext3DirEntry*>(&blockBuffer[offset]);

                if (entry->rec_len < 8 || offset + entry->rec_len > m_block_size) break;

                if (entry->inode != 0) {
                    char* namePtr = reinterpret_cast<char*>(&blockBuffer[offset + 8]);
                    std::string currentFileName(namePtr, entry->name_len);

                    if (currentFileName == targetName) {
                        result.found = true;
                        result.isDirectory = (entry->file_type == 2);
                        result.targetInode = entry->inode;
                        result.dirBlockNumber = block;
                        result.entryOffset = offset;
                        result.previousRecLen = prevRecLen;
                        result.previousEntryOffset = prevOffset;
                        return result;
                    }
                }

                prevRecLen = entry->rec_len;
                prevOffset = offset;
                offset += entry->rec_len;
            }
        }

        return result;
    }

    bool Ext3Driver::EraseFile(const std::string& relativePath) {
        if (m_block_size == 0) return false;

        std::vector<std::string> pathTokens = TokenizePath(relativePath);
        if (pathTokens.empty()) {
            std::cout << "[Ext3] ERROR: Invalid path provided.\n";
            return false;
        }

        std::cout << "\n--- Initiating Ext3 Secure Erase for: " << relativePath << " ---\n";

        uint32_t currentInodeNum = EXT3_ROOT_INO;
        SearchResult searchRes = { false, false, 0, 0, 0, 0, 0 };

        for (size_t i = 0; i < pathTokens.size(); ++i) {
            const std::string& token = pathTokens[i];
            bool isLastToken = (i == pathTokens.size() - 1);

            std::cout << "[Ext3 Parser] Searching for '" << token << "' in directory inode " << currentInodeNum << "...\n";

            std::vector<uint32_t> dirBlocks = GetInodeBlocks(currentInodeNum);
            searchRes = FindEntryInDirectory(dirBlocks, token);

            if (!searchRes.found) {
                std::cout << "[Ext3 Parser] ERROR: '" << token << "' not found!\n";
                return false;
            }

            if (!isLastToken) {
                if (!searchRes.isDirectory) {
                    std::cout << "[Ext3 Parser] ERROR: '" << token << "' is a file, not a directory!\n";
                    return false;
                }
                currentInodeNum = searchRes.targetInode;
            }
        }

        uint32_t targetInode = searchRes.targetInode;
        std::cout << "[Ext3] Target file found with Inode: " << targetInode << "\n";

        Ext3Inode fileInode;
        if (!ReadInode(targetInode, fileInode)) {
            std::cout << "[Ext3] ERROR: Failed to read target inode.\n";
            return false;
        }

        // 1. Collect all data blocks + indirect pointer blocks
        std::vector<uint32_t> blocksToWipe = GetAllInodeBlocksIncludingIndirect(fileInode);
        std::cout << "[Ext3] Securely wiping " << blocksToWipe.size() << " data/indirect blocks...\n";

        uint32_t bytesPerSector = m_hardware->GetGeometry().bytesPerSector;
        if (bytesPerSector == 0) return false;
        uint32_t sectorsPerBlock = (m_block_size + bytesPerSector - 1) / bytesPerSector;

        for (uint32_t blockNum : blocksToWipe) {
            if (blockNum == 0) continue;
            uint64_t sector = BlockToSector(blockNum);
            m_hardware->SecureEraseSectors(sector, sectorsPerBlock);
            ClearBlockBitMapBit(blockNum);
        }

        // 2. Clear Inode structure on disk and Inode Bitmap
        std::cout << "[Ext3] Zeroing out Inode metadata and updating Inode Bitmap...\n";
        Ext3Inode zeroInode;
        std::memset(&zeroInode, 0, sizeof(zeroInode));
        WriteInode(targetInode, zeroInode);
        ClearInodeBitMapBit(targetInode);

        // 3. Unlink Directory Entry from parent directory block
        std::cout << "[Ext3] Unlinking directory entry from parent block " << searchRes.dirBlockNumber << "...\n";
        uint64_t dirSector = BlockToSector(searchRes.dirBlockNumber);
        std::vector<uint8_t> dirBlockBuffer(sectorsPerBlock * bytesPerSector);
        if (m_hardware->ReadSectors(dirSector, sectorsPerBlock, dirBlockBuffer.data())) {
            Ext3DirEntry* targetEntry = reinterpret_cast<Ext3DirEntry*>(&dirBlockBuffer[searchRes.entryOffset]);
            if (searchRes.previousRecLen > 0) {
                Ext3DirEntry* prevEntry = reinterpret_cast<Ext3DirEntry*>(&dirBlockBuffer[searchRes.previousEntryOffset]);
                uint16_t targetRecLen = targetEntry->rec_len;
                prevEntry->rec_len += targetRecLen;
                std::memset(targetEntry, 0, targetRecLen);
            } else {
                targetEntry->inode = 0;
            }
            m_hardware->WriteSectors(dirSector, sectorsPerBlock, dirBlockBuffer.data());
        }

        std::cout << "=== Ext3 File Securely Wiped ===\n";
        return true;
    }

    bool Ext3Driver::WipeVolume() {
        if (m_block_size == 0) return false;

        std::cout << "\n=== Initiating Full Ext3 Volume Wipe ===\n";

        uint32_t totalGroups = (m_superblock.blocks_count + m_blocksPerGroup - 1) / m_blocksPerGroup;
        uint32_t bytesPerSector = m_hardware->GetGeometry().bytesPerSector;
        if (bytesPerSector == 0) return false;
        uint32_t sectorsPerBlock = (m_block_size + bytesPerSector - 1) / bytesPerSector;

        std::vector<uint8_t> zeroBlock(sectorsPerBlock * bytesPerSector, 0);

        for (uint32_t group = 0; group < totalGroups; ++group) {
            Ext3GroupDescriptor bgd;
            if (!GetGroupDescriptor(group, bgd)) continue;

            std::cout << "  -> Wiping Block Group " << group << "...\n";

            // 1. Wipe data blocks in group
            uint32_t firstDataBlock = (m_block_size == 1024) ? 1 : 0;
            uint32_t groupStartBlock = group * m_blocksPerGroup + firstDataBlock;
            uint32_t groupEndBlock = std::min((group + 1) * m_blocksPerGroup + firstDataBlock - 1, m_superblock.blocks_count - 1);

            for (uint32_t b = groupStartBlock; b <= groupEndBlock; ++b) {
                // Skip superblock, group descriptors, and metadata blocks
                if (b == bgd.bg_block_bitmap || b == bgd.bg_inode_bitmap ||
                    (b >= bgd.bg_inode_table && b < bgd.bg_inode_table + (m_inodesPerGroup * m_inode_size / m_block_size))) {
                    continue;
                }
                m_hardware->SecureEraseSectors(BlockToSector(b), sectorsPerBlock);
            }

            // 2. Clear Inode table
            uint32_t inodeTableBlocks = (m_inodesPerGroup * m_inode_size + m_block_size - 1) / m_block_size;
            for (uint32_t ib = 0; ib < inodeTableBlocks; ++ib) {
                m_hardware->WriteSectors(BlockToSector(bgd.bg_inode_table + ib), sectorsPerBlock, zeroBlock.data());
            }

            // 3. Clear Bitmaps
            m_hardware->WriteSectors(BlockToSector(bgd.bg_block_bitmap), sectorsPerBlock, zeroBlock.data());
            m_hardware->WriteSectors(BlockToSector(bgd.bg_inode_bitmap), sectorsPerBlock, zeroBlock.data());
        }

        std::cout << "=== Ext3 Volume Securely Wiped! ===\n";
        return true;
    }

    void Ext3Driver::PrintSuperBlockInfo() const {
        if (m_superblock.s_magic != 0xEF53) {
            std::cout << "Drive is not mounted or not a valid Ext3 filesystem!\n";
            return;
        }

        std::cout << "\n================ Ext3 SuperBlock Info ================\n";
        std::cout << "  Magic Signature   : 0x" << std::hex << m_superblock.s_magic << std::dec << " (Valid Ext2/Ext3)\n";
        std::cout << "  Block Size        : " << m_block_size << " bytes\n";
        std::cout << "  Inode Size        : " << m_inode_size << " bytes\n";
        std::cout << "  Total Inodes      : " << m_superblock.inode_count << "\n";
        std::cout << "  Total Blocks      : " << m_superblock.blocks_count << "\n";
        std::cout << "  Blocks Per Group  : " << m_blocksPerGroup << "\n";
        std::cout << "  Inodes Per Group  : " << m_inodesPerGroup << "\n";
        std::cout << "  Revision Level    : " << m_superblock.s_rev_level << "\n";
        std::cout << "======================================================\n\n";
    }

} // namespace FileSystems
} // namespace Erasure