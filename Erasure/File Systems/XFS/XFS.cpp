#include "XFS.h"
#include <iostream>
#include <cstring>
#include <sstream>
#include <algorithm>

namespace Erasure {
namespace FileSystems {

// =============================================================================
// Constructor & Destructor
// =============================================================================

XfsDriver::XfsDriver(Core::IHardwareController* hardware)
    : m_hardware(hardware)
    , m_blockSize(0)
    , m_sectorsPerBlock(0)
    , m_bytesPerSector(0)
    , m_inodeSize(0)
    , m_inodesPerBlock(0)
    , m_agBlocks(0)
    , m_agCount(0)
    , m_agBlkLog(0)
    , m_inopbLog(0)
    , m_rootIno(0)
    , m_hasFtype(false)
{
    // Initialize superblock cache memory to zero
    std::memset(&m_sb, 0, sizeof(m_sb));
}

// =============================================================================
// Geometry & Block Addressing Helpers
// =============================================================================

/**
 * @brief Converts an XFS Filesystem Block Number (fsbno) into a linear disk block.
 *
 * In XFS filesystems spanning multiple Allocation Groups (AGs), block numbers in
 * extent records (xfs_bmbt_rec) and B+Tree pointers do NOT represent flat linear
 * block offsets. Instead, XFS packs the Allocation Group index into the upper bits:
 *
 *       fsbno = (AG_Number << sb_agblklog) | Block_Within_AG
 *
 * Because an AG's size (sb_agblocks) may not be an exact power of 2, directly
 * treating fsbno as a disk block offset would lead to massive physical offset drift.
 * This method extracts the AG number and relative block, then computes:
 *       linear_block = (AG_Number * sb_agblocks) + Block_Within_AG
 */
uint64_t XfsDriver::FsbToBlock(uint64_t fsbno) const {
    // If there is only 1 Allocation Group or agblklog is 0, the layout is completely flat
    if (m_agCount <= 1 || m_agBlkLog == 0) {
        return fsbno;
    }

    // Extract AG index from the high bits
    uint64_t agno = fsbno >> m_agBlkLog;
    // Extract the block index relative to the start of that AG
    uint64_t agbno = fsbno & ((1ULL << m_agBlkLog) - 1);

    // Compute true linear block from the start of the drive/partition
    return (agno * m_agBlocks) + agbno;
}

/**
 * @brief Converts a linear block index into an absolute LBA sector number.
 */
uint64_t XfsDriver::BlockToSector(uint64_t block) const {
    return block * m_sectorsPerBlock;
}

/**
 * @brief Reads an entire filesystem block from disk via the hardware controller.
 */
bool XfsDriver::ReadBlock(uint64_t block, void* buffer) const {
    if (!m_hardware || m_sectorsPerBlock == 0 || !buffer) return false;
    return m_hardware->ReadSectors(BlockToSector(block), m_sectorsPerBlock, buffer);
}

/**
 * @brief Writes an entire filesystem block onto disk via the hardware controller.
 */
bool XfsDriver::WriteBlock(uint64_t block, const void* buffer) {
    if (!m_hardware || m_sectorsPerBlock == 0 || !buffer) return false;
    return m_hardware->WriteSectors(BlockToSector(block), m_sectorsPerBlock, buffer);
}

// =============================================================================
// Filesystem Mount & Superblock Parsing
// =============================================================================

/**
 * @brief Mounts the XFS filesystem by reading Sector 0 (Primary Superblock).
 *
 * Execution flow:
 *   1. Queries physical sector size from the hardware controller.
 *   2. Reads the first sector(s) containing the XFS primary superblock.
 *   3. Verifies the 0x58465342 ("XFSB") magic signature.
 *   4. Converts all Big-Endian fields into CPU native order.
 *   5. Calculates block-to-sector ratios and Allocation Group bitmask parameters.
 */
bool XfsDriver::Mount() {
    if (!m_hardware) {
        std::cerr << "[XfsDriver] Error: Hardware controller is null.\n";
        return false;
    }

    // 1. Validate underlying storage device geometry
    Core::DeviceGeometry geo = m_hardware->GetGeometry();
    if (geo.bytesPerSector == 0) {
        std::cerr << "[XfsDriver] Error: Invalid device sector size (0 bytes).\n";
        return false;
    }
    m_bytesPerSector = geo.bytesPerSector;

    // 2. Read Sector 0 (Primary Superblock is at byte offset 0 of the partition)
    uint32_t sbSectors = (sizeof(XFS::XfsSuperblock) + m_bytesPerSector - 1) / m_bytesPerSector;
    std::vector<uint8_t> sbBuffer(sbSectors * m_bytesPerSector);

    if (!m_hardware->ReadSectors(0, sbSectors, sbBuffer.data())) {
        std::cerr << "[XfsDriver] Error: Failed to read Superblock sectors from disk.\n";
        return false;
    }

    // 3. Inspect magic number (first 4 bytes, Big-Endian)
    const XFS::XfsSuperblock* raw = reinterpret_cast<const XFS::XfsSuperblock*>(sbBuffer.data());
    uint32_t magic = XFS::be32_to_cpu(raw->sb_magicnum);

    if (magic != XFS::XFS_SB_MAGIC) {
        std::cerr << "[XfsDriver] Error: Invalid XFS magic 0x" << std::hex << magic << std::dec
                  << " (Expected 0x" << std::hex << XFS::XFS_SB_MAGIC << std::dec << ")\n";
        return false;
    }

    // 4. Unpack Big-Endian on-disk fields into host CPU variables
    m_sb.sb_magicnum     = magic;
    m_sb.sb_blocksize    = XFS::be32_to_cpu(raw->sb_blocksize);
    m_sb.sb_dblocks      = XFS::be64_to_cpu(raw->sb_dblocks);
    m_sb.sb_rblocks      = XFS::be64_to_cpu(raw->sb_rblocks);
    m_sb.sb_rextents     = XFS::be64_to_cpu(raw->sb_rextents);
    std::memcpy(m_sb.sb_uuid, raw->sb_uuid, 16);
    m_sb.sb_logstart     = XFS::be64_to_cpu(raw->sb_logstart);
    m_sb.sb_rootino      = XFS::be64_to_cpu(raw->sb_rootino);
    m_sb.sb_rbmino       = XFS::be64_to_cpu(raw->sb_rbmino);
    m_sb.sb_rsumino      = XFS::be64_to_cpu(raw->sb_rsumino);
    m_sb.sb_rextsize     = XFS::be32_to_cpu(raw->sb_rextsize);
    m_sb.sb_agblocks     = XFS::be32_to_cpu(raw->sb_agblocks);
    m_sb.sb_agcount      = XFS::be32_to_cpu(raw->sb_agcount);
    m_sb.sb_rbmblocks    = XFS::be32_to_cpu(raw->sb_rbmblocks);
    m_sb.sb_logblocks    = XFS::be32_to_cpu(raw->sb_logblocks);
    m_sb.sb_versionnum   = XFS::be16_to_cpu(raw->sb_versionnum);
    m_sb.sb_sectsize     = XFS::be16_to_cpu(raw->sb_sectsize);
    m_sb.sb_inodesize    = XFS::be16_to_cpu(raw->sb_inodesize);
    m_sb.sb_inopblock    = XFS::be16_to_cpu(raw->sb_inopblock);
    std::memcpy(m_sb.sb_fname, raw->sb_fname, 12);
    m_sb.sb_blocklog     = raw->sb_blocklog;
    m_sb.sb_sectlog      = raw->sb_sectlog;
    m_sb.sb_inodelog     = raw->sb_inodelog;
    m_sb.sb_inopblog     = raw->sb_inopblog;
    m_sb.sb_agblklog     = raw->sb_agblklog;
    m_sb.sb_rextslog     = raw->sb_rextslog;
    m_sb.sb_features2    = XFS::be32_to_cpu(raw->sb_features2);

    // 5. Cache frequently used calculation values
    m_blockSize       = m_sb.sb_blocksize;
    if (m_blockSize < m_bytesPerSector || (m_blockSize % m_bytesPerSector) != 0) {
        std::cerr << "[XfsDriver] Error: Invalid block size (" << m_blockSize << " bytes, sector size " << m_bytesPerSector << ").\n";
        return false;
    }
    m_sectorsPerBlock = m_blockSize / m_bytesPerSector;
    m_inodeSize       = m_sb.sb_inodesize ? m_sb.sb_inodesize : 256;
    m_inodesPerBlock  = m_sb.sb_inopblock ? m_sb.sb_inopblock : (m_blockSize / m_inodeSize);
    m_agBlocks        = m_sb.sb_agblocks;
    m_agCount         = m_sb.sb_agcount;
    m_agBlkLog        = m_sb.sb_agblklog;
    m_inopbLog        = m_sb.sb_inopblog;
    m_rootIno         = m_sb.sb_rootino;

    if (m_agBlkLog >= 32 || m_inopbLog >= 16) {
        std::cerr << "[XfsDriver] Error: Corrupted shift logs (agblklog=" << (int)m_agBlkLog
                  << ", inopblog=" << (int)m_inopbLog << ").\n";
        return false;
    }

    // Check if directories support 1-byte file type (FTYPE feature)
    m_hasFtype = (m_sb.sb_features2 & XFS::XFS_SB_VERSION2_FTYPE) != 0;

    return true;
}

// =============================================================================
// Inode Addressing & Inode I/O
// =============================================================================

/**
 * @brief Converts a 64-bit absolute inode number to its exact physical byte offset on disk.
 *
 * An XFS inode number encodes:
 *   [AG Number] : bits (inopblog + agblklog) and above
 *   [AG Block]  : bits inopblog to (inopblog + agblklog - 1)
 *   [Offset]    : lowest inopblog bits (index within that block * inodeSize)
 */
uint64_t XfsDriver::InoToByteOffset(uint64_t ino) const {
    uint64_t agno = 0;
    uint64_t agbno = 0;
    uint64_t offsetInBlock = 0;

    if (m_agCount > 1 && m_agBlkLog > 0) {
        // Multi-AG layout: shift out inode-in-block and AG-block bits to get AG number
        agno = ino >> (m_inopbLog + m_agBlkLog);
        // Mask out the block offset within the AG
        agbno = (ino >> m_inopbLog) & ((1ULL << m_agBlkLog) - 1);
        // Offset within the block
        offsetInBlock = (ino & ((1ULL << m_inopbLog) - 1)) * m_inodeSize;
    } else {
        // Single AG / flat layout
        agbno = ino >> m_inopbLog;
        offsetInBlock = (ino & ((1ULL << m_inopbLog) - 1)) * m_inodeSize;
    }

    uint64_t absBlock = (agno * m_agBlocks) + agbno;
    return (absBlock * m_blockSize) + offsetInBlock;
}

/**
 * @brief Reads an on-disk inode, decodes its core header, and extracts the data fork.
 */
bool XfsDriver::ReadInode(uint64_t ino, XFS::XfsDinodeCore& outCore, std::vector<uint8_t>& outFork) const {
    if (m_bytesPerSector == 0 || m_inodeSize == 0) return false;

    // Calculate physical sector range containing this inode
    uint64_t byteOffset = InoToByteOffset(ino);
    uint64_t startSector = byteOffset / m_bytesPerSector;
    uint32_t offsetInSector = static_cast<uint32_t>(byteOffset % m_bytesPerSector);
    uint32_t sectorsToRead = (offsetInSector + m_inodeSize + m_bytesPerSector - 1) / m_bytesPerSector;

    std::vector<uint8_t> buffer(sectorsToRead * m_bytesPerSector);
    if (!m_hardware->ReadSectors(startSector, sectorsToRead, buffer.data())) {
        return false;
    }

    const uint8_t* inodePtr = buffer.data() + offsetInSector;

    // Validate Inode Magic signature: 0x494E ("IN")
    uint16_t magic = XFS::be16_to_cpu(*reinterpret_cast<const uint16_t*>(inodePtr));
    if (magic != XFS::XFS_DINODE_MAGIC) {
        std::cerr << "[XfsDriver] Warning: Inode " << ino << " magic mismatch: 0x"
                  << std::hex << magic << std::dec << "\n";
        return false;
    }

    // Unpack core fields from Big-Endian on-disk format
    outCore.di_magic    = magic;
    outCore.di_mode     = XFS::be16_to_cpu(*reinterpret_cast<const uint16_t*>(inodePtr + 2));
    outCore.di_version  = static_cast<int8_t>(inodePtr[4]);
    outCore.di_format   = static_cast<int8_t>(inodePtr[5]);
    outCore.di_nlink    = XFS::be32_to_cpu(*reinterpret_cast<const uint32_t*>(inodePtr + 16));
    outCore.di_size     = static_cast<int64_t>(XFS::be64_to_cpu(*reinterpret_cast<const uint64_t*>(inodePtr + 56)));
    outCore.di_nblocks  = static_cast<int64_t>(XFS::be64_to_cpu(*reinterpret_cast<const uint64_t*>(inodePtr + 64)));
    outCore.di_extsize  = XFS::be32_to_cpu(*reinterpret_cast<const uint32_t*>(inodePtr + 72));
    outCore.di_nextents = XFS::be32_to_cpu(*reinterpret_cast<const uint32_t*>(inodePtr + 76));
    outCore.di_anextents= XFS::be16_to_cpu(*reinterpret_cast<const uint16_t*>(inodePtr + 80));
    outCore.di_forkoff  = inodePtr[82];
    outCore.di_aformat  = static_cast<int8_t>(inodePtr[83]);
    outCore.di_flags    = XFS::be16_to_cpu(*reinterpret_cast<const uint16_t*>(inodePtr + 90));

    // Determine inode core size based on version:
    //   - v1/v2 inodes: 100 bytes
    //   - v3 (v5 fs) inodes: 176 bytes
    size_t coreSize = (outCore.di_version == 3) ? XFS::XFS_DINODE_CORE_SIZE_V3 : XFS::XFS_DINODE_CORE_SIZE_V2;
    if (coreSize > m_inodeSize) coreSize = m_inodeSize;

    // Available space in data fork
    size_t maxForkSize = (m_inodeSize > coreSize) ? (m_inodeSize - coreSize) : 0;
    size_t forkSize = maxForkSize;
    if (outCore.di_forkoff > 0) {
        size_t attrOffset = static_cast<size_t>(outCore.di_forkoff) * 8;
        if (attrOffset > coreSize && attrOffset <= m_inodeSize) {
            forkSize = attrOffset - coreSize;
        }
    }

    outFork.resize(forkSize);
    if (forkSize > 0) {
        std::memcpy(outFork.data(), inodePtr + coreSize, forkSize);
    }

    return true;
}

/**
 * @brief Obliterates an on-disk inode record by zero-filling all its bytes on disk.
 *
 * This wipes the file mode, size, timestamps, extent count, and inline data fork,
 * guaranteeing that no forensic recovery tool can reconstruct metadata or block pointers.
 */
bool XfsDriver::WipeInodeOnDisk(uint64_t ino) {
    if (m_bytesPerSector == 0 || m_inodeSize == 0) return false;

    uint64_t byteOffset = InoToByteOffset(ino);
    uint64_t startSector = byteOffset / m_bytesPerSector;
    uint32_t offsetInSector = static_cast<uint32_t>(byteOffset % m_bytesPerSector);
    uint32_t sectorsToRead = (offsetInSector + m_inodeSize + m_bytesPerSector - 1) / m_bytesPerSector;

    std::vector<uint8_t> buffer(sectorsToRead * m_bytesPerSector);
    if (!m_hardware->ReadSectors(startSector, sectorsToRead, buffer.data())) {
        return false;
    }

    uint8_t* inodePtr = buffer.data() + offsetInSector;

    // Zero out the entire inode structure (headers, attributes, timestamps, and data fork)
    std::memset(inodePtr, 0, m_inodeSize);

    // Commit sanitized sector buffer back to disk
    if (!m_hardware->WriteSectors(startSector, sectorsToRead, buffer.data())) {
        return false;
    }

    // Scrub the inode from the Allocation Group Inode (AGI) unlinked hash buckets
    ScrubAgiUnlinkedBucket(ino);

    return true;
}

/**
 * @brief Purges an inode from the AGI unlinked hash buckets (agi_unlinked[64]).
 *
 * In XFS, unlinked inodes that were held open before deletion are linked into
 * agi_unlinked buckets in Sector 2 of the AG. This function clears any matching
 * hash entry to XFS_AGI_UNLINKED_NULL so recovery tools cannot reconstruct the chain.
 */
bool XfsDriver::ScrubAgiUnlinkedBucket(uint64_t ino) {
    if (m_bytesPerSector == 0 || m_blockSize == 0) return false;

    // 1. Determine Allocation Group index
    uint64_t agno = 0;
    if (m_agCount > 1 && m_agBlkLog > 0) {
        agno = ino >> (m_inopbLog + m_agBlkLog);
    }
    if (agno >= m_agCount) return false;

    // 2. Locate Sector 2 (AGI header) of this Allocation Group
    // Each AG begins at (agno * m_agBlocks). Sector 2 is at byte offset 1024 from AG start.
    uint64_t agByteStart = (agno * m_agBlocks) * m_blockSize;
    uint64_t agiByteOffset = agByteStart + (2 * 512);
    uint64_t startSector = agiByteOffset / m_bytesPerSector;
    uint32_t offsetInSector = static_cast<uint32_t>(agiByteOffset % m_bytesPerSector);
    uint32_t sectorsToRead = (offsetInSector + sizeof(XFS::XfsAgi) + m_bytesPerSector - 1) / m_bytesPerSector;

    std::vector<uint8_t> buffer(sectorsToRead * m_bytesPerSector);
    if (!m_hardware->ReadSectors(startSector, sectorsToRead, buffer.data())) {
        return false;
    }

    XFS::XfsAgi* agi = reinterpret_cast<XFS::XfsAgi*>(buffer.data() + offsetInSector);
    if (XFS::be32_to_cpu(agi->agi_magicnum) != XFS::XFS_AGI_MAGIC) {
        return false;
    }

    // 3. Scan all 64 hash buckets in the AGI unlinked table
    // While standard XFS hashes via (agino % 64), scanning all 64 entries in RAM
    // guarantees that corrupted or collision-resolved hash chains are completely scrubbed.
    uint32_t agino = static_cast<uint32_t>((ino >> m_inopbLog) & ((1ULL << m_agBlkLog) - 1));
    bool modified = false;

    for (uint32_t b = 0; b < 64; ++b) {
        uint32_t headIno = XFS::be32_to_cpu(agi->agi_unlinked[b]);
        if (headIno == ino || headIno == agino) {
            // Obliterate the unlinked hash bucket head pointer!
            agi->agi_unlinked[b] = XFS::cpu_to_be32(XFS::XFS_AGI_UNLINKED_NULL);
            std::cout << "  -> [AGI Header] Purged Inode " << ino << " from unlinked hash bucket ["
                      << b << "] in AG " << agno << "\n";
            modified = true;
        }
    }

    if (modified) {
        return m_hardware->WriteSectors(startSector, sectorsToRead, buffer.data());
    }

    return true;
}

/**
 * @brief Scans the Intent Log (Journal) and zeroes out transaction items referencing an inode or filename.
 */
bool XfsDriver::ScrubJournalForInode(uint64_t ino, const std::string& filename) {
    if (m_sb.sb_logstart == 0 || m_sb.sb_logblocks == 0 || m_blockSize == 0) {
        return true; // External log or no log configured
    }

    uint64_t logStartBlock = FsbToBlock(m_sb.sb_logstart);
    uint32_t logBlocks = m_sb.sb_logblocks;

    uint64_t beIno = XFS::cpu_to_be64(ino);
    std::vector<uint8_t> blockBuf(m_blockSize);
    uint32_t scrubbedBlocks = 0;

    for (uint32_t b = 0; b < logBlocks; ++b) {
        uint64_t currentBlock = logStartBlock + b;
        if (!ReadBlock(currentBlock, blockBuf.data())) continue;

        bool modified = false;

        // 1. Search for 64-bit Big-Endian Inode Number in the log block
        for (size_t off = 0; off + 8 <= m_blockSize; off += 4) {
            if (std::memcmp(&blockBuf[off], &beIno, 8) == 0) {
                // Zero out the surrounding log operation chunk (128 bytes)
                size_t wipeStart = (off >= 64) ? (off - 64) : 0;
                size_t wipeEnd = std::min(off + 64, static_cast<size_t>(m_blockSize));
                std::memset(&blockBuf[wipeStart], 0, wipeEnd - wipeStart);
                modified = true;
            }
        }

        // 2. Search for the filename string in the log block (if length >= 3)
        if (filename.length() >= 3 && filename.length() <= m_blockSize) {
            size_t flen = filename.length();
            for (size_t off = 0; off + flen <= m_blockSize; ++off) {
                if (std::memcmp(&blockBuf[off], filename.data(), flen) == 0) {
                    // Zero out the filename and surrounding metadata in this log block
                    size_t wipeStart = (off >= 32) ? (off - 32) : 0;
                    size_t wipeEnd = std::min(off + flen + 32, static_cast<size_t>(m_blockSize));
                    std::memset(&blockBuf[wipeStart], 0, wipeEnd - wipeStart);
                    modified = true;
                }
            }
        }

        if (modified) {
            WriteBlock(currentBlock, blockBuf.data());
            scrubbedBlocks++;
        }
    }

    if (scrubbedBlocks > 0) {
        std::cout << "  -> [Journal] Purged historical transaction records across "
                  << scrubbedBlocks << " log blocks!\n";
    }

    return true;
}

// =============================================================================
// Extent & B+Tree Traversal
// =============================================================================

/**
 * @brief Gathers all physical extents allocated to a file.
 *
 * If the file is stored in extent format (di_format == 2), reads extents from the data fork.
 * If the file escalated to a B+Tree (di_format == 3), traverses the tree and records all
 * intermediate/leaf B+Tree metadata blocks in outBtreeBlocks for subsequent erasure.
 */
std::vector<XFS::XfsBmbtRec::ExtentInfo> XfsDriver::GetInodeExtents(
    const XFS::XfsDinodeCore& core,
    const std::vector<uint8_t>& forkData,
    std::vector<uint64_t>& outBtreeBlocks) const
{
    std::vector<XFS::XfsBmbtRec::ExtentInfo> extents;

    if (core.di_format == XFS::XFS_DINODE_FMT_EXTENTS) {
        // Direct array of 16-byte extent records
        size_t recCount = forkData.size() / sizeof(XFS::XfsBmbtRec);
        if (core.di_nextents < recCount) recCount = core.di_nextents;

        const XFS::XfsBmbtRec* recs = reinterpret_cast<const XFS::XfsBmbtRec*>(forkData.data());
        for (size_t i = 0; i < recCount; ++i) {
            extents.push_back(recs[i].Unpack());
        }
    } else if (core.di_format == XFS::XFS_DINODE_FMT_BTREE) {
        // B+Tree root stored in data fork: header (XfsBmdrBlock) + keys + pointers
        if (forkData.size() >= sizeof(XFS::XfsBmdrBlock)) {
            const XFS::XfsBmdrBlock* bmdr = reinterpret_cast<const XFS::XfsBmdrBlock*>(forkData.data());
            uint16_t numRecs = XFS::be16_to_cpu(bmdr->bb_numrecs);

            // In XFS BMBT root, pointers follow keys.
            // On standard on-disk XFS, pointers start at (sizeof(XfsBmdrBlock) + maxRecs * 8).
            // In packed synthetic test layouts, pointers may follow immediately at (sizeof(XfsBmdrBlock) + numRecs * 8).
            size_t avail = (forkData.size() >= sizeof(XFS::XfsBmdrBlock)) ? (forkData.size() - sizeof(XFS::XfsBmdrBlock)) : 0;
            size_t maxRecs = avail / 16; // 16 bytes per (key + ptr)
            size_t ptrOffsetSparse = sizeof(XFS::XfsBmdrBlock) + (maxRecs * 8);
            size_t ptrOffsetPacked = sizeof(XFS::XfsBmdrBlock) + (numRecs * 8);

            size_t ptrOffset = ptrOffsetPacked;
            if (maxRecs > numRecs && ptrOffsetSparse + (numRecs * 8) <= forkData.size()) {
                const uint64_t* sparsePtrs = reinterpret_cast<const uint64_t*>(forkData.data() + ptrOffsetSparse);
                if (sparsePtrs[0] != 0) {
                    ptrOffset = ptrOffsetSparse;
                }
            }

            if (ptrOffset + (numRecs * 8) <= forkData.size()) {
                const uint64_t* ptrs = reinterpret_cast<const uint64_t*>(forkData.data() + ptrOffset);
                for (uint16_t i = 0; i < numRecs; ++i) {
                    uint64_t childFsb = XFS::be64_to_cpu(ptrs[i]);
                    if (childFsb != 0) {
                        outBtreeBlocks.push_back(childFsb);
                        CollectBtreeExtents(childFsb, extents, outBtreeBlocks);
                    }
                }
            }
        }
    }

    return extents;
}

/**
 * @brief Recursively traverses indirect on-disk B+Tree blocks.
 *
 * Extracts all leaf file extents and tracks every metadata block fsbno so that
 * all historical extent pointers can be wiped clean with zeros.
 */
void XfsDriver::CollectBtreeExtents(
    uint64_t btreeFsb,
    std::vector<XFS::XfsBmbtRec::ExtentInfo>& outExtents,
    std::vector<uint64_t>& outBtreeBlocks) const
{
    uint64_t linearBlock = FsbToBlock(btreeFsb);
    std::vector<uint8_t> blockBuf(m_blockSize);
    if (!ReadBlock(linearBlock, blockBuf.data())) return;

    const XFS::XfsBtreeBlock* hdr = reinterpret_cast<const XFS::XfsBtreeBlock*>(blockBuf.data());
    uint16_t level = XFS::be16_to_cpu(hdr->bb_level);
    uint16_t numRecs = XFS::be16_to_cpu(hdr->bb_numrecs);

    // Header size differs between v4 (24 bytes) and v5 (sizeof(XfsBtreeBlock) = 72 bytes with CRC)
    uint32_t magic = XFS::be32_to_cpu(hdr->bb_magic);
    size_t hdrSize = (magic == XFS::XFS_BMAP_CRC_MAGIC) ? sizeof(XFS::XfsBtreeBlock) : 24;

    if (level == 0) {
        // Leaf block: contains raw extent records (XfsBmbtRec)
        size_t availableSpace = m_blockSize - hdrSize;
        size_t maxRecs = availableSpace / sizeof(XFS::XfsBmbtRec);
        if (numRecs > maxRecs) numRecs = static_cast<uint16_t>(maxRecs);

        const XFS::XfsBmbtRec* recs = reinterpret_cast<const XFS::XfsBmbtRec*>(blockBuf.data() + hdrSize);
        for (uint16_t i = 0; i < numRecs; ++i) {
            outExtents.push_back(recs[i].Unpack());
        }
    } else {
        // Node block: contains keys followed by child block pointers
        size_t avail = (m_blockSize >= hdrSize) ? (m_blockSize - hdrSize) : 0;
        size_t maxRecs = avail / 16;
        size_t ptrOffsetSparse = hdrSize + (maxRecs * 8);
        size_t ptrOffsetPacked = hdrSize + (numRecs * 8);

        size_t ptrOffset = ptrOffsetPacked;
        if (maxRecs > numRecs && ptrOffsetSparse + (numRecs * 8) <= m_blockSize) {
            const uint64_t* sparsePtrs = reinterpret_cast<const uint64_t*>(blockBuf.data() + ptrOffsetSparse);
            if (sparsePtrs[0] != 0) {
                ptrOffset = ptrOffsetSparse;
            }
        }

        if (ptrOffset + (numRecs * 8) <= m_blockSize) {
            const uint64_t* ptrs = reinterpret_cast<const uint64_t*>(blockBuf.data() + ptrOffset);
            for (uint16_t i = 0; i < numRecs; ++i) {
                uint64_t childFsb = XFS::be64_to_cpu(ptrs[i]);
                if (childFsb != 0) {
                    outBtreeBlocks.push_back(childFsb);
                    CollectBtreeExtents(childFsb, outExtents, outBtreeBlocks);
                }
            }
        }
    }
}

// =============================================================================
// Directory Traversal & Entry Scrubbing
// =============================================================================

/**
 * @brief Splits a path string by '/' or '\\' into individual token names.
 */
std::vector<std::string> XfsDriver::TokenizePath(const std::string& path) const {
    std::vector<std::string> tokens;
    std::string token;
    std::string normalized = path;

    for (char& c : normalized) {
        if (c == '\\') c = '/';
    }

    std::stringstream ss(normalized);
    while (std::getline(ss, token, '/')) {
        if (!token.empty() && token != ".") {
            tokens.push_back(token);
        }
    }
    return tokens;
}

/**
 * @brief Searches a directory inode for a target entry name.
 *
 * Implements lookup across:
 *   1. Shortform Directories (di_format == 1): stored inside the inode fork.
 *   2. Block / Extent Directories (di_format == 2): stored in external data blocks.
 */
XfsDriver::DirectorySearchResult XfsDriver::FindEntryInDirectory(
    uint64_t dirIno,
    const std::string& targetName) const
{
    DirectorySearchResult result = { false, false, 0, false, 0, 0, 0 };

    XFS::XfsDinodeCore dirCore;
    std::vector<uint8_t> dirFork;
    if (!ReadInode(dirIno, dirCore, dirFork)) {
        return result;
    }

    // Verify that target inode is indeed a directory
    if ((dirCore.di_mode & XFS::XFS_S_IFMT) != XFS::XFS_S_IFDIR) {
        return result;
    }

    // -------------------------------------------------------------------------
    // 1. Shortform Directory (XFS_DINODE_FMT_LOCAL)
    // -------------------------------------------------------------------------
    if (dirCore.di_format == XFS::XFS_DINODE_FMT_LOCAL) {
        if (dirFork.size() < sizeof(XFS::XfsDir2SfHdr)) return result;

        const XFS::XfsDir2SfHdr* sfHdr = reinterpret_cast<const XFS::XfsDir2SfHdr*>(dirFork.data());
        uint8_t count = sfHdr->count;
        uint8_t i8count = sfHdr->i8count;

        // In shortform directories, parent inode is 4 bytes if i8count==0, or 8 bytes if i8count>0
        size_t offset = (i8count > 0) ? 10 : 6;
        for (uint8_t i = 0; i < count && offset < dirFork.size(); ++i) {
            uint8_t namelen = dirFork[offset];
            if (offset + 3 + namelen > dirFork.size()) break;

            // Extract entry name
            std::string name(reinterpret_cast<const char*>(&dirFork[offset + 3]), namelen);
            size_t inoOffset = offset + 3 + namelen + (m_hasFtype ? 1 : 0);

            // Read inode number (32-bit or 64-bit depending on i8count)
            uint64_t entryIno = 0;
            if (i8count > 0) {
                if (inoOffset + 8 > dirFork.size()) break;
                entryIno = XFS::be64_to_cpu(*reinterpret_cast<const uint64_t*>(&dirFork[inoOffset]));
            } else {
                if (inoOffset + 4 > dirFork.size()) break;
                entryIno = XFS::be32_to_cpu(*reinterpret_cast<const uint32_t*>(&dirFork[inoOffset]));
            }

            size_t entryLen = (inoOffset + (i8count > 0 ? 8 : 4)) - offset;

            if (name == targetName) {
                result.found        = true;
                result.inodeNum     = entryIno;
                result.isShortform  = true;
                result.dirBlock     = 0;
                result.entryOffset  = offset;
                result.entryLength  = entryLen;

                // Inspect child inode to check if it is a directory
                XFS::XfsDinodeCore childCore;
                std::vector<uint8_t> childFork;
                if (ReadInode(entryIno, childCore, childFork)) {
                    result.isDirectory = (childCore.di_mode & XFS::XFS_S_IFMT) == XFS::XFS_S_IFDIR;
                }
                return result;
            }

            offset += entryLen;
        }
    }
    // -------------------------------------------------------------------------
    // 2. Block or Extent Directory (XFS_DINODE_FMT_EXTENTS / BTREE)
    // -------------------------------------------------------------------------
    else if (dirCore.di_format == XFS::XFS_DINODE_FMT_EXTENTS || dirCore.di_format == XFS::XFS_DINODE_FMT_BTREE) {
        std::vector<uint64_t> btreeBlocks;
        std::vector<XFS::XfsBmbtRec::ExtentInfo> dirExtents = GetInodeExtents(dirCore, dirFork, btreeBlocks);

        std::vector<uint8_t> blockBuf(m_blockSize);
        for (const auto& ext : dirExtents) {
            for (uint32_t b = 0; b < ext.blockcount; ++b) {
                uint64_t linearBlock = FsbToBlock(ext.startblock + b);
                if (!ReadBlock(linearBlock, blockBuf.data())) continue;

                uint32_t magic = XFS::be32_to_cpu(*reinterpret_cast<const uint32_t*>(blockBuf.data()));
                size_t hdrSize = 16;
                if (magic == XFS::XFS_DIR3_BLOCK_MAGIC || magic == XFS::XFS_DIR3_DATA_MAGIC) {
                    hdrSize = 64; // v5 data header with CRC protection
                }

                size_t off = hdrSize;
                while (off + 8 < m_blockSize) {
                    uint16_t freetag = *reinterpret_cast<const uint16_t*>(&blockBuf[off]);
                    if (freetag == 0xFFFF) {
                        // Unused region: skip ahead by free length
                        uint16_t freeLen = XFS::be16_to_cpu(*reinterpret_cast<const uint16_t*>(&blockBuf[off + 2]));
                        if (freeLen == 0) break;
                        off += freeLen;
                        continue;
                    }

                    // Active directory data entry
                    uint64_t entryIno = XFS::be64_to_cpu(*reinterpret_cast<const uint64_t*>(&blockBuf[off]));
                    uint8_t namelen = blockBuf[off + 8];
                    if (namelen == 0 || off + 9 + namelen > m_blockSize) break;

                    std::string name(reinterpret_cast<const char*>(&blockBuf[off + 9]), namelen);
                    // Entries are aligned to 8-byte boundaries
                    size_t entryLen = ((namelen + 8 + 1 + (m_hasFtype ? 1 : 0) + 2 + 7) & ~7);

                    if (name == targetName) {
                        result.found        = true;
                        result.inodeNum     = entryIno;
                        result.isShortform  = false;
                        result.dirBlock     = linearBlock;
                        result.entryOffset  = off;
                        result.entryLength  = entryLen;

                        XFS::XfsDinodeCore childCore;
                        std::vector<uint8_t> childFork;
                        if (ReadInode(entryIno, childCore, childFork)) {
                            result.isDirectory = (childCore.di_mode & XFS::XFS_S_IFMT) == XFS::XFS_S_IFDIR;
                        }
                        return result;
                    }

                    off += entryLen;
                }
            }
        }
    }

    return result;
}

/**
 * @brief Scrubs a directory entry from the parent directory block on disk.
 */
bool XfsDriver::WipeDirectoryEntry(
    uint64_t parentDirIno,
    const DirectorySearchResult& entry)
{
    if (entry.isShortform) {
        // Read parent directory inode
        uint64_t byteOffset = InoToByteOffset(parentDirIno);
        uint64_t startSector = byteOffset / m_bytesPerSector;
        uint32_t offsetInSector = static_cast<uint32_t>(byteOffset % m_bytesPerSector);
        uint32_t sectorsToRead = (offsetInSector + m_inodeSize + m_bytesPerSector - 1) / m_bytesPerSector;

        std::vector<uint8_t> buffer(sectorsToRead * m_bytesPerSector);
        if (!m_hardware->ReadSectors(startSector, sectorsToRead, buffer.data())) {
            return false;
        }

        uint8_t* inodePtr = buffer.data() + offsetInSector;
        int8_t version = static_cast<int8_t>(inodePtr[4]);
        size_t coreSize = (version == 3) ? XFS::XFS_DINODE_CORE_SIZE_V3 : XFS::XFS_DINODE_CORE_SIZE_V2;

        uint8_t* forkPtr = inodePtr + coreSize;
        size_t maxForkSize = m_inodeSize - coreSize;
        uint8_t forkoff = inodePtr[82]; // di_forkoff
        if (forkoff > 0) {
            size_t attrOffset = static_cast<size_t>(forkoff) * 8;
            if (attrOffset > coreSize && attrOffset <= m_inodeSize) {
                maxForkSize = attrOffset - coreSize;
            }
        }

        if (coreSize + entry.entryOffset + entry.entryLength <= m_inodeSize) {
            // Shift subsequent shortform entries forward to compact the table
            size_t moveSrc = entry.entryOffset + entry.entryLength;
            size_t moveBytes = (maxForkSize > moveSrc) ? (maxForkSize - moveSrc) : 0;
            if (moveBytes > 0) {
                std::memmove(forkPtr + entry.entryOffset, forkPtr + moveSrc, moveBytes);
            }
            // Zero out trailing vacated bytes at the end of the fork
            std::memset(forkPtr + maxForkSize - entry.entryLength, 0, entry.entryLength);

            // Decrement active entry count in shortform directory header
            XFS::XfsDir2SfHdr* sfHdr = reinterpret_cast<XFS::XfsDir2SfHdr*>(forkPtr);
            if (sfHdr->count > 0) {
                sfHdr->count--;
            }

            // Decrement di_size in parent directory inode header
            int64_t curSize = static_cast<int64_t>(XFS::be64_to_cpu(*reinterpret_cast<const uint64_t*>(inodePtr + 56)));
            if (curSize >= static_cast<int64_t>(entry.entryLength)) {
                *reinterpret_cast<uint64_t*>(inodePtr + 56) = XFS::cpu_to_be64(curSize - entry.entryLength);
            }
        }

        // Commit updated parent inode to disk
        return m_hardware->WriteSectors(startSector, sectorsToRead, buffer.data());
    } else {
        // Block / Extent Directory: transform entry into an unused freetag region
        std::vector<uint8_t> blockBuf(m_blockSize);
        if (!ReadBlock(entry.dirBlock, blockBuf.data())) return false;

        if (entry.entryOffset + entry.entryLength <= m_blockSize) {
            // Tag offset with 0xFFFF to mark it free space
            *reinterpret_cast<uint16_t*>(&blockBuf[entry.entryOffset]) = 0xFFFF;
            // Record free length
            *reinterpret_cast<uint16_t*>(&blockBuf[entry.entryOffset + 2]) = XFS::cpu_to_be16(static_cast<uint16_t>(entry.entryLength));
            // Zero out the remaining bytes of the deleted entry (wiping filename and inode pointer)
            if (entry.entryLength > 4) {
                std::memset(&blockBuf[entry.entryOffset + 4], 0, entry.entryLength - 4);
            }
        }

        // --- SCRUB DIRECTORY BLOCK TAIL LEAF HASH ARRAY ---
        // In XFS single-block directories (XD2B / XDB3), a leaf hash array sits at the
        // bottom of the block growing backwards from the 8-byte tail structure.
        // We locate the leaf entry referencing our data offset, zero its hash and address,
        // and increment tail->stale so no forensic hash remnant remains in the block tail!
        if (m_blockSize >= sizeof(XFS::XfsDir2BlockTail)) {
            size_t tailOffset = m_blockSize - sizeof(XFS::XfsDir2BlockTail);
            XFS::XfsDir2BlockTail* tail = reinterpret_cast<XFS::XfsDir2BlockTail*>(&blockBuf[tailOffset]);
            uint32_t count = XFS::be32_to_cpu(tail->count);
            uint32_t stale = XFS::be32_to_cpu(tail->stale);

            // Safety check: ensure count doesn't overflow backwards into the block header
            if (count > 0 && (count * sizeof(XFS::XfsDir2LeafEntry)) <= tailOffset) {
                size_t leafArrayOffset = tailOffset - (count * sizeof(XFS::XfsDir2LeafEntry));
                XFS::XfsDir2LeafEntry* leafArray = reinterpret_cast<XFS::XfsDir2LeafEntry*>(&blockBuf[leafArrayOffset]);

                // In XFS directory leaf entries, data offset is addressed in 8-byte units
                uint32_t targetAddr = static_cast<uint32_t>(entry.entryOffset / 8);

                for (uint32_t i = 0; i < count; ++i) {
                    uint32_t leafAddr = XFS::be32_to_cpu(leafArray[i].address);
                    if (leafAddr == targetAddr) {
                        // Obliterate the 32-bit name hash and data address!
                        leafArray[i].hashval = 0;
                        leafArray[i].address = 0;
                        tail->stale = XFS::cpu_to_be32(stale + 1);
                        std::cout << "  -> [Directory Tail] Scrubbed leaf hash entry at index " << i
                                  << " (Offset " << (leafArrayOffset + i * sizeof(XFS::XfsDir2LeafEntry)) << ")\n";
                        break;
                    }
                }
            }
        }

        // Commit updated directory data block to disk
        return WriteBlock(entry.dirBlock, blockBuf.data());
    }
}

// =============================================================================
// File Erasure Lifecycle (EraseFile)
// =============================================================================

/**
 * @brief Surgical file erasure: overwrites data, B+trees, inode, and dir entries with 0s.
 */
bool XfsDriver::EraseFile(const std::string& relativePath) {
    if (m_blockSize == 0 || m_sectorsPerBlock == 0) {
        std::cerr << "[XfsDriver] Error: Filesystem not mounted.\n";
        return false;
    }

    std::vector<std::string> pathTokens = TokenizePath(relativePath);
    if (pathTokens.empty()) {
        std::cerr << "[XfsDriver] Error: Empty path provided.\n";
        return false;
    }

    std::cout << "\n--- Initiating XFS Secure EraseFile for: " << relativePath << " ---\n";

    // Step 1: Directory Traversal from root inode
    uint64_t currentDirIno = m_rootIno;
    DirectorySearchResult searchRes;

    for (size_t i = 0; i < pathTokens.size(); ++i) {
        const std::string& token = pathTokens[i];
        bool isLastToken = (i == pathTokens.size() - 1);

        std::cout << "[Parser] Searching for '" << token << "' in directory Inode " << currentDirIno << "...\n";
        searchRes = FindEntryInDirectory(currentDirIno, token);

        if (!searchRes.found) {
            std::cerr << "[Parser] Error: Path component '" << token << "' not found!\n";
            return false;
        }

        if (!isLastToken) {
            if (!searchRes.isDirectory) {
                std::cerr << "[Parser] Error: '" << token << "' is not a directory!\n";
                return false;
            }
            currentDirIno = searchRes.inodeNum;
        }
    }

    uint64_t targetIno = searchRes.inodeNum;
    std::cout << "[Erasure] Target file located. Target Inode: " << targetIno << "\n";

    XFS::XfsDinodeCore fileCore;
    std::vector<uint8_t> fileFork;
    if (!ReadInode(targetIno, fileCore, fileFork)) {
        std::cerr << "[Erasure] Error: Failed to read target Inode " << targetIno << "\n";
        return false;
    }

    // Step 2: Data Obliteration
    if (fileCore.di_format == XFS::XFS_DINODE_FMT_LOCAL) {
        // File data resides inside the inode's data fork itself (shortform / inline)
        std::cout << "  -> File data is stored locally in Inode core fork. Will be obliterated during Inode wipe.\n";
    } else {
        std::vector<uint64_t> btreeBlocks;
        std::vector<XFS::XfsBmbtRec::ExtentInfo> extents = GetInodeExtents(fileCore, fileFork, btreeBlocks);

        std::cout << "  -> File occupies " << extents.size() << " extents across disk. Beginning block zeroing...\n";

        // 2a. Overwrite all data blocks with 0x00 via SecureEraseSectors
        for (const auto& ext : extents) {
            if (ext.blockcount == 0) continue;

            uint64_t linearBlock = FsbToBlock(ext.startblock);
            uint64_t startSector = BlockToSector(linearBlock);
            uint32_t sectorCount = ext.blockcount * m_sectorsPerBlock;

            std::cout << "    [Data] Zeroing " << ext.blockcount << " blocks (Sectors "
                      << startSector << " to " << (startSector + sectorCount - 1) << ")...\n";

            if (!m_hardware->SecureEraseSectors(startSector, sectorCount)) {
                std::cerr << "[Erasure] Error: Failed to erase data sectors starting at " << startSector << "\n";
                return false;
            }
        }

        // 2b. Overwrite all B+Tree metadata blocks with 0x00 if file escalated to B+Tree format
        if (!btreeBlocks.empty()) {
            std::cout << "  -> Obliterating " << btreeBlocks.size() << " B+Tree metadata blocks...\n";
            for (uint64_t btreeFsb : btreeBlocks) {
                uint64_t linearBlock = FsbToBlock(btreeFsb);
                uint64_t startSector = BlockToSector(linearBlock);

                std::cout << "    [B+Tree] Zeroing B+Tree block at sector " << startSector << "...\n";
                if (!m_hardware->SecureEraseSectors(startSector, m_sectorsPerBlock)) {
                    std::cerr << "[Erasure] Warning: Failed to zero B+Tree sector " << startSector << "\n";
                }
            }
        }
    }

    // Step 3: Metadata Sanitization: Overwrite the Inode on disk with zeros
    std::cout << "  -> Obliterating on-disk Inode record (" << m_inodeSize << " bytes)...\n";
    if (!WipeInodeOnDisk(targetIno)) {
        std::cerr << "[Erasure] Error: Failed to wipe Inode " << targetIno << " on disk.\n";
        return false;
    }

    // Step 4: Directory Sanitization: Scrub entry from parent directory
    std::cout << "  -> Scrubbing directory entry from parent Inode " << currentDirIno << "...\n";
    if (!WipeDirectoryEntry(currentDirIno, searchRes)) {
        std::cerr << "[Erasure] Warning: Failed to scrub parent directory entry.\n";
    }

    // Step 5: Journal Sanitization: Scrub historical metadata transactions from the Intent Log
    std::string targetFilename = pathTokens.back();
    std::cout << "  -> Scrubbing Intent Log (Journal) transactions for Inode " << targetIno
              << " (\"" << targetFilename << "\")...\n";
    ScrubJournalForInode(targetIno, targetFilename);

    std::cout << "--- XFS File Secure Erasure Successfully Completed! ---\n";
    return true;
}

// =============================================================================
// Volume-Wide Surgical Wipe (WipeVolume)
// =============================================================================

/**
 * @brief Obliterates all user data blocks and records across the volume
 * while quarantining essential headers to preserve filesystem mountability.
 */
bool XfsDriver::WipeVolume() {
    if (m_blockSize == 0 || m_sectorsPerBlock == 0) return false;

    std::cout << "\n=== INITIATING XFS SURGICAL VOLUME WIPE ===\n";
    std::cout << "[Quarantine] Mapping critical filesystem structures across " << m_agCount << " Allocation Groups...\n";

    // Vector of quarantined block ranges: pair of (startBlock, blockCount)
    std::vector<std::pair<uint64_t, uint32_t>> quarantinedRanges;

    // 1. Quarantine Allocation Group Headers across every AG:
    //    Each AG starts with: Sector 0 (Superblock), Sector 1 (AGF), Sector 2 (AGI), Sector 3 (AGFL)
    uint32_t agHeaderBlocks = (4 * m_bytesPerSector + m_blockSize - 1) / m_blockSize;
    for (uint32_t ag = 0; ag < m_agCount; ++ag) {
        uint64_t agStartBlock = static_cast<uint64_t>(ag) * m_agBlocks;
        quarantinedRanges.push_back({agStartBlock, agHeaderBlocks});
    }

    // 2. Quarantine Root Inode Block and its allocated directory extents
    uint64_t rootByteOffset = InoToByteOffset(m_rootIno);
    uint64_t rootBlock = rootByteOffset / m_blockSize;
    quarantinedRanges.push_back({rootBlock, 1});

    XFS::XfsDinodeCore rootCore;
    std::vector<uint8_t> rootFork;
    if (ReadInode(m_rootIno, rootCore, rootFork)) {
        std::vector<uint64_t> btreeBlocks;
        std::vector<XFS::XfsBmbtRec::ExtentInfo> rootExtents = GetInodeExtents(rootCore, rootFork, btreeBlocks);
        for (const auto& ext : rootExtents) {
            quarantinedRanges.push_back({FsbToBlock(ext.startblock), ext.blockcount});
        }
    }

    // 3. Quarantine Journal Blocks (specifically sanitized and reset with clean header in Step 5)
    if (m_sb.sb_logstart > 0 && m_sb.sb_logblocks > 0) {
        quarantinedRanges.push_back({FsbToBlock(m_sb.sb_logstart), m_sb.sb_logblocks});
    }

    // Helper lambda to test if a block falls within any quarantined range
    auto isQuarantined = [&](uint64_t blk) {
        for (const auto& range : quarantinedRanges) {
            if (blk >= range.first && blk < (range.first + range.second)) {
                return true;
            }
        }
        return false;
    };

    // 3. Carpet-bomb all non-quarantined blocks with 0x00 via SecureEraseSectors
    std::cout << "[Erasure] Overwriting user data blocks with zeros...\n";
    uint64_t totalBlocks = m_sb.sb_dblocks;
    uint64_t wipedCount = 0;

    for (uint64_t blk = 0; blk < totalBlocks; ++blk) {
        if (isQuarantined(blk)) continue;

        uint64_t sector = BlockToSector(blk);
        m_hardware->SecureEraseSectors(sector, m_sectorsPerBlock);
        wipedCount++;

        if (wipedCount % 2000 == 0) {
            std::cout << "  -> Wiped " << wipedCount << " blocks...\r";
            std::cout.flush();
        }
    }

    std::cout << "\n[Erasure] Successfully wiped " << wipedCount << " user blocks!\n";

    // 4. Scrub Root Directory metadata entries
    std::cout << "[System] Scrubbing root directory metadata...\n";
    if (rootCore.di_format == XFS::XFS_DINODE_FMT_LOCAL) {
        uint64_t byteOffset = InoToByteOffset(m_rootIno);
        uint64_t startSector = byteOffset / m_bytesPerSector;
        uint32_t offsetInSector = static_cast<uint32_t>(byteOffset % m_bytesPerSector);
        uint32_t sectorsToRead = (offsetInSector + m_inodeSize + m_bytesPerSector - 1) / m_bytesPerSector;

        std::vector<uint8_t> buffer(sectorsToRead * m_bytesPerSector);
        if (m_hardware->ReadSectors(startSector, sectorsToRead, buffer.data())) {
            uint8_t* inodePtr = buffer.data() + offsetInSector;
            int8_t version = static_cast<int8_t>(inodePtr[4]);
            size_t coreSize = (version == 3) ? XFS::XFS_DINODE_CORE_SIZE_V3 : XFS::XFS_DINODE_CORE_SIZE_V2;

            // Reset shortform directory count to 0 and zero entries
            XFS::XfsDir2SfHdr* sfHdr = reinterpret_cast<XFS::XfsDir2SfHdr*>(inodePtr + coreSize);
            sfHdr->count = 0;
            sfHdr->i8count = 0;
            std::memset(sfHdr->parent, 0, 8);

            // Zero out remaining data fork bytes
            size_t maxFork = (m_inodeSize > coreSize) ? (m_inodeSize - coreSize) : 0;
            if (maxFork > sizeof(XFS::XfsDir2SfHdr)) {
                std::memset(inodePtr + coreSize + sizeof(XFS::XfsDir2SfHdr), 0, maxFork - sizeof(XFS::XfsDir2SfHdr));
            }

            // Reset directory size in inode
            *reinterpret_cast<uint64_t*>(inodePtr + 56) = XFS::cpu_to_be64(sizeof(XFS::XfsDir2SfHdr));

            m_hardware->WriteSectors(startSector, sectorsToRead, buffer.data());
        }
    } else if (rootCore.di_format == XFS::XFS_DINODE_FMT_EXTENTS) {
        // Root directory has external block(s): scrub each block's entries and tail
        std::vector<uint64_t> btreeBlocks;
        std::vector<XFS::XfsBmbtRec::ExtentInfo> rootExtents = GetInodeExtents(rootCore, rootFork, btreeBlocks);
        std::vector<uint8_t> blockBuf(m_blockSize);
        for (const auto& ext : rootExtents) {
            for (uint32_t b = 0; b < ext.blockcount; ++b) {
                uint64_t linearBlock = FsbToBlock(ext.startblock + b);
                if (!ReadBlock(linearBlock, blockBuf.data())) continue;

                uint32_t magic = XFS::be32_to_cpu(*reinterpret_cast<const uint32_t*>(blockBuf.data()));
                size_t hdrSize = (magic == XFS::XFS_DIR3_BLOCK_MAGIC || magic == XFS::XFS_DIR3_DATA_MAGIC) ? 64 : 16;
                size_t tailSize = sizeof(XFS::XfsDir2BlockTail);

                // Zero out all entries between header and tail
                if (m_blockSize > hdrSize + tailSize) {
                    size_t dataSpace = m_blockSize - hdrSize - tailSize;
                    // Format as a single empty free region
                    *reinterpret_cast<uint16_t*>(&blockBuf[hdrSize]) = 0xFFFF; // freetag
                    *reinterpret_cast<uint16_t*>(&blockBuf[hdrSize + 2]) = XFS::cpu_to_be16(static_cast<uint16_t>(dataSpace));
                    if (dataSpace > 4) {
                        std::memset(&blockBuf[hdrSize + 4], 0, dataSpace - 4);
                    }

                    // Reset block tail: count = 0, stale = 0
                    size_t tailOffset = m_blockSize - tailSize;
                    XFS::XfsDir2BlockTail* tail = reinterpret_cast<XFS::XfsDir2BlockTail*>(&blockBuf[tailOffset]);
                    tail->count = 0;
                    tail->stale = 0;

                    WriteBlock(linearBlock, blockBuf.data());
                }
            }
        }
    }

    // 5. Overwrite the entire Intent Log (Journal) with zeros and re-initialize a clean header
    if (m_sb.sb_logstart > 0 && m_sb.sb_logblocks > 0) {
        std::cout << "[System] Sanitizing Intent Log (Journal) across " << m_sb.sb_logblocks << " blocks...\n";
        uint64_t logStartBlock = FsbToBlock(m_sb.sb_logstart);
        uint64_t startSector = BlockToSector(logStartBlock);
        uint32_t sectorCount = m_sb.sb_logblocks * m_sectorsPerBlock;

        // Obliterate all log blocks with 0x00
        m_hardware->SecureEraseSectors(startSector, sectorCount);

        // Write a clean, empty Log Record Header at the head of the log
        std::vector<uint8_t> cleanLogRecord(m_blockSize, 0);
        XFS::XlogRecHeader* logHdr = reinterpret_cast<XFS::XlogRecHeader*>(cleanLogRecord.data());
        logHdr->h_magicno    = XFS::cpu_to_be32(XFS::XLOG_HEADER_MAGIC_V2);
        logHdr->h_cycle      = XFS::cpu_to_be32(1);
        logHdr->h_version    = XFS::cpu_to_be32(2);
        logHdr->h_len        = 0;
        logHdr->h_num_logops = 0;

        WriteBlock(logStartBlock, cleanLogRecord.data());
        std::cout << "  -> [Journal] Intent Log completely zeroed and initialized with a clean header.\n";
    }

    std::cout << "=== XFS SURGICAL VOLUME WIPE SECURELY COMPLETED! ===\n";
    return true;
}

// =============================================================================
// Diagnostics & Inspection Helpers
// =============================================================================

void XfsDriver::PrintSuperblockInfo() const {
    if (m_blockSize == 0) {
        std::cout << "XFS filesystem is not mounted.\n";
        return;
    }

    std::cout << "\n=== XFS Superblock Information ===\n";
    std::cout << "Block Size:          " << m_blockSize << " bytes\n";
    std::cout << "Sector Size:         " << m_bytesPerSector << " bytes\n";
    std::cout << "Sectors per Block:   " << m_sectorsPerBlock << "\n";
    std::cout << "Total Data Blocks:   " << m_sb.sb_dblocks << " ("
              << (m_sb.sb_dblocks * m_blockSize / (1024 * 1024)) << " MB)\n";
    std::cout << "Allocation Groups:   " << m_agCount << "\n";
    std::cout << "Blocks per AG:       " << m_agBlocks << "\n";
    std::cout << "AG Block Log:        " << static_cast<int>(m_agBlkLog) << "\n";
    std::cout << "Inode Size:          " << m_inodeSize << " bytes\n";
    std::cout << "Inodes per Block:    " << m_inodesPerBlock << "\n";
    std::cout << "Root Inode:          " << m_rootIno << "\n";
    std::cout << "FTYPE feature:       " << (m_hasFtype ? "Enabled" : "Disabled") << "\n";
    std::cout << "==================================\n";
}

} // namespace FileSystems
} // namespace Erasure
