#pragma once

#include <cstdint>

#pragma pack(push, 1) // Strictly pack structs to match raw disk layout

namespace Erasure {
namespace FileSystems {
namespace Ext4 {

    // Magic numbers & Constants
    constexpr uint16_t EXT4_SUPER_MAGIC         = 0xEF53;
    constexpr uint16_t EXT4_EXTENT_MAGIC        = 0xF30A;
    constexpr uint32_t EXT4_SUPERBLOCK_OFFSET   = 1024;
    constexpr uint32_t EXT4_ROOT_INO            = 2;
    constexpr uint32_t EXT4_GOOD_OLD_INODE_SIZE = 128;

    // Inode Flags
    constexpr uint32_t EXT4_SECRM_FL            = 0x00000001; // Secure deletion
    constexpr uint32_t EXT4_UNRM_FL             = 0x00000002; // Undelete
    constexpr uint32_t EXT4_EXTENTS_FL          = 0x00080000; // Inode uses extents
    constexpr uint32_t EXT4_INLINE_DATA_FL      = 0x10000000; // Inode has inline data (stored in i_block and extra area)

    // File type in i_mode
    constexpr uint16_t EXT4_S_IFMT              = 0xF000; // Format mask
    constexpr uint16_t EXT4_S_IFREG             = 0x8000; // Regular file
    constexpr uint16_t EXT4_S_IFDIR             = 0x4000; // Directory
    constexpr uint16_t EXT4_S_IFLNK             = 0xA000; // Symbolic link

    // Directory entry file types (Ext4DirEntry2::file_type)
    constexpr uint8_t EXT4_FT_UNKNOWN           = 0;
    constexpr uint8_t EXT4_FT_REG_FILE          = 1;
    constexpr uint8_t EXT4_FT_DIR               = 2;
    constexpr uint8_t EXT4_FT_CHRDEV            = 3;
    constexpr uint8_t EXT4_FT_BLKDEV            = 4;
    constexpr uint8_t EXT4_FT_FIFO              = 5;
    constexpr uint8_t EXT4_FT_SOCK              = 6;
    constexpr uint8_t EXT4_FT_SYMLINK           = 7;

    // Feature incompat flags
    constexpr uint32_t EXT4_FEATURE_INCOMPAT_FILETYPE = 0x0002;
    constexpr uint32_t EXT4_FEATURE_INCOMPAT_EXTENTS  = 0x0040;
    constexpr uint32_t EXT4_FEATURE_INCOMPAT_64BIT    = 0x0080;
    constexpr uint32_t EXT4_FEATURE_INCOMPAT_FLEX_BG    = 0x0200;
    constexpr uint32_t EXT4_FEATURE_INCOMPAT_INLINE_DATA = 0x8000;

