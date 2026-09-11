#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

#pragma pack(push, 1) // Strictly pack all on-disk structures (1-byte alignment) to match exact raw disk layout

namespace Erasure {
namespace FileSystems {
namespace XFS {

    // =========================================================================
    // Endianness Helpers (XFS On-Disk Format is strictly Big-Endian)
    // =========================================================================
    // On x86 / x86_64 host CPUs (Little-Endian), any multi-byte integer read
    // directly from an XFS disk image will have reversed byte order.
    // The helpers below convert between Big-Endian and host CPU byte order
    // using compiler intrinsics for maximum performance and zero overhead.

    /**
     * @brief Converts a 16-bit unsigned integer from Big-Endian to CPU host order.
     */
    inline uint16_t be16_to_cpu(uint16_t v) {
#if defined(_MSC_VER)
        return _byteswap_ushort(v);
#elif defined(__GNUC__) || defined(__clang__)
        return __builtin_bswap16(v);
#else
        return (v >> 8) | (v << 8);
#endif
    }

    /**
     * @brief Converts a 32-bit unsigned integer from Big-Endian to CPU host order.
     */
    inline uint32_t be32_to_cpu(uint32_t v) {
#if defined(_MSC_VER)
        return _byteswap_ulong(v);
#elif defined(__GNUC__) || defined(__clang__)
        return __builtin_bswap32(v);
#else
        return ((v >> 24) & 0x000000FF) |
               ((v >> 8)  & 0x0000FF00) |
               ((v << 8)  & 0x00FF0000) |
               ((v << 24) & 0xFF000000);
#endif
    }

    /**
     * @brief Converts a 64-bit unsigned integer from Big-Endian to CPU host order.
     */
    inline uint64_t be64_to_cpu(uint64_t v) {
#if defined(_MSC_VER)
        return _byteswap_uint64(v);
#elif defined(__GNUC__) || defined(__clang__)
        return __builtin_bswap64(v);
#else
        return (((v >> 56) & 0x00000000000000FFULL)) |
               (((v >> 40) & 0x000000000000FF00ULL)) |
               (((v >> 24) & 0x0000000000FF0000ULL)) |
               (((v >> 8)  & 0x00000000FF000000ULL)) |
               (((v << 8)  & 0x000000FF00000000ULL)) |
               (((v << 24) & 0x0000FF0000000000ULL)) |
               (((v << 40) & 0x00FF000000000000ULL)) |
               (((v << 56) & 0xFF00000000000000ULL));
#endif
    }

    // Symmetric functions for writing CPU host values back to on-disk Big-Endian format
    inline uint16_t cpu_to_be16(uint16_t v) { return be16_to_cpu(v); }
    inline uint32_t cpu_to_be32(uint32_t v) { return be32_to_cpu(v); }
    inline uint64_t cpu_to_be64(uint64_t v) { return be64_to_cpu(v); }

    // =========================================================================
    // Core Magic Numbers & Identifying Signatures
    // =========================================================================

    constexpr uint32_t XFS_SB_MAGIC       = 0x58465342; // ASCII "XFSB" (Superblock)
    constexpr uint32_t XFS_AGF_MAGIC      = 0x58414746; // ASCII "XAGF" (Allocation Group Free Space)
    constexpr uint32_t XFS_AGI_MAGIC      = 0x58414749; // ASCII "XAGI" (Allocation Group Inode Header)
    constexpr uint32_t XFS_AGFL_MAGIC     = 0x5841464C; // ASCII "XAFL" (Allocation Group Free List)
    constexpr uint16_t XFS_DINODE_MAGIC   = 0x494E;     // ASCII "IN"   (On-Disk Inode)

    // Unlinked Inode Null Marker in AGI Hash Table
    constexpr uint32_t XFS_AGI_UNLINKED_NULL = 0xFFFFFFFF; // Marks an empty or cleared unlinked hash bucket

    // Directory Block Signatures
    constexpr uint32_t XFS_DIR2_BLOCK_MAGIC = 0x58443242; // ASCII "XD2B" (v4 Single Block Directory)
    constexpr uint32_t XFS_DIR3_BLOCK_MAGIC = 0x58444233; // ASCII "XDB3" (v5 Single Block Directory with CRC)
    constexpr uint32_t XFS_DIR2_DATA_MAGIC  = 0x58443244; // ASCII "XD2D" (v4 Multi-Block Directory Data)
    constexpr uint32_t XFS_DIR3_DATA_MAGIC  = 0x58444433; // ASCII "XDD3" (v5 Multi-Block Directory Data with CRC)

