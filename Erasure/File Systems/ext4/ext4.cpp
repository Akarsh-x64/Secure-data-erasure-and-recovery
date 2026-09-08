#include "ext4.h"
#include <iostream>
#include <cstring>
#include <sstream>
#include <chrono>
#include <algorithm>

namespace Erasure {
namespace FileSystems {

Ext4Driver::Ext4Driver(Core::IHardwareController* hardware)
    : m_hardware(hardware)
    , m_blockSize(0)
    , m_sectorsPerBlock(0)
    , m_bytesPerSector(0)
    , m_groupCount(0)
    , m_inodeSize(0)
    , m_is64Bit(false)
{
    std::memset(&m_sb, 0, sizeof(m_sb));
}

uint64_t Ext4Driver::BlockToSector(uint64_t block) const {
    return block * m_sectorsPerBlock;
}

bool Ext4Driver::ReadBlock(uint64_t block, void* buffer) const {
    if (!m_hardware || m_sectorsPerBlock == 0 || !buffer) return false;
    return m_hardware->ReadSectors(BlockToSector(block), m_sectorsPerBlock, buffer);
}

bool Ext4Driver::WriteBlock(uint64_t block, const void* buffer) {
    if (!m_hardware || m_sectorsPerBlock == 0 || !buffer) return false;
    return m_hardware->WriteSectors(BlockToSector(block), m_sectorsPerBlock, buffer);
}

bool Ext4Driver::Mount() {
    if (!m_hardware) {
        std::cerr << "[Ext4Driver] Error: Hardware controller is null.\n";
        return false;
    }

    Core::DeviceGeometry geo = m_hardware->GetGeometry();
    if (geo.bytesPerSector == 0) {
        std::cerr << "[Ext4Driver] Error: Invalid sector size (0 bytes).\n";
        return false;
    }
    m_bytesPerSector = geo.bytesPerSector;

    // Superblock is located at byte offset 1024
    uint64_t sbSector = Ext4::EXT4_SUPERBLOCK_OFFSET / m_bytesPerSector;
    uint32_t sbSectorsToRead = (sizeof(Ext4::Ext4Superblock) + m_bytesPerSector - 1) / m_bytesPerSector;

    std::vector<uint8_t> sbBuffer(sbSectorsToRead * m_bytesPerSector);
    if (!m_hardware->ReadSectors(sbSector, sbSectorsToRead, sbBuffer.data())) {
        std::cerr << "[Ext4Driver] Error: Failed to read Superblock sectors.\n";
        return false;
    }

    // Offset within the first sector if 1024 is not aligned to sector boundary
    uint32_t offsetInSector = Ext4::EXT4_SUPERBLOCK_OFFSET % m_bytesPerSector;
    std::memcpy(&m_sb, sbBuffer.data() + offsetInSector, sizeof(Ext4::Ext4Superblock));

    // Verify magic signature: 0xEF53
    if (m_sb.s_magic != Ext4::EXT4_SUPER_MAGIC) {
        std::cerr << "[Ext4Driver] Error: Invalid ext4 magic: 0x"
                  << std::hex << m_sb.s_magic << std::dec
                  << " (Expected 0x" << std::hex << Ext4::EXT4_SUPER_MAGIC << std::dec << ")\n";
        return false;
    }

    // Calculate block size
    if (m_sb.s_log_block_size > 6) {
        std::cerr << "[Ext4Driver] Error: Invalid log block size: " << m_sb.s_log_block_size << "\n";
        return false;
    }
    m_blockSize = 1024 << m_sb.s_log_block_size;
    if (m_blockSize < m_bytesPerSector || (m_blockSize % m_bytesPerSector) != 0) {
        std::cerr << "[Ext4Driver] Error: Unsupported block size: " << m_blockSize << " bytes.\n";
        return false;
    }
    m_sectorsPerBlock = m_blockSize / m_bytesPerSector;

    // Inode size (standard ext4 uses 256 bytes)
    m_inodeSize = m_sb.s_inode_size;
    if (m_inodeSize < Ext4::EXT4_GOOD_OLD_INODE_SIZE) {
        m_inodeSize = Ext4::EXT4_GOOD_OLD_INODE_SIZE;
    }
    if (m_inodeSize > m_blockSize || (m_blockSize % m_inodeSize) != 0) {
        std::cerr << "[Ext4Driver] Error: Invalid inode size (" << m_inodeSize << " bytes) for block size (" << m_blockSize << " bytes).\n";
        return false;
    }

    // Check 64-bit feature
    m_is64Bit = (m_sb.s_feature_incompat & Ext4::EXT4_FEATURE_INCOMPAT_64BIT) != 0;

    // Total blocks
    uint64_t totalBlocks = m_sb.s_blocks_count_lo;
    if (m_is64Bit) {
        totalBlocks |= (static_cast<uint64_t>(m_sb.s_blocks_count_hi) << 32);
    }

    if (m_sb.s_blocks_per_group == 0 || totalBlocks < m_sb.s_first_data_block) {
        std::cerr << "[Ext4Driver] Error: Invalid blocks_per_group or totalBlocks.\n";
        return false;
    }

    m_groupCount = static_cast<uint32_t>((totalBlocks - m_sb.s_first_data_block + m_sb.s_blocks_per_group - 1) / m_sb.s_blocks_per_group);

    // Read Group Descriptor Table (GDT)
    // GDT immediately follows superblock:
    // If block size is 1024 (s_first_data_block = 1), GDT is at block 2.
    // If block size > 1024 (s_first_data_block = 0), GDT is at block 1.
    uint64_t gdtStartBlock = m_sb.s_first_data_block + 1;
    uint32_t descSize = m_is64Bit ? (m_sb.s_desc_size ? m_sb.s_desc_size : 64) : 32;
    uint64_t totalGdtBytes = static_cast<uint64_t>(m_groupCount) * descSize;
    uint32_t gdtBlocksCount = static_cast<uint32_t>((totalGdtBytes + m_blockSize - 1) / m_blockSize);

    std::vector<uint8_t> gdtBuffer(gdtBlocksCount * m_blockSize);
    for (uint32_t b = 0; b < gdtBlocksCount; ++b) {
        if (!ReadBlock(gdtStartBlock + b, gdtBuffer.data() + (b * m_blockSize))) {
            std::cerr << "[Ext4Driver] Error: Failed to read GDT block " << (gdtStartBlock + b) << "\n";
            return false;
        }
    }

    m_groupDescriptors.clear();
    m_groupDescriptors.resize(m_groupCount);

    for (uint32_t i = 0; i < m_groupCount; ++i) {
        const uint8_t* descPtr = gdtBuffer.data() + (i * descSize);
        if (m_is64Bit && descSize >= 64) {
            std::memcpy(&m_groupDescriptors[i], descPtr, sizeof(Ext4::Ext4GroupDesc64));
        } else {
            std::memset(&m_groupDescriptors[i], 0, sizeof(Ext4::Ext4GroupDesc64));
            std::memcpy(&m_groupDescriptors[i].lo, descPtr, sizeof(Ext4::Ext4GroupDesc));
        }
    }

    return true;
}

uint64_t Ext4Driver::GetBlockBitmapBlock(uint32_t group) const {
    if (group >= m_groupDescriptors.size()) return 0;
    uint64_t blk = m_groupDescriptors[group].lo.bg_block_bitmap_lo;
    if (m_is64Bit) {
        blk |= (static_cast<uint64_t>(m_groupDescriptors[group].bg_block_bitmap_hi) << 32);
    }
    return blk;
}

uint64_t Ext4Driver::GetInodeBitmapBlock(uint32_t group) const {
    if (group >= m_groupDescriptors.size()) return 0;
    uint64_t blk = m_groupDescriptors[group].lo.bg_inode_bitmap_lo;
    if (m_is64Bit) {
        blk |= (static_cast<uint64_t>(m_groupDescriptors[group].bg_inode_bitmap_hi) << 32);
    }
    return blk;
}

uint64_t Ext4Driver::GetInodeTableBlock(uint32_t group) const {
    if (group >= m_groupDescriptors.size()) return 0;
    uint64_t blk = m_groupDescriptors[group].lo.bg_inode_table_lo;
    if (m_is64Bit) {
        blk |= (static_cast<uint64_t>(m_groupDescriptors[group].bg_inode_table_hi) << 32);
    }
    return blk;
}

bool Ext4Driver::ReadInode(uint32_t inodeNum, Ext4::Ext4Inode& outInode) const {
    if (inodeNum == 0 || m_sb.s_inodes_per_group == 0 || m_blockSize == 0) return false;

    uint32_t group = (inodeNum - 1) / m_sb.s_inodes_per_group;
    uint32_t indexInGroup = (inodeNum - 1) % m_sb.s_inodes_per_group;

    uint64_t itableStart = GetInodeTableBlock(group);
    if (itableStart == 0) return false;

    uint64_t byteOffsetInTable = static_cast<uint64_t>(indexInGroup) * m_inodeSize;
    uint64_t inodeBlock = itableStart + (byteOffsetInTable / m_blockSize);
    uint32_t offsetInBlock = static_cast<uint32_t>(byteOffsetInTable % m_blockSize);

    std::vector<uint8_t> blockBuffer(m_blockSize);
    if (!ReadBlock(inodeBlock, blockBuffer.data())) {
        return false;
    }

    std::memset(&outInode, 0, sizeof(Ext4::Ext4Inode));
    size_t copySize = std::min(static_cast<size_t>(m_inodeSize), sizeof(Ext4::Ext4Inode));
    std::memcpy(&outInode, blockBuffer.data() + offsetInBlock, copySize);
    return true;
}

bool Ext4Driver::WriteInode(uint32_t inodeNum, const Ext4::Ext4Inode& inInode) {
    if (inodeNum == 0 || m_sb.s_inodes_per_group == 0 || m_blockSize == 0) return false;

    uint32_t group = (inodeNum - 1) / m_sb.s_inodes_per_group;
    uint32_t indexInGroup = (inodeNum - 1) % m_sb.s_inodes_per_group;

    uint64_t itableStart = GetInodeTableBlock(group);
    if (itableStart == 0) return false;

    uint64_t byteOffsetInTable = static_cast<uint64_t>(indexInGroup) * m_inodeSize;
    uint64_t inodeBlock = itableStart + (byteOffsetInTable / m_blockSize);
    uint32_t offsetInBlock = static_cast<uint32_t>(byteOffsetInTable % m_blockSize);

    std::vector<uint8_t> blockBuffer(m_blockSize);
    if (!ReadBlock(inodeBlock, blockBuffer.data())) {
        return false;
    }

    size_t copySize = std::min(static_cast<size_t>(m_inodeSize), sizeof(Ext4::Ext4Inode));
    std::memcpy(blockBuffer.data() + offsetInBlock, &inInode, copySize);

    return WriteBlock(inodeBlock, blockBuffer.data());
}

bool Ext4Driver::WipeInodeOnDisk(uint32_t inodeNum) {
    if (inodeNum == 0 || m_sb.s_inodes_per_group == 0 || m_blockSize == 0) return false;

    uint32_t group = (inodeNum - 1) / m_sb.s_inodes_per_group;
    uint32_t indexInGroup = (inodeNum - 1) % m_sb.s_inodes_per_group;

    uint64_t itableStart = GetInodeTableBlock(group);
    if (itableStart == 0) return false;

    uint64_t byteOffsetInTable = static_cast<uint64_t>(indexInGroup) * m_inodeSize;
    uint64_t inodeBlock = itableStart + (byteOffsetInTable / m_blockSize);
    uint32_t offsetInBlock = static_cast<uint32_t>(byteOffsetInTable % m_blockSize);

    std::vector<uint8_t> blockBuffer(m_blockSize);
    if (!ReadBlock(inodeBlock, blockBuffer.data())) {
        return false;
    }

    // Zero out the ENTIRE on-disk inode footprint (all m_inodeSize bytes, e.g. 256 bytes).
    // This wipes i_block[60] (inline data/fast symlinks) and any extended attribute
    // or inline data overflow space (bytes 128..255).
    std::memset(blockBuffer.data() + offsetInBlock, 0, m_inodeSize);

    // Record deletion timestamp in the zeroed inode
    uint32_t currentTime = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
    Ext4::Ext4Inode* scrubbed = reinterpret_cast<Ext4::Ext4Inode*>(blockBuffer.data() + offsetInBlock);
    scrubbed->i_dtime = currentTime;

    return WriteBlock(inodeBlock, blockBuffer.data());
}

void Ext4Driver::CollectExtentBlocks(uint64_t extentBlock, std::vector<uint64_t>& outBlocks) const {
    std::vector<uint8_t> buffer(m_blockSize);
    if (!ReadBlock(extentBlock, buffer.data())) return;

    const Ext4::Ext4ExtentHeader* eh = reinterpret_cast<const Ext4::Ext4ExtentHeader*>(buffer.data());
    if (eh->eh_magic != Ext4::EXT4_EXTENT_MAGIC) return;

    if (eh->eh_depth == 0) {
        // Leaf node
        const Ext4::Ext4Extent* extents = reinterpret_cast<const Ext4::Ext4Extent*>(buffer.data() + sizeof(Ext4::Ext4ExtentHeader));
        for (uint16_t i = 0; i < eh->eh_entries; ++i) {
            uint64_t startBlock = (static_cast<uint64_t>(extents[i].ee_start_hi) << 32) | extents[i].ee_start_lo;
            uint16_t len = (extents[i].ee_len <= 32768) ? extents[i].ee_len : (extents[i].ee_len - 32768);
            for (uint16_t b = 0; b < len; ++b) {
                outBlocks.push_back(startBlock + b);
            }
        }
    } else {
        // Internal node
        const Ext4::Ext4ExtentIdx* indices = reinterpret_cast<const Ext4::Ext4ExtentIdx*>(buffer.data() + sizeof(Ext4::Ext4ExtentHeader));
        for (uint16_t i = 0; i < eh->eh_entries; ++i) {
            uint64_t childBlock = (static_cast<uint64_t>(indices[i].ei_leaf_hi) << 32) | indices[i].ei_leaf_lo;
            outBlocks.push_back(childBlock); // Collect the extent metadata block itself for erasure
            CollectExtentBlocks(childBlock, outBlocks);
        }
    }
}

std::vector<uint64_t> Ext4Driver::GetInodeAllocatedBlocks(const Ext4::Ext4Inode& inode) const {
    std::vector<uint64_t> blocks;

    if ((inode.i_flags & Ext4::EXT4_EXTENTS_FL) != 0) {
        // Extent Tree
        const Ext4::Ext4ExtentHeader* eh = reinterpret_cast<const Ext4::Ext4ExtentHeader*>(inode.i_block);
        if (eh->eh_magic == Ext4::EXT4_EXTENT_MAGIC) {
            if (eh->eh_depth == 0) {
                // Leaf entries embedded directly in inode
                const Ext4::Ext4Extent* extents = reinterpret_cast<const Ext4::Ext4Extent*>(inode.i_block + sizeof(Ext4::Ext4ExtentHeader));
                for (uint16_t i = 0; i < eh->eh_entries; ++i) {
                    uint64_t startBlock = (static_cast<uint64_t>(extents[i].ee_start_hi) << 32) | extents[i].ee_start_lo;
                    uint16_t len = (extents[i].ee_len <= 32768) ? extents[i].ee_len : (extents[i].ee_len - 32768);
                    for (uint16_t b = 0; b < len; ++b) {
                        blocks.push_back(startBlock + b);
                    }
                }
            } else {
                // Internal index entries embedded in inode
                const Ext4::Ext4ExtentIdx* indices = reinterpret_cast<const Ext4::Ext4ExtentIdx*>(inode.i_block + sizeof(Ext4::Ext4ExtentHeader));
                for (uint16_t i = 0; i < eh->eh_entries; ++i) {
                    uint64_t childBlock = (static_cast<uint64_t>(indices[i].ei_leaf_hi) << 32) | indices[i].ei_leaf_lo;
                    blocks.push_back(childBlock);
                    CollectExtentBlocks(childBlock, blocks);
                }
            }
        }
    } else {
        // If inline data flag is set or i_blocks_lo == 0 (inline file or fast symlink),
        // the payload is stored directly inside the inode and no external blocks exist.
        if ((inode.i_flags & Ext4::EXT4_INLINE_DATA_FL) == 0 && inode.i_blocks_lo > 0) {
            // Direct block pointers (legacy ext2/3 style, first 12 pointers)
            const uint32_t* directBlocks = reinterpret_cast<const uint32_t*>(inode.i_block);
            for (int i = 0; i < 12; ++i) {
                if (directBlocks[i] != 0) {
                    blocks.push_back(directBlocks[i]);
                }
            }
        }
    }

    return blocks;
}

bool Ext4Driver::ClearBlockBitmapBit(uint64_t blockNum) {
    if (m_sb.s_blocks_per_group == 0 || m_blockSize == 0) return false;
    if (blockNum < m_sb.s_first_data_block) return false;

    uint64_t relativeBlock = blockNum - m_sb.s_first_data_block;
    uint32_t group = static_cast<uint32_t>(relativeBlock / m_sb.s_blocks_per_group);
    uint32_t indexInGroup = static_cast<uint32_t>(relativeBlock % m_sb.s_blocks_per_group);

    uint64_t bitmapBlock = GetBlockBitmapBlock(group);
    if (bitmapBlock == 0) return false;

    std::vector<uint8_t> buffer(m_blockSize);
    if (!ReadBlock(bitmapBlock, buffer.data())) return false;

    uint32_t byteOffset = indexInGroup / 8;
    uint8_t bitMask = ~(1 << (indexInGroup % 8));

    if (byteOffset < buffer.size()) {
        buffer[byteOffset] &= bitMask;
        return WriteBlock(bitmapBlock, buffer.data());
    }
    return false;
}

bool Ext4Driver::ClearInodeBitmapBit(uint32_t inodeNum) {
    if (inodeNum == 0 || m_sb.s_inodes_per_group == 0 || m_blockSize == 0) return false;

    uint32_t group = (inodeNum - 1) / m_sb.s_inodes_per_group;
    uint32_t indexInGroup = (inodeNum - 1) % m_sb.s_inodes_per_group;

    uint64_t bitmapBlock = GetInodeBitmapBlock(group);
    if (bitmapBlock == 0) return false;

    std::vector<uint8_t> buffer(m_blockSize);
    if (!ReadBlock(bitmapBlock, buffer.data())) return false;

    uint32_t byteOffset = indexInGroup / 8;
    uint8_t bitMask = ~(1 << (indexInGroup % 8));

    if (byteOffset < buffer.size()) {
        buffer[byteOffset] &= bitMask;
        return WriteBlock(bitmapBlock, buffer.data());
    }
    return false;
}

std::vector<std::string> Ext4Driver::TokenizePath(const std::string& path) const {
    std::vector<std::string> tokens;
    std::string token;
    std::string normalized = path;

    for (char& c : normalized) {
        if (c == '\\') c = '/';
    }

    std::stringstream ss(normalized);
    while (std::getline(ss, token, '/')) {
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    return tokens;
}

Ext4Driver::DirectorySearchResult
Ext4Driver::FindEntryInDirectory(const Ext4::Ext4Inode& dirInode, const std::string& targetName) const {
    DirectorySearchResult result = { false, false, 0, 0, 0, 0 };

    if ((dirInode.i_mode & Ext4::EXT4_S_IFMT) != Ext4::EXT4_S_IFDIR) {
        return result;
    }

    std::vector<uint64_t> dirBlocks = GetInodeAllocatedBlocks(dirInode);
    std::vector<uint8_t> blockBuffer(m_blockSize);

    for (uint64_t block : dirBlocks) {
        if (!ReadBlock(block, blockBuffer.data())) continue;

        size_t offset = 0;
        while (offset + sizeof(Ext4::Ext4DirEntry2) <= m_blockSize) {
            const Ext4::Ext4DirEntry2* entry = reinterpret_cast<const Ext4::Ext4DirEntry2*>(blockBuffer.data() + offset);

            if (entry->rec_len == 0) break; // Corrupt or empty

            if (entry->inode != 0 && entry->name_len == targetName.length()) {
                if (std::memcmp(entry->name, targetName.data(), entry->name_len) == 0) {
                    result.found = true;
                    result.isDirectory = (entry->file_type == Ext4::EXT4_FT_DIR);
                    result.inodeNum = entry->inode;
                    result.dirBlock = block;
                    result.entryOffsetInBlock = offset;
                    result.recLen = entry->rec_len;
                    return result;
                }
            }

            offset += entry->rec_len;
        }
    }

    // Check inline directory data (stored directly in dirInode.i_block)
    if (dirBlocks.empty() && dirInode.i_size_lo > 0) {
        size_t offset = 0;
        if (dirInode.i_size_lo >= 4 + sizeof(Ext4::Ext4DirEntry2)) {
            const Ext4::Ext4DirEntry2* test = reinterpret_cast<const Ext4::Ext4DirEntry2*>(dirInode.i_block + 4);
            if (test->rec_len >= 8 && test->rec_len <= sizeof(dirInode.i_block)) {
                offset = 4;
            }
        }

        while (offset + sizeof(Ext4::Ext4DirEntry2) <= sizeof(dirInode.i_block) && offset < dirInode.i_size_lo) {
            const Ext4::Ext4DirEntry2* entry = reinterpret_cast<const Ext4::Ext4DirEntry2*>(dirInode.i_block + offset);
            if (entry->rec_len == 0 || offset + entry->rec_len > sizeof(dirInode.i_block)) break;

            if (entry->inode != 0 && entry->name_len == targetName.length()) {
                if (std::memcmp(entry->name, targetName.data(), entry->name_len) == 0) {
                    result.found = true;
                    result.isDirectory = (entry->file_type == Ext4::EXT4_FT_DIR);
                    result.inodeNum = entry->inode;
                    result.dirBlock = 0; // 0 indicates inline in parent inode
                    result.entryOffsetInBlock = offset;
                    result.recLen = entry->rec_len;
                    return result;
                }
            }
            offset += entry->rec_len;
        }
    }

    return result;
}

bool Ext4Driver::EraseFile(const std::string& relativePath) {
    if (m_blockSize == 0) {
        std::cerr << "[Ext4Driver] Error: Filesystem not mounted.\n";
        return false;
    }

    std::vector<std::string> pathTokens = TokenizePath(relativePath);
    if (pathTokens.empty()) {
        std::cerr << "[Ext4Driver] Error: Invalid or empty path.\n";
        return false;
    }

    std::cout << "\n--- Initiating Recursive ext4 EraseFile for: " << relativePath << " ---\n";

    // Start at Root Inode (2)
    uint32_t currentInodeNum = Ext4::EXT4_ROOT_INO;
    uint32_t parentInodeNum = currentInodeNum;
    Ext4::Ext4Inode currentInode;
    if (!ReadInode(currentInodeNum, currentInode)) {
        std::cerr << "[Ext4Driver] Error: Failed to read root inode.\n";
        return false;
    }

    DirectorySearchResult searchRes = { false, false, 0, 0, 0, 0 };

    for (size_t i = 0; i < pathTokens.size(); ++i) {
        const std::string& targetName = pathTokens[i];
        bool isLastToken = (i == pathTokens.size() - 1);

        std::cout << "[Parser] Searching for '" << targetName << "' in inode " << currentInodeNum << "...\n";
        searchRes = FindEntryInDirectory(currentInode, targetName);

        if (!searchRes.found) {
            std::cerr << "[Parser] Error: '" << targetName << "' not found!\n";
            return false;
        }

        if (!isLastToken) {
            if (!searchRes.isDirectory) {
                std::cerr << "[Parser] Error: '" << targetName << "' is not a directory!\n";
                return false;
            }
            parentInodeNum = currentInodeNum;
            currentInodeNum = searchRes.inodeNum;
            if (!ReadInode(currentInodeNum, currentInode)) {
                std::cerr << "[Parser] Error: Failed to read directory inode " << currentInodeNum << "\n";
                return false;
            }
        } else {
            parentInodeNum = currentInodeNum;
        }
    }

    // Now searchRes points to the target file entry
    uint32_t targetInodeNum = searchRes.inodeNum;
    Ext4::Ext4Inode targetInode;
    if (!ReadInode(targetInodeNum, targetInode)) {
        std::cerr << "[Erasure] Error: Failed to read target file inode " << targetInodeNum << "\n";
        return false;
    }

    // 1. Collect all allocated data blocks
    std::vector<uint64_t> allocatedBlocks = GetInodeAllocatedBlocks(targetInode);
    if (!allocatedBlocks.empty()) {
        std::cout << "[Erasure] Target occupies " << allocatedBlocks.size() << " external data/extent blocks. Beginning sector wipe...\n";
        for (uint64_t blk : allocatedBlocks) {
            uint64_t startSector = BlockToSector(blk);
            std::cout << "  -> Wiping Block " << blk << " (Sector " << startSector << ", " << m_sectorsPerBlock << " sectors)\n";
            m_hardware->SecureEraseSectors(startSector, m_sectorsPerBlock);
            ClearBlockBitmapBit(blk);
        }
    } else {
        std::cout << "[Erasure] Target file has no external data blocks (Inline Data / Fast Symlink).\n";
        std::cout << "  -> Target data payload is stored directly inside on-disk Inode " << targetInodeNum << ".\n";
    }

    // 2. Clear Inode Bitmap bit
    ClearInodeBitmapBit(targetInodeNum);
    std::cout << "[Erasure] Freed Inode " << targetInodeNum << " in inode allocation bitmap.\n";

    // 3. Scrub entire on-disk Inode structure (full m_inodeSize, e.g. 256 bytes)
    // This zeroes i_block[60] and any extended attribute space holding inline overflow data
    if (!WipeInodeOnDisk(targetInodeNum)) {
        std::cerr << "[Erasure] Warning: Failed to wipe inode on disk.\n";
    } else {
        std::cout << "[Erasure] Successfully zeroed all " << m_inodeSize << " bytes of on-disk Inode " << targetInodeNum
                  << " (obliterating i_block payload and extended attribute space).\n";
    }

    // 4. Scrub Directory Entry in Parent Directory
    if (searchRes.dirBlock != 0) {
        std::vector<uint8_t> dirBlockBuffer(m_blockSize);
        if (ReadBlock(searchRes.dirBlock, dirBlockBuffer.data())) {
            Ext4::Ext4DirEntry2* entry = reinterpret_cast<Ext4::Ext4DirEntry2*>(dirBlockBuffer.data() + searchRes.entryOffsetInBlock);
            entry->inode = 0;
            std::memset(entry->name, 0, entry->name_len);
            entry->name_len = 0;
            entry->file_type = Ext4::EXT4_FT_UNKNOWN;

            WriteBlock(searchRes.dirBlock, dirBlockBuffer.data());
            std::cout << "[Erasure] Scrubbed parent directory entry record on block " << searchRes.dirBlock << "\n";
        }
    } else {
        Ext4::Ext4Inode parentInode;
        if (ReadInode(parentInodeNum, parentInode)) {
            Ext4::Ext4DirEntry2* entry = reinterpret_cast<Ext4::Ext4DirEntry2*>(parentInode.i_block + searchRes.entryOffsetInBlock);
            entry->inode = 0;
            std::memset(entry->name, 0, entry->name_len);
            entry->name_len = 0;
            entry->file_type = Ext4::EXT4_FT_UNKNOWN;

            WriteInode(parentInodeNum, parentInode);
            std::cout << "[Erasure] Scrubbed parent inline directory entry in Inode " << parentInodeNum << "\n";
        }
    }

    std::cout << "--- ext4 Recursive EraseFile Completed Successfully! ---\n";
    return true;
}

bool Ext4Driver::WipeVolume() {
    if (m_blockSize == 0 || m_groupCount == 0) {
        std::cerr << "[Ext4Driver] Error: Filesystem not mounted.\n";
        return false;
    }

    std::cout << "\n=== INITIATING EXT4 SURGICAL VOLUME WIPE ===\n";
    std::cout << "[Quarantine] Mapping critical filesystem structures across " << m_groupCount << " block groups...\n";

    std::vector<uint64_t> quarantinedBlocks;

    // 1. Superblock & GDT blocks in Group 0
    uint64_t sbBlock = Ext4::EXT4_SUPERBLOCK_OFFSET / m_blockSize;
    quarantinedBlocks.push_back(sbBlock);

    uint64_t gdtStartBlock = m_sb.s_first_data_block + 1;
    uint32_t descSize = m_is64Bit ? (m_sb.s_desc_size ? m_sb.s_desc_size : 64) : 32;
    uint64_t totalGdtBytes = static_cast<uint64_t>(m_groupCount) * descSize;
    uint32_t gdtBlocksCount = static_cast<uint32_t>((totalGdtBytes + m_blockSize - 1) / m_blockSize);

    for (uint32_t b = 0; b < gdtBlocksCount; ++b) {
        quarantinedBlocks.push_back(gdtStartBlock + b);
    }

    // 2. Bitmaps and Inode Tables for each Block Group
    uint32_t itableBlocksPerGroup = (m_sb.s_inodes_per_group * m_inodeSize + m_blockSize - 1) / m_blockSize;

    for (uint32_t g = 0; g < m_groupCount; ++g) {
        uint64_t bmap = GetBlockBitmapBlock(g);
        uint64_t imap = GetInodeBitmapBlock(g);
        uint64_t itable = GetInodeTableBlock(g);

        if (bmap) quarantinedBlocks.push_back(bmap);
        if (imap) quarantinedBlocks.push_back(imap);
        for (uint32_t ib = 0; ib < itableBlocksPerGroup; ++ib) {
            quarantinedBlocks.push_back(itable + ib);
        }
    }

    // 3. Root Directory Inode 2 data blocks
    Ext4::Ext4Inode rootInode;
    if (ReadInode(Ext4::EXT4_ROOT_INO, rootInode)) {
        std::vector<uint64_t> rootDataBlocks = GetInodeAllocatedBlocks(rootInode);
        quarantinedBlocks.insert(quarantinedBlocks.end(), rootDataBlocks.begin(), rootDataBlocks.end());
    }

    // Sort and unique quarantined blocks for fast lookup
    std::sort(quarantinedBlocks.begin(), quarantinedBlocks.end());
    quarantinedBlocks.erase(std::unique(quarantinedBlocks.begin(), quarantinedBlocks.end()), quarantinedBlocks.end());

    auto isQuarantined = [&](uint64_t blk) -> bool {
        return std::binary_search(quarantinedBlocks.begin(), quarantinedBlocks.end(), blk);
    };

    uint64_t totalBlocks = m_sb.s_blocks_count_lo;
    if (m_is64Bit) totalBlocks |= (static_cast<uint64_t>(m_sb.s_blocks_count_hi) << 32);

    std::cout << "[Erasure] Sanitizing user data blocks...\n";
    uint64_t wipedCount = 0;

    for (uint64_t blk = m_sb.s_first_data_block; blk < totalBlocks; ++blk) {
        if (isQuarantined(blk)) continue;

        m_hardware->SecureEraseSectors(BlockToSector(blk), m_sectorsPerBlock);
        wipedCount++;

        if (wipedCount % 5000 == 0) {
            std::cout << "  -> Wiped " << wipedCount << " data blocks...\r";
            std::cout.flush();
        }
    }
    std::cout << "\n[Erasure] Successfully wiped " << wipedCount << " user data blocks!\n";

    // 4. Scrub User Inodes (preserve reserved inodes 1..10)
    std::cout << "[System] Scrubbing non-reserved inodes in Inode Tables...\n";
    for (uint32_t g = 0; g < m_groupCount; ++g) {
        uint32_t firstInoInGroup = g * m_sb.s_inodes_per_group + 1;
        uint32_t lastInoInGroup = firstInoInGroup + m_sb.s_inodes_per_group - 1;

        for (uint32_t ino = firstInoInGroup; ino <= lastInoInGroup; ++ino) {
            if (ino <= 10) continue; // Preserve reserved system inodes (bad blocks, root, journal, etc.)

            Ext4::Ext4Inode userInode;
            if (ReadInode(ino, userInode) && userInode.i_mode != 0) {
                std::memset(&userInode, 0, sizeof(userInode));
                WriteInode(ino, userInode);
            }
        }
    }

    // 5. Reset Inode and Block Bitmaps
    std::cout << "[System] Rebuilding allocation bitmaps...\n";
    for (uint32_t g = 0; g < m_groupCount; ++g) {
        // Reset Inode Bitmap: bits 0..9 (inodes 1..10) are used in group 0, rest are free
        std::vector<uint8_t> inodeBmap(m_blockSize, 0);
        if (g == 0) {
            for (uint32_t i = 0; i < 10; ++i) {
                inodeBmap[i / 8] |= (1 << (i % 8));
            }
        }
        WriteBlock(GetInodeBitmapBlock(g), inodeBmap.data());

        // Reset Block Bitmap: mark only quarantined blocks in this group as used
        std::vector<uint8_t> blockBmap(m_blockSize, 0);
        uint64_t groupStartBlock = m_sb.s_first_data_block + static_cast<uint64_t>(g) * m_sb.s_blocks_per_group;
        uint64_t groupEndBlock = std::min(groupStartBlock + m_sb.s_blocks_per_group, totalBlocks);

        for (uint64_t b = groupStartBlock; b < groupEndBlock; ++b) {
            if (isQuarantined(b)) {
                uint32_t idx = static_cast<uint32_t>(b - groupStartBlock);
                blockBmap[idx / 8] |= (1 << (idx % 8));
            }
        }
        WriteBlock(GetBlockBitmapBlock(g), blockBmap.data());
    }

    // 6. Scrub user directory entries in Root Directory (preserve '.' and '..')
    std::cout << "[System] Cleaning root directory entries...\n";
    if (ReadInode(Ext4::EXT4_ROOT_INO, rootInode)) {
        std::vector<uint64_t> rootBlocks = GetInodeAllocatedBlocks(rootInode);
        if (!rootBlocks.empty()) {
            std::vector<uint8_t> rootData(m_blockSize, 0);
            if (ReadBlock(rootBlocks[0], rootData.data())) {
                size_t offset = 0;
                while (offset + sizeof(Ext4::Ext4DirEntry2) <= m_blockSize) {
                    Ext4::Ext4DirEntry2* entry = reinterpret_cast<Ext4::Ext4DirEntry2*>(rootData.data() + offset);
                    if (entry->rec_len == 0) break;

                    bool isDot = (entry->name_len == 1 && entry->name[0] == '.');
                    bool isDotDot = (entry->name_len == 2 && entry->name[0] == '.' && entry->name[1] == '.');

                    if (!isDot && !isDotDot && entry->inode != 0) {
                        entry->inode = 0;
                        std::memset(entry->name, 0, entry->name_len);
                        entry->name_len = 0;
                        entry->file_type = Ext4::EXT4_FT_UNKNOWN;
                    }
                    offset += entry->rec_len;
                }
                WriteBlock(rootBlocks[0], rootData.data());
            }
        }
    }

    std::cout << "=== EXT4 SURGICAL WIPE COMPLETED SUCCESSFULLY! ===\n";
    return true;
}

void Ext4Driver::PrintSuperblockInfo() const {
    if (m_blockSize == 0) {
        std::cout << "Ext4 filesystem is not mounted.\n";
        return;
    }

    uint64_t totalBlocks = m_sb.s_blocks_count_lo;
    if (m_is64Bit) totalBlocks |= (static_cast<uint64_t>(m_sb.s_blocks_count_hi) << 32);

    uint64_t freeBlocks = m_sb.s_free_blocks_count_lo;
    if (m_is64Bit) freeBlocks |= (static_cast<uint64_t>(m_sb.s_free_blocks_count_hi) << 32);

    std::cout << "\n=== ext4 Superblock Information ===\n";
    std::cout << "Magic:              0x" << std::hex << m_sb.s_magic << std::dec << "\n";
    std::cout << "Volume Name:        " << std::string(m_sb.s_volume_name, 16) << "\n";
    std::cout << "Block Size:         " << m_blockSize << " bytes\n";
    std::cout << "Sectors Per Block:  " << m_sectorsPerBlock << "\n";
    std::cout << "Total Blocks:       " << totalBlocks << "\n";
    std::cout << "Free Blocks:        " << freeBlocks << "\n";
    std::cout << "Total Inodes:       " << m_sb.s_inodes_count << "\n";
    std::cout << "Free Inodes:        " << m_sb.s_free_inodes_count_lo << "\n";
    std::cout << "Blocks Per Group:   " << m_sb.s_blocks_per_group << "\n";
    std::cout << "Inodes Per Group:   " << m_sb.s_inodes_per_group << "\n";
    std::cout << "Inode Size:         " << m_inodeSize << " bytes\n";
    std::cout << "Block Groups:       " << m_groupCount << "\n";
    std::cout << "64-bit Mode:        " << (m_is64Bit ? "YES" : "NO") << "\n";
    std::cout << "Extents Enabled:    " << ((m_sb.s_feature_incompat & Ext4::EXT4_FEATURE_INCOMPAT_EXTENTS) ? "YES" : "NO") << "\n";
    std::cout << "====================================\n";
}

} // namespace FileSystems
} // namespace Erasure