    /**
     * @brief The ext4 Superblock (1024 bytes)
     * Located at byte offset 1024 from the partition/volume start.
     */
    struct Ext4Superblock {
        uint32_t s_inodes_count;          // Total inodes count
        uint32_t s_blocks_count_lo;       // Total blocks count (low 32 bits)
        uint32_t s_r_blocks_count_lo;     // Reserved blocks count
        uint32_t s_free_blocks_count_lo;  // Free blocks count (low 32 bits)
        uint32_t s_free_inodes_count_lo;  // Free inodes count
        uint32_t s_first_data_block;      // First data block (0 for >1k blocks, 1 for 1k)
        uint32_t s_log_block_size;        // Block size = 1024 << s_log_block_size
        uint32_t s_log_cluster_size;      // Cluster size shift
        uint32_t s_blocks_per_group;      // Blocks per block group
        uint32_t s_clusters_per_group;    // Clusters per block group
        uint32_t s_inodes_per_group;      // Inodes per block group
        uint32_t s_mtime;                 // Mount time
        uint32_t s_wtime;                 // Write time
        uint16_t s_mnt_count;             // Mount count
        int16_t  s_max_mnt_count;         // Maximal mount count
        uint16_t s_magic;                 // Magic signature: 0xEF53
        uint16_t s_state;                 // File system state
        uint16_t s_errors;                // Behaviour when detecting errors
        uint16_t s_minor_rev_level;       // Minor revision level
        uint32_t s_lastcheck;             // Time of last check
        uint32_t s_checkinterval;         // Max. time between checks
        uint32_t s_creator_os;            // OS creator
        uint32_t s_rev_level;             // Revision level
        uint16_t s_def_resuid;            // Default uid for reserved blocks
        uint16_t s_def_resgid;            // Default gid for reserved blocks
        uint32_t s_first_ino;             // First non-reserved inode (usually 11)
        uint16_t s_inode_size;            // Size of on-disk inode structure
        uint16_t s_block_group_nr;        // Block group # of this superblock
        uint32_t s_feature_compat;        // Compatible feature set
        uint32_t s_feature_incompat;      // Incompatible feature set
        uint32_t s_feature_ro_compat;     // Readonly-compatible feature set
        uint8_t  s_uuid[16];              // 128-bit uuid for volume
        char     s_volume_name[16];       // Volume name
        char     s_last_mounted[64];      // Directory where last mounted
        uint32_t s_algorithm_usage_bitmap;// For compression
        uint8_t  s_prealloc_blocks;       // Nr of blocks to try to preallocate
        uint8_t  s_prealloc_dir_blocks;   // Nr to preallocate for dirs
        uint16_t s_reserved_gdt_blocks;   // Per group desc for online growth
        uint8_t  s_journal_uuid[16];      // UUID of journal superblock
        uint32_t s_journal_inum;          // Inode number of journal file
        uint32_t s_journal_dev;           // Device number of journal file
        uint32_t s_last_orphan;           // Head of list of inodes to delete
        uint32_t s_hash_seed[4];          // HTREE hash seed
        uint8_t  s_def_hash_version;      // Default hash version to use
        uint8_t  s_jnl_backup_type;       // Default type of journal backup
        uint16_t s_desc_size;             // Size of group descriptor (if 64BIT flag set)
        uint32_t s_default_mount_opts;    // Default mount options
        uint32_t s_first_meta_bg;         // First metablock group
        uint32_t s_mkfs_time;             // When the filesystem was created
        uint32_t s_jnl_blocks[17];        // Backup of the journal inode
        uint32_t s_blocks_count_hi;       // Blocks count (high 32 bits)
        uint32_t s_r_blocks_count_hi;     // Reserved blocks count (high 32 bits)
        uint32_t s_free_blocks_count_hi;  // Free blocks count (high 32 bits)
        uint16_t s_min_extra_isize;       // All inodes have at least # bytes
        uint16_t s_want_extra_isize;      // New inodes should reserve # bytes
        uint32_t s_flags;                 // Miscellaneous flags
        uint16_t s_raid_stride;           // RAID stride
        uint16_t s_mmp_interval;          // Seconds to wait in MMP checking
        uint64_t s_mmp_block;             // Block for multi-mount protection
        uint32_t s_raid_stripe_width;     // Blocks on all data disks (N*stride)
        uint8_t  s_log_groups_per_flex;   // FLEX_BG group size (2 ^ flex_group_size)
        uint8_t  s_checksum_type;         // Metadata checksum algorithm type
        uint8_t  s_encryption_level;      // Versioning level for encryption
        uint8_t  s_reserved_pad;          // Padding
        uint64_t s_kbytes_written;        // Number of lifetime kilobytes written
        uint32_t s_snapshot_inum;         // Inode number of active snapshot
        uint32_t s_snapshot_id;           // Sequential ID of active snapshot
        uint64_t s_snapshot_r_blocks_count; // Reserved blocks for active snapshot
        uint32_t s_snapshot_list;         // Inode number of snapshot list
        uint32_t s_error_count;           // Number of file system errors
        uint32_t s_first_error_time;      // First time an error happened
        uint32_t s_first_error_ino;       // Inode involved in first error
        uint64_t s_first_error_block;     // Block involved in first error
        uint8_t  s_first_error_func[32];  // Function where the error happened
        uint32_t s_first_error_line;      // Line number where error happened
        uint32_t s_last_error_time;       // Most recent time an error happened
        uint32_t s_last_error_ino;        // Inode involved in last error
        uint32_t s_last_error_line;       // Line number where last error happened
        uint64_t s_last_error_block;      // Block involved of last error
        uint8_t  s_last_error_func[32];   // Function where the last error happened
        uint8_t  s_mount_opts[64];        // Mount options string
        uint32_t s_usr_quota_inum;        // Inode for tracking user quota
        uint32_t s_grp_quota_inum;        // Inode for tracking group quota
        uint32_t s_overhead_clusters;     // Overhead blocks/clusters in fs
        uint32_t s_backup_bgs[2];         // Groups with sparse_super2 SBs
        uint8_t  s_encrypt_algos[4];      // Encryption algorithms in use
        uint8_t  s_encrypt_pw_salt[16];   // Salt used for string2key algorithm
        uint32_t s_lpf_ino;               // Location of lost+found inode
        uint32_t s_prj_quota_inum;        // Inode for tracking project quota
        uint32_t s_checksum_seed;         // Checksum seed
        uint8_t  s_wtime_hi;              // High 8 bits of write time
        uint8_t  s_mtime_hi;              // High 8 bits of mount time
        uint8_t  s_mkfs_time_hi;          // High 8 bits of mkfs time
        uint8_t  s_lastcheck_hi;          // High 8 bits of lastcheck time
        uint8_t  s_first_error_time_hi;   // High 8 bits of first error time
        uint8_t  s_last_error_time_hi;    // High 8 bits of last error time
        uint8_t  s_first_error_errcode;   // First error code
        uint8_t  s_last_error_errcode;    // Last error code
        uint16_t s_encoding;              // Filename charset encoding
        uint16_t s_encoding_flags;        // Filename charset encoding flags
        uint32_t s_orphan_file_inum;      // Inode for tracking orphan file
        uint32_t s_reserved[94];          // Padding to 1024 bytes
        uint32_t s_checksum;              // Superblock checksum
    };
    static_assert(sizeof(Ext4Superblock) == 1024, "Ext4Superblock size must be exactly 1024 bytes");