    // B+Tree Block Signatures (BMBT = Block Map B+Tree)
    constexpr uint32_t XFS_BMAP_MAGIC       = 0x424D4150; // ASCII "BMAP" (v4 BMBT block)
    constexpr uint32_t XFS_BMAP_CRC_MAGIC   = 0x424D4133; // ASCII "BMA3" (v5 BMBT block with CRC)

    // Intent Log (Journal) Signatures & Constants
    constexpr uint32_t XLOG_HEADER_MAGIC_V1 = 0xFEEDbabe; // Journal Log Record Header v1
    constexpr uint32_t XLOG_HEADER_MAGIC_V2 = 0xFEED2882; // Journal Log Record Header v2 (standard)
    constexpr uint16_t XFS_LI_INODE         = 0x123B;     // Inode Log Item Format Type

    // Inode Data Fork Formats (di_format in XfsDinodeCore)
    constexpr uint8_t XFS_DINODE_FMT_DEV     = 0; // Device files (major/minor numbers)
    constexpr uint8_t XFS_DINODE_FMT_LOCAL   = 1; // Inline shortform data or shortform directory
    constexpr uint8_t XFS_DINODE_FMT_EXTENTS = 2; // Linear array of extent records in inode data fork
    constexpr uint8_t XFS_DINODE_FMT_BTREE   = 3; // B+Tree root (escalated for fragmented files)
    constexpr uint8_t XFS_DINODE_FMT_UUID    = 4; // UUID mapping format
    constexpr uint8_t XFS_DINODE_FMT_RMAP    = 5; // Reverse mapping format

    // File type bitmasks in di_mode (standard POSIX permissions & type bits)
    constexpr uint16_t XFS_S_IFMT   = 0xF000; // Type mask
    constexpr uint16_t XFS_S_IFREG  = 0x8000; // Regular file
    constexpr uint16_t XFS_S_IFDIR  = 0x4000; // Directory
    constexpr uint16_t XFS_S_IFLNK  = 0xA000; // Symbolic link

    // Version flags stored in sb_versionnum
    constexpr uint16_t XFS_SB_VERSION_NUMBITS   = 0x000F; // Version mask (v4 or v5)
    constexpr uint16_t XFS_SB_VERSION_4         = 4;      // Standard XFS v4
    constexpr uint16_t XFS_SB_VERSION_5         = 5;      // Modern XFS v5 (with metadata CRCs)
    constexpr uint16_t XFS_SB_VERSION_DIRV2BIT  = 0x2000; // Directory v2 format supported
    constexpr uint16_t XFS_SB_VERSION_MOREBITSBIT = 0x4000; // Secondary features enabled (features2)

    // Feature 2 flags stored in sb_features2
    constexpr uint32_t XFS_SB_VERSION2_FTYPE    = 0x00000200; // Directory entries store file type byte

    // Inode Core sizes
    constexpr size_t XFS_DINODE_CORE_SIZE_V2 = 100; // 100 bytes for v1 and v2 inodes (v4 filesystems)
    constexpr size_t XFS_DINODE_CORE_SIZE_V3 = 176; // 176 bytes for v3 inodes (v5 filesystems)

    // =========================================================================
    // XFS Superblock Structure (xfs_sb)
    // =========================================================================
    // Located at byte offset 0 of Allocation Group 0 (and backed up at the start
    // of each subsequent Allocation Group). Contains all primary parameters.

