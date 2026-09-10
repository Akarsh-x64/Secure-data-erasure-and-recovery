#pragma once

#include "../../Core/IFileSystemDriver.h"
#include "../../Core/IHardwareController.h"
#include "XFS_Structures.h"
#include <vector>
#include <string>

namespace Erasure {
namespace FileSystems {

/**
 * @brief High-performance, forensic-grade Secure Deletion Driver for the XFS Filesystem.
 *
 * Adheres strictly to the project's 3-Layer Decoupled Architecture:
 *   [Layer 1: OS / IStorageDevice] -> [Layer 2: Hardware / IHardwareController] -> [Layer 3: Filesystem / XfsDriver]
 *
 * Responsibilities:
 *   1. Parse XFS on-disk metadata (Superblock, Inodes, Extents, Directories, B+Trees).
 *   2. Support both XFS v4 and XFS v5 (CRC) filesystems, handling Big-Endian byte ordering.
 *   3. Translate file paths through directory trees (both Shortform and Extent/Block formats).
 *   4. Command the hardware controller to securely zero-fill every allocated data block.
 *   5. Recursively discover and zero-fill all indirect B+Tree metadata blocks on disk.
 *   6. Completely overwrite on-disk inode structures (`WipeInodeOnDisk`) with zeros.
 *   7. Sanitize parent directory records so filenames are permanently unrecoverable.
 *   8. Provide a surgical volume-wide wipe (`WipeVolume`) that quarantines filesystem
 *      headers (Superblocks, AGF, AGI, AGFL, root inode) while obliterating all user data.
 */
class XfsDriver : public Core::IFileSystemDriver {
private:
    // =========================================================================
    // Core Dependencies & Cached Filesystem Geometry
    // =========================================================================

    // Pointer to the hardware demolition layer (e.g. HDDController).
    // All raw disk I/O and sector-level overwrites are routed exclusively through this interface.
    Core::IHardwareController* m_hardware;

    // In-memory unpacked copy of the primary Superblock (located at sector 0 of AG 0)
    XFS::XfsSuperblock m_sb;

    // Fundamental filesystem parameters decoded from the Superblock
    uint32_t m_blockSize;        // Filesystem block size in bytes (e.g., 4096)
    uint32_t m_sectorsPerBlock;  // Number of underlying disk sectors per filesystem block (e.g., 8)
    uint32_t m_bytesPerSector;   // Physical disk sector size in bytes (e.g., 512)
    uint16_t m_inodeSize;        // Size of an on-disk inode in bytes (typically 256 or 512)
    uint16_t m_inodesPerBlock;   // Count of inodes packed into a single block (m_blockSize / m_inodeSize)

    // Allocation Group parameters
    uint32_t m_agBlocks;         // Size of an Allocation Group in filesystem blocks
    uint32_t m_agCount;          // Total count of Allocation Groups across the disk
    uint8_t  m_agBlkLog;         // ceil(log2(sb_agblocks)) - bitshift for AG block addressing
    uint8_t  m_inopbLog;         // log2(sb_inopblock) - bitshift for inode indexing inside a block

    // Root directory anchor
    uint64_t m_rootIno;          // 64-bit absolute inode number of the root directory ("/")

    // Feature flags
    bool     m_hasFtype;         // True if directory entries store the 1-byte file type field

    // =========================================================================
    // Physical & Logical Geometry Conversions
    // =========================================================================

    /**
     * @brief Converts an XFS Filesystem Block Number (fsbno) into a linear disk block index.
     *
     * In XFS, an fsbno bit-packs the Allocation Group number into its high bits:
     *   agno  = fsbno >> m_agBlkLog
     *   agbno = fsbno & ((1 << m_agBlkLog) - 1)
     * Because AG sizes are rarely exact powers of 2, this function unpacks the AG number
     * and relative offset, returning: (agno * m_agBlocks) + agbno.
     *
     * @param fsbno The 64-bit XFS filesystem block number from an extent record or B+Tree pointer.
     * @return uint64_t Linear block index from 0 to total disk blocks.
     */
    uint64_t FsbToBlock(uint64_t fsbno) const;

    /**
     * @brief Converts a linear filesystem block index into an absolute LBA sector number.
     *
     * @param block The linear disk block index.
     * @return uint64_t LBA sector number suitable for passing to IHardwareController.
     */
    uint64_t BlockToSector(uint64_t block) const;

    /**
     * @brief Reads a single filesystem block from disk into a destination buffer.
     */
    bool ReadBlock(uint64_t block, void* buffer) const;

    /**
     * @brief Writes a single filesystem block from a source buffer onto disk.
     */
    bool WriteBlock(uint64_t block, const void* buffer);

    // =========================================================================
    // Inode Addressing & Metadata Manipulation
    // =========================================================================