    /**
     * @brief 32-bit Block Group Descriptor (32 bytes)
     */
    struct Ext4GroupDesc {
        uint32_t bg_block_bitmap_lo;      // Blocks bitmap block
        uint32_t bg_inode_bitmap_lo;      // Inodes bitmap block
        uint32_t bg_inode_table_lo;       // Inodes table block
        uint16_t bg_free_blocks_count_lo; // Free blocks count
        uint16_t bg_free_inodes_count_lo; // Free inodes count
        uint16_t bg_used_dirs_count_lo;   // Directories count
        uint16_t bg_flags;                // EXT4_BG_flags
        uint32_t bg_exclude_bitmap_lo;    // Exclude bitmap for snapshots
        uint16_t bg_block_bitmap_csum_lo; // crc32c(s_uuid+grp_num+bbitmap) LE
        uint16_t bg_inode_bitmap_csum_lo; // crc32c(s_uuid+grp_num+ibitmap) LE
        uint16_t bg_itable_unused_lo;     // Unused inodes count
        uint16_t bg_checksum;             // crc16(sb_uuid+group+desc)
    };
    static_assert(sizeof(Ext4GroupDesc) == 32, "Ext4GroupDesc size must be exactly 32 bytes");

    /**
     * @brief 64-bit Block Group Descriptor (64 bytes)
     */
    struct Ext4GroupDesc64 {
        // First 32 bytes (low fields)
        Ext4GroupDesc lo;
        // High fields (for 64-bit block addressing)
        uint32_t bg_block_bitmap_hi;      // Blocks bitmap block MSB
        uint32_t bg_inode_bitmap_hi;      // Inodes bitmap block MSB
        uint32_t bg_inode_table_hi;       // Inodes table block MSB
        uint16_t bg_free_blocks_count_hi; // Free blocks count MSB
        uint16_t bg_free_inodes_count_hi; // Free inodes count MSB
        uint16_t bg_used_dirs_count_hi;   // Directories count MSB
        uint16_t bg_itable_unused_hi;     // Unused inodes count MSB
        uint32_t bg_exclude_bitmap_hi;    // Exclude bitmap block MSB
        uint16_t bg_block_bitmap_csum_hi; // crc32c(s_uuid+grp_num+bbitmap) BE
        uint16_t bg_inode_bitmap_csum_hi; // crc32c(s_uuid+grp_num+ibitmap) BE
        uint32_t bg_reserved;             // Padding
    };
    static_assert(sizeof(Ext4GroupDesc64) == 64, "Ext4GroupDesc64 size must be exactly 64 bytes");