    struct XfsSuperblock {
        uint32_t sb_magicnum;       // 0x58465342 ("XFSB") - Signature identifying XFS
        uint32_t sb_blocksize;      // Fundamental filesystem block size in bytes (e.g., 4096)
        uint64_t sb_dblocks;        // Total data blocks allocated across the volume
        uint64_t sb_rblocks;        // Total realtime volume blocks
        uint64_t sb_rextents;       // Total realtime volume extents
        uint8_t  sb_uuid[16];       // Universally Unique Identifier for this filesystem
        uint64_t sb_logstart;       // First filesystem block of the journal log
        uint64_t sb_rootino;        // Absolute 64-bit inode number of the root directory ("/")
        uint64_t sb_rbmino;         // Inode number of the realtime bitmap
        uint64_t sb_rsumino;        // Inode number of the realtime summary
        uint32_t sb_rextsize;       // Realtime extent size in blocks
        uint32_t sb_agblocks;       // Size of each Allocation Group in filesystem blocks
        uint32_t sb_agcount;        // Total number of Allocation Groups spanning the disk
        uint32_t sb_rbmblocks;      // Number of blocks in realtime bitmap
        uint32_t sb_logblocks;      // Total blocks allocated to the journal log
        uint16_t sb_versionnum;     // Core version number and primary feature flags
        uint16_t sb_sectsize;       // Underlying hardware disk sector size (e.g., 512 or 4096)
        uint16_t sb_inodesize;      // Size of an on-disk inode in bytes (standard: 256 or 512)
        uint16_t sb_inopblock;      // Number of inodes packed in a single block (sb_blocksize / sb_inodesize)
        char     sb_fname[12];      // Human-readable filesystem name/label
        uint8_t  sb_blocklog;       // log2(sb_blocksize) - e.g. 12 for 4096-byte blocks
        uint8_t  sb_sectlog;        // log2(sb_sectsize) - e.g. 9 for 512-byte sectors
        uint8_t  sb_inodelog;       // log2(sb_inodesize) - e.g. 8 for 256-byte inodes
        uint8_t  sb_inopblog;       // log2(sb_inopblock) - e.g. 4 for 16 inodes per block
        uint8_t  sb_agblklog;       // ceil(log2(sb_agblocks)) - bitshift for AG block addressing
        uint8_t  sb_rextslog;       // log2(sb_rextents)
        uint8_t  sb_inprogress;     // Flag indicating mkfs was interrupted
        uint8_t  sb_imax_pct;       // Maximum percentage of disk space allowed for inodes
        uint64_t sb_icount;         // Total currently allocated inodes across the volume
        uint64_t sb_ifree;          // Total unallocated/free inodes available
        uint64_t sb_fdblocks;       // Total free data blocks available for allocation
        uint64_t sb_frextents;      // Total free realtime extents
        uint64_t sb_uquotino;       // User quota inode number
        uint64_t sb_gquotino;       // Group quota inode number
        uint16_t sb_qflags;         // Quota subsystem flags
        uint8_t  sb_flags;          // Miscellaneous filesystem flags
        uint8_t  sb_shared_vn;      // Shared version number
        uint32_t sb_inoalignmt;     // Inode chunk alignment in filesystem blocks
        uint32_t sb_unit;           // Storage stripe unit in filesystem blocks
        uint32_t sb_width;          // Storage stripe width in filesystem blocks
        uint8_t  sb_dirblklog;      // Directory block factor: dirblksize = sb_blocksize << dirblklog
        uint8_t  sb_logsectlog;     // log2 of journal log sector size
        uint16_t sb_logsectsize;    // Journal log sector size in bytes
        uint32_t sb_logsunit;       // Journal log stripe unit
        uint32_t sb_features2;      // Secondary feature bitmask (e.g., XFS_SB_VERSION2_FTYPE)
        uint32_t sb_bad_features2;  // Mirror copy of features2 for backwards compatibility
        // Modern XFS v5 specific fields (CRC & metadata protection)
        uint32_t sb_features_compat;
        uint32_t sb_features_ro_compat;
        uint32_t sb_features_incompat;
        uint32_t sb_features_log_incompat;
        uint32_t sb_crc;            // CRC32c checksum of the superblock
        uint64_t sb_pquotino;       // Project quota inode number
        uint64_t sb_lsn;            // Log sequence number of last write
        uint8_t  sb_meta_uuid[16];  // Metadata UUID
        uint64_t sb_rsumino2;
    };

    // =========================================================================
    // Allocation Group Free Space Header (xfs_agf)
    // =========================================================================
    // Located at Sector 1 of every Allocation Group. Tracks free data blocks
    // and the B+Tree roots indexing free block extents.