    /**
     * @brief Calculates the exact physical byte offset of any 64-bit inode number on disk.
     *
     * Uses the standard XFS mathematical formula:
     *   agno           = ino >> (m_inopbLog + m_agBlkLog)
     *   agbno          = (ino >> m_inopbLog) & ((1 << m_agBlkLog) - 1)
     *   offsetInBlock  = (ino & ((1 << m_inopbLog) - 1)) * m_inodeSize
     *   byteOffset     = (((agno * m_agBlocks) + agbno) * m_blockSize) + offsetInBlock
     */
    uint64_t InoToByteOffset(uint64_t ino) const;

    /**
     * @brief Reads an on-disk inode, unpacks its core header, and extracts its data fork.
     *
     * @param ino The 64-bit inode number to read.
     * @param outCore Destination structure for decoded core fields.
     * @param outFork Destination vector for raw data fork bytes (extents, shortform dir, or B+Tree root).
     * @return bool True if successfully read and verified.
     */
    bool ReadInode(uint64_t ino, XFS::XfsDinodeCore& outCore, std::vector<uint8_t>& outFork) const;

    /**
     * @brief Permanently obliterates an on-disk inode by zeroing out its entire sector slice.
     *
     * Overwrites all inode fields: file mode, size, timestamps, block counts, and data fork.
     *
     * @param ino The 64-bit inode number to wipe.
     * @return bool True if the physical sector write succeeded.
     */
    bool WipeInodeOnDisk(uint64_t ino);

    /**
     * @brief Scrubs the unlinked inode hash array (agi_unlinked[64]) in Sector 2 of the AG.
     *
     * If this inode was tracked in an unlinked bucket, resets the bucket to XFS_AGI_UNLINKED_NULL
     * so recovery tools cannot traverse the unlinked list.
     *
     * @param ino The 64-bit inode number to purge from the AGI unlinked hash buckets.
     * @return bool True if successfully verified and updated on disk.
     */
    bool ScrubAgiUnlinkedBucket(uint64_t ino);

    /**
     * @brief Scans the Intent Log (Journal) and zeroes out transaction items referencing an inode or filename.
     *
     * In XFS, recent metadata modifications (inode states, historical extents, directory adds)
     * are logged in the circular log buffer. This function purges matching log records so
     * that log analysis tools (like xfs_logprint) find zero trace of the wiped file.
     *
     * @param ino The 64-bit inode number to purge from log blocks.
     * @param filename The name of the file to purge from directory transaction records.
     * @return bool True if journal was successfully scanned and sanitized.
     */
    bool ScrubJournalForInode(uint64_t ino, const std::string& filename);

    // =========================================================================
    // Extent & B+Tree Traversal Helpers
    // =========================================================================

    /**
     * @brief Resolves all physical extents allocated to a file across the volume.
     *
     * Handles both XFS_DINODE_FMT_EXTENTS (direct array) and XFS_DINODE_FMT_BTREE (multi-level tree).
     *
     * @param core Decoded inode core header.
     * @param forkData Raw bytes of the inode's data fork.
     * @param outBtreeBlocks Populated with the fsbno of every B+Tree metadata block traversed.
     * @return std::vector<XFS::XfsBmbtRec::ExtentInfo> List of all unpacked physical extents.
     */
    std::vector<XFS::XfsBmbtRec::ExtentInfo> GetInodeExtents(
        const XFS::XfsDinodeCore& core,
        const std::vector<uint8_t>& forkData,
        std::vector<uint64_t>& outBtreeBlocks) const;

    /**
     * @brief Recursively traverses indirect on-disk B+Tree blocks to extract all leaf extents.
     *
     * Also records every B+Tree block fsbno so that these metadata blocks can be zeroed.
     *
     * @param btreeFsb Filesystem block number of the B+Tree node or leaf block.
     * @param outExtents Accumulator vector for discovered file extents.
     * @param outBtreeBlocks Accumulator vector for B+Tree metadata block locations.
     */
    void CollectBtreeExtents(
        uint64_t btreeFsb,
        std::vector<XFS::XfsBmbtRec::ExtentInfo>& outExtents,
        std::vector<uint64_t>& outBtreeBlocks) const;

    // =========================================================================
    // Directory Parsing & Scrubbing
    // =========================================================================

    /**
     * @brief Tokenizes a file path string (e.g. "documents/finance/salary.xlsx") into path tokens.
     */
    std::vector<std::string> TokenizePath(const std::string& path) const;

    /**
     * @brief Descriptor for locating a target directory entry on disk.
     */
    struct DirectorySearchResult {
        bool found;                  // True if entry matching the target name was found
        bool isDirectory;            // True if the resolved target inode is a directory
        uint64_t inodeNum;           // Target inode number referenced by the entry
        bool isShortform;            // True if entry resides inside an inode's shortform data fork
        uint64_t dirBlock;           // Linear disk block number (if block/extent directory)
        size_t entryOffset;          // Byte offset of the entry within the block or data fork
        size_t entryLength;          // Total byte length of the entry
    };