    /**
     * @brief Ext4 Inode structure (128 bytes base)
     */
    struct Ext4Inode {
        uint16_t i_mode;                  // File mode (type and permissions)
        uint16_t i_uid;                   // Low 16 bits of Owner Uid
        uint32_t i_size_lo;               // Size in bytes (low 32 bits)
        uint32_t i_atime;                 // Access time
        uint32_t i_ctime;                 // Inode Change time
        uint32_t i_mtime;                 // Modification time
        uint32_t i_dtime;                 // Deletion Time (0 if active)
        uint16_t i_gid;                   // Low 16 bits of Group Id
        uint16_t i_links_count;           // Links count
        uint32_t i_blocks_lo;             // Blocks count (in 512-byte units)
        uint32_t i_flags;                 // File flags (e.g. EXT4_EXTENTS_FL = 0x80000)
        uint32_t i_osd1;                  // OS dependent 1
        uint8_t  i_block[60];             // Block pointers or Extent Tree Root
        uint32_t i_generation;            // File version (for NFS)
        uint32_t i_file_acl_lo;           // File ACL
        uint32_t i_size_high;             // High 32 bits of size (for regular files)
        uint32_t i_obso_faddr;            // Obsoleted fragment address
        uint8_t  i_osd2[12];              // OS dependent 2
    };
    static_assert(sizeof(Ext4Inode) == 128, "Ext4Inode base size must be exactly 128 bytes");

    /**
     * @brief Extent Tree Header (12 bytes)
     * Appears at the beginning of an extent block or in i_block[0..11].
     */
    struct Ext4ExtentHeader {
        uint16_t eh_magic;                // Magic: 0xF30A
        uint16_t eh_entries;              // Number of valid entries following header
        uint16_t eh_max;                  // Maximum capacity of entries
        uint16_t eh_depth;                // Depth of tree: 0 = leaf node, >0 = index node
        uint32_t eh_generation;           // Generation of extent tree
    };
    static_assert(sizeof(Ext4ExtentHeader) == 12, "Ext4ExtentHeader must be exactly 12 bytes");

    /**
     * @brief Extent Leaf Entry (12 bytes)
     * Describes contiguous allocated physical blocks.
     */
    struct Ext4Extent {
        uint32_t ee_block;                // First logical file block covered by this extent
        uint16_t ee_len;                  // Number of blocks (if <= 32768: init, > 32768: uninit)
        uint16_t ee_start_hi;             // High 16 bits of physical block
        uint32_t ee_start_lo;             // Low 32 bits of physical block
    };
    static_assert(sizeof(Ext4Extent) == 12, "Ext4Extent must be exactly 12 bytes");

    /**
     * @brief Extent Index Entry (12 bytes)
     * Internal node pointing to child extent block.
     */
    struct Ext4ExtentIdx {
        uint32_t ei_block;                // Index covers logical blocks from 'ei_block'
        uint32_t ei_leaf_lo;              // Low 32 bits of child physical block
        uint16_t ei_leaf_hi;              // High 16 bits of child physical block
        uint16_t ei_unused;               // Unused / alignment
    };
    static_assert(sizeof(Ext4ExtentIdx) == 12, "Ext4ExtentIdx must be exactly 12 bytes");

    /**
     * @brief Directory Entry 2 (variable length, minimum 8 bytes)
     * Used when EXT4_FEATURE_INCOMPAT_FILETYPE is enabled.
     */
    struct Ext4DirEntry2 {
        uint32_t inode;                   // Inode number (0 = deleted/unused entry)
        uint16_t rec_len;                 // Directory entry length (offset to next entry)
        uint8_t  name_len;                // Name length
        uint8_t  file_type;               // File type code (EXT4_FT_*)
        char     name[1];                 // File name characters (variable length, up to 255)
    };

} // namespace Ext4
} // namespace FileSystems
} // namespace Erasure

#pragma pack(pop) // Restore default packing