    struct XfsAgf {
        uint32_t agf_magicnum;      // 0x58414746 ("XAGF")
        uint32_t agf_versionnum;    // Header version (typically 1)
        uint32_t agf_seqno;         // Sequence number of this Allocation Group (0, 1, ...)
        uint32_t agf_length;        // Total size of this Allocation Group in blocks
        uint32_t agf_roots[2];      // Roots of free space B+Trees: [0] by Block Number, [1] by Extent Length
        uint32_t agf_spare0;
        uint32_t agf_levels[2];     // Depths of free space B+Trees: [0] by Block Number, [1] by Extent Length
        uint32_t agf_spare1;
        uint32_t agf_flfirst;       // First index in the Allocation Group Free List (AGFL)
        uint32_t agf_fllast;        // Last index in the AGFL
        uint32_t agf_flcount;       // Count of entries currently stored in the AGFL
        uint32_t agf_freeblks;      // Total count of free blocks available in this AG
        uint32_t agf_longest;       // Size in blocks of the single longest free contiguous extent
        uint32_t agf_btreeblks;     // Total blocks consumed by the free space B+Trees
    };

    // =========================================================================
    // Allocation Group Inode Header (xfs_agi)
    // =========================================================================
    // Located at Sector 2 of every Allocation Group. Tracks inode allocations
    // and the B+Tree roots indexing allocated 64-inode chunks.

    struct XfsAgi {
        uint32_t agi_magicnum;      // 0x58414749 ("XAGI")
        uint32_t agi_versionnum;    // Header version (typically 1)
        uint32_t agi_seqno;         // Sequence number of this Allocation Group (0, 1, ...)
        uint32_t agi_length;        // Total size of this Allocation Group in blocks
        uint32_t agi_count;         // Total inodes allocated within this AG
        uint32_t agi_root;          // Root block of the Inode Allocation B+Tree (inobt)
        uint32_t agi_level;         // Depth of the Inode Allocation B+Tree
        uint32_t agi_freecount;     // Count of free inodes within the allocated chunks
        uint32_t agi_newino;        // Starting block of the most recently allocated inode chunk
        uint32_t agi_dirino;        // Inode number of the most recently allocated directory
        uint32_t agi_unlinked[64];  // 64 hash buckets linking inodes unlinked but still held open
    };

    // =========================================================================
    // High-Resolution Timestamp Structure
    // =========================================================================

    struct XfsTimestamp {
        int32_t t_sec;              // Seconds since Unix Epoch (Jan 1, 1970)
        int32_t t_nsec;             // Nanosecond fraction (0 .. 999,999,999)
    };

    // =========================================================================
    // XFS On-Disk Inode Structure (xfs_dinode core)
    // =========================================================================
    // Represents the static header of an on-disk inode.
    // The Data Fork begins immediately following this core structure:
    //   - For v1/v2 inodes (v4 filesystems): starts at offset 100
    //   - For v3 inodes (v5 filesystems): starts at offset 176

    struct XfsDinodeCore {
        uint16_t     di_magic;       // 0x494E ("IN") - Inode magic signature
        uint16_t     di_mode;        // POSIX file type (directory, regular file) and permissions
        int8_t       di_version;     // Inode layout version: 1, 2, or 3
        int8_t       di_format;      // Data fork layout: 1=LOCAL, 2=EXTENTS, 3=BTREE
        uint16_t     di_onlink;      // Legacy link count (used in v1 inodes)
        uint32_t     di_uid;         // Owner User ID
        uint32_t     di_gid;         // Group ID
        uint32_t     di_nlink;       // Number of hard links pointing to this inode
        uint16_t     di_projid_lo;   // Low 16 bits of Project ID
        uint16_t     di_projid_hi;   // High 16 bits of Project ID
        uint8_t      di_pad[6];      // Padding to align to 32-bit boundary
        uint16_t     di_flushiter;   // Incremented on every flush to disk
        XfsTimestamp di_atime;       // Last access time
        XfsTimestamp di_mtime;       // Last modification time
        XfsTimestamp di_ctime;       // Last status/metadata change time
        int64_t      di_size;        // Logical file size in bytes
        int64_t      di_nblocks;     // Count of 512-byte blocks allocated (data + B+Tree metadata)
        uint32_t     di_extsize;     // Extent size hint for allocation
        uint32_t     di_nextents;    // Count of extents stored in data fork
        uint16_t     di_anextents;   // Count of extents stored in attribute fork
        uint8_t      di_forkoff;     // Offset of attribute fork from data fork in 8-byte units
        int8_t       di_aformat;     // Format of attribute fork
        uint32_t     di_dmevmask;    // DMAPI event mask
        uint16_t     di_dmstate;     // DMAPI state
        uint16_t     di_flags;       // Inode behavior flags (e.g. realtime, append-only)
        uint32_t     di_gen;         // Generation number (prevents stale NFS handles)