    /**
     * @brief Searches a directory inode for a specific child file or directory entry name.
     *
     * Supports both Shortform (di_format == 1) and Block/Extent (di_format == 2/3) directories.
     *
     * @param dirIno Inode number of the directory to search.
     * @param targetName Filename to locate.
     * @return DirectorySearchResult Result containing location and metadata needed for wiping.
     */
    DirectorySearchResult FindEntryInDirectory(
        uint64_t dirIno,
        const std::string& targetName) const;

    /**
     * @brief Scrubs a directory entry from the parent directory block or shortform fork.
     *
     * For shortform directories: zeroes the entry bytes in the parent inode data fork.
     * For block directories: sets the freetag (0xFFFF), updates free length, and zeroes the name.
     *
     * @param parentDirIno Inode number of the parent directory.
     * @param entry The search result describing where the entry is located on disk.
     * @return bool True if parent directory metadata was successfully flushed to disk.
     */
    bool WipeDirectoryEntry(
        uint64_t parentDirIno,
        const DirectorySearchResult& entry);

    /**
     * @brief Lists all entries in a directory inode (excluding "." and "..").
     *
     * @param dirIno Inode number of the directory to enumerate.
     * @param outEntries Vector of pairs (entry name, child inode number).
     * @param outIsDir Vector of booleans indicating if each child is a directory.
     * @return bool True if directory contents were successfully parsed.
     */
    bool ListDirectoryContents(
        uint64_t dirIno,
        std::vector<std::pair<std::string, uint64_t>>& outEntries,
        std::vector<bool>& outIsDir) const;

    /**
     * @brief Recursively erases all children of a directory inode.
     *
     * @param dirIno Inode number of the directory.
     * @return bool True if all contents were eradicated.
     */
    bool EraseDirectoryRecursive(uint64_t dirIno);

public:
    /**
     * @brief Constructs the XfsDriver with a pointer to the hardware demolition layer.
     *
     * @param hardware Demolition controller providing SecureEraseSectors, ReadSectors, and WriteSectors.
     */
    explicit XfsDriver(Core::IHardwareController* hardware);
    ~XfsDriver() override = default;

    /**
     * @brief Mounts the XFS filesystem by reading and parsing Sector 0 (Primary Superblock).
     *
     * Validates the 0x58465342 ("XFSB") magic signature, extracts block and sector sizes,
     * calculates Allocation Group bitshifts, and verifies drive geometry.
     *
     * @return bool True if valid XFS filesystem was successfully recognized and mounted.
     */
    bool Mount() override;

    /**
     * @brief Surgically destroys a single file specified by its relative path.
     *
     * Complete Erasure Process:
     *   1. Traverses the directory tree from root inode to locate the file.
     *   2. Resolves all physical extents allocated to the file data.
     *   3. Overwrites all data blocks with 0x00 via hardware SecureEraseSectors.
     *   4. Overwrites all indirect B+Tree metadata blocks with 0x00 if file was fragmented.
     *   5. Overwrites the file's on-disk Inode structure with zeros (WipeInodeOnDisk).
     *   6. Scrubs the file entry from the parent directory block on disk (WipeDirectoryEntry).
     *
     * @param relativePath Path relative to filesystem root (e.g., "secret.txt" or "dir/secret.pdf").
     * @return bool True if data and all traces of metadata were permanently eradicated.
     */
    bool EraseFile(const std::string& relativePath) override;

    /**
     * @brief Recursively destroys an entire folder and all nested files and subdirectories.
     *
     * @param relativePath Relative path to directory (e.g., "docs" or "docs/finance").
     * @return bool True if directory and all nested contents were permanently obliterated.
     */
    bool EraseDirectory(const std::string& relativePath);

    /**
     * @brief Performs a surgical volume-wide wipe of all user data.
     *
     * Quarantines critical filesystem structures (Superblocks, AGF, AGI, AGFL across all AGs,
     * and root directory inode), then carpet-bombs all remaining user blocks with 0x00
     * and resets directory tables.
     *
     * @return bool True if volume was completely sanitized while maintaining mountability.
     */
    bool WipeVolume() override;

    // =========================================================================
    // Diagnostics & Inspection Helpers
    // =========================================================================

    /**
     * @brief Prints a formatted summary of the mounted XFS superblock to stdout.
     */
    void PrintSuperblockInfo() const;

    const XFS::XfsSuperblock& GetSuperblock() const { return m_sb; }
    uint32_t GetBlockSize() const { return m_blockSize; }
    uint32_t GetAgCount() const { return m_agCount; }
    uint64_t GetRootIno() const { return m_rootIno; }
};

} // namespace FileSystems
} // namespace Erasure