        // Fields below are present only in v3 (v5 filesystems) inodes:
        uint32_t     di_next_unlinked; // Inode number of next unlinked inode in hash chain
        uint32_t     di_crc;           // CRC32c checksum of the inode
        uint64_t     di_changecount;   // Monotonically increasing change counter
        uint64_t     di_lsn;           // Log Sequence Number of last metadata transaction
        uint64_t     di_flags2;        // Extended inode flags (CoW, DAX, etc.)
        uint32_t     di_cowextsize;    // Copy-on-write extent size hint
        uint8_t      di_pad2[12];      // Reserved padding
        XfsTimestamp di_crtime;        // File creation timestamp
        uint64_t     di_ino;           // Absolute 64-bit inode number of this inode
        uint8_t      di_uuid[16];      // Filesystem UUID copy to detect cross-volume leaks
    };

    // =========================================================================
    // XFS 128-Bit Packed Extent Record (xfs_bmbt_rec)
    // =========================================================================
    // Each extent record is exactly 16 bytes (128 bits), packed Big-Endian:
    //   - bit 127:      State flag (0 = written extent, 1 = unwritten extent)
    //   - bits 126..73: Logical file block offset (54 bits)
    //   - bits 72..21:  Physical filesystem block number (52 bits)
    //   - bits 20..0:   Extent length in blocks (21 bits, up to 2,097,151 blocks)

    struct XfsBmbtRec {
        uint64_t l0; // High 64 bits (Big-Endian)
        uint64_t l1; // Low 64 bits (Big-Endian)

        // Decoded representation for easy usage in driver logic
        struct ExtentInfo {
            uint8_t  state;       // 0 = standard written, 1 = unwritten preallocated
            uint64_t startoff;    // Logical file block offset (54 bits)
            uint64_t startblock;  // Filesystem block number (52 bits)
            uint32_t blockcount;  // Extent length in blocks (21 bits)
        };

        /**
         * @brief Unpacks the 128-bit big-endian bitfield into native CPU integers.
         */
        ExtentInfo Unpack() const {
            uint64_t w0 = be64_to_cpu(l0);
            uint64_t w1 = be64_to_cpu(l1);

            ExtentInfo info;
            // High bit (bit 63 of word 0) is the unwritten extent state flag
            info.state      = static_cast<uint8_t>((w0 >> 63) & 0x1);
            // Bits 9..62 of word 0 (54 bits) store the logical file offset
            info.startoff   = (w0 >> 9) & ((1ULL << 54) - 1);
            // Bits 0..8 of word 0 (9 bits) + Bits 21..63 of word 1 (43 bits) store physical block (52 bits)
            info.startblock = ((w0 & 0x1FFULL) << 43) | ((w1 >> 21) & ((1ULL << 43) - 1));
            // Bits 0..20 of word 1 (21 bits) store block count
            info.blockcount = static_cast<uint32_t>(w1 & ((1ULL << 21) - 1));
            return info;
        }
    };

    // =========================================================================
    // B+Tree Root Header in Inode Data Fork (xfs_bmdr_block)
    // =========================================================================
    // When a file fragments beyond what fits in the inode core fork,
    // di_format converts to XFS_DINODE_FMT_BTREE.
    // The inode data fork then stores this root header, followed by keys and pointers.

    struct XfsBmdrBlock {
        uint16_t bb_level;   // Depth of tree from here (0 = points to leaf blocks, >0 = node blocks)
        uint16_t bb_numrecs; // Number of keys/pointers stored in this root
    };

    // =========================================================================
    // On-Disk B+Tree Intermediate / Leaf Block Header (xfs_btree_block)
    // =========================================================================
    // Header for indirect blocks allocated on disk for large or fragmented files.
    // Must be sanitized during surgical file deletion!

    struct XfsBtreeBlock {
        uint32_t bb_magic;   // "BMAP" (0x424D4150) or "BMA3" (0x424D4133)
        uint16_t bb_level;   // 0 = leaf block containing extent records, >0 = node block
        uint16_t bb_numrecs; // Count of records or key/pointer pairs in this block
        uint64_t bb_leftsib; // Sibling fsbno to the left (-1ULL if none)
        uint64_t bb_rightsib;// Sibling fsbno to the right (-1ULL if none)
        // Fields below are present in v5 (CRC) filesystems:
        uint64_t bb_blkno;   // Physical block number where this block is written
        uint64_t bb_lsn;     // Log Sequence Number
        uint8_t  bb_uuid[16];// Filesystem UUID
        uint64_t bb_owner;   // Inode number of the file owning this B+Tree block
        uint32_t bb_crc;     // CRC32c checksum
        uint32_t bb_pad;
    };

    // =========================================================================
    // Shortform Directory Structures (XFS_DINODE_FMT_LOCAL in Directory Inodes)
    // =========================================================================
    // For directories containing few entries, directory entries are stored
    // directly inside the inode's data fork with zero external block allocations.

    struct XfsDir2SfHdr {
        uint8_t count;      // Number of active directory entries in this shortform table
        uint8_t i8count;    // Number of entries using 64-bit inode numbers (if 0, all are 32-bit)
        uint8_t parent[8];  // Inode number of the parent directory ("..") - 4 or 8 bytes
    };

    // =========================================================================
    // Block / Data Directory Structures (XFS_DINODE_FMT_EXTENTS)
    // =========================================================================
    // When a directory outgrows shortform, XFS allocates external data blocks
    // formatted with data headers, entries, and free space tags.

    struct XfsDir2DataHdr {
        uint32_t magic;     // XD2B (v4 single-block), XDB3 (v5 single-block), XD2D (v4 data), XDD3 (v5 data)
        // Best-free table indexing the three largest contiguous free regions in this block
        uint16_t bestfree[6];
    };

    // Unused / free space entry within a directory data block
    struct XfsDir2DataUnused {
        uint16_t freetag;   // Always 0xFFFF, distinguishing free space from an active inode entry
        uint16_t length;    // Byte length of this free region (must be a multiple of 8)
    };

    // Active directory entry within a directory data block
    struct XfsDir2DataEntry {
        uint64_t inumber;   // 64-bit inode number of the referenced file/folder (Big-Endian)
        uint8_t  namelen;   // Length of filename in bytes
        // Followed immediately by:
        //   - char name[namelen];
        //   - uint8_t ftype;  (optional, present if XFS_SB_VERSION2_FTYPE is active)
        //   - uint16_t tag;   (2-byte offset to start of this entry, aligned to 8-byte boundary)
    };

    // =========================================================================
    // Block Directory Tail & Leaf Hash Array (At the end of directory blocks)
    // =========================================================================

    // 8-byte tail structure located at (blockSize - 8) in single-block directories
    struct XfsDir2BlockTail {
        uint32_t count; // Number of leaf hash entries stored backwards from the tail
        uint32_t stale; // Number of stale/deleted leaf entries
    };

    // 8-byte leaf entry mapping a 32-bit name hash to a data offset
    struct XfsDir2LeafEntry {
        uint32_t hashval; // 32-bit hash of the filename
        uint32_t address; // Address in 8-byte units (address * 8 = data offset in block)
    };

    // =========================================================================
    // Intent Log (Journal) Record Header (xlog_rec_header)
    // =========================================================================

    struct XlogRecHeader {
        uint32_t h_magicno;     // 0xFEEDbabe or 0xFEED2882
        uint32_t h_cycle;       // Log cycle number
        uint32_t h_version;     // Header version (1 or 2)
        uint32_t h_len;         // Length of valid data in bytes
        uint64_t h_lsn;         // Log Sequence Number
        uint64_t h_tail_lsn;    // Tail LSN
        uint32_t h_crc;         // CRC32c checksum (v5)
        uint32_t h_prev_block;  // Previous block in log
        uint32_t h_num_logops;  // Number of log operations
        uint32_t h_cycle_data[16];
    };

} // namespace XFS
} // namespace FileSystems
} // namespace Erasure

#pragma pack(pop) // Restore compiler default struct alignment
