#include "Ext4Detector.h"
#include "../../Erasure/File Systems/ext4/ext4_Structures.h"
#include <iomanip>
#include <sstream>
#include <cstring>

namespace Recovery {
namespace Filesystems {

static constexpr uint64_t EXT4_SB_OFFSET = 1024;

bool Ext4Detector::CanParse(Core::ByteReader& reader, const Core::StorageRegion& region) {
    uint64_t sbOffset = region.startOffset + EXT4_SB_OFFSET;

    // Check if region/storage has room for at least offset + 1024 + 1024 bytes
    if (reader.GetSize() > 0 && sbOffset + sizeof(Erasure::FileSystems::Ext4::Ext4Superblock) > reader.GetSize()) {
        return false;
    }

    // Offset to s_magic in superblock is 0x38 (56 bytes from start of superblock)
    uint16_t magic = 0;
    if (!reader.ReadBytes(sbOffset + 0x38, sizeof(magic), &magic)) {
        return false;
    }

    return (magic == Erasure::FileSystems::Ext4::EXT4_SUPER_MAGIC);
}

Ext4Metadata Ext4Detector::Parse(Core::ByteReader& reader, const Core::StorageRegion& region) {
    Ext4Metadata meta;
    uint64_t sbOffset = region.startOffset + EXT4_SB_OFFSET;

    Erasure::FileSystems::Ext4::Ext4Superblock sb;
    if (!reader.ReadStruct(sbOffset, sb)) {
        return meta;
    }

    if (sb.s_magic != Erasure::FileSystems::Ext4::EXT4_SUPER_MAGIC) {
        return meta;
    }

    meta.isValid = true;
    meta.blockSize = 1024 << sb.s_log_block_size;
    meta.is64Bit = (sb.s_feature_incompat & Erasure::FileSystems::Ext4::EXT4_FEATURE_INCOMPAT_64BIT) != 0;
    meta.hasExtents = (sb.s_feature_incompat & Erasure::FileSystems::Ext4::EXT4_FEATURE_INCOMPAT_EXTENTS) != 0;

    meta.totalBlocks = sb.s_blocks_count_lo;
    if (meta.is64Bit) {
        meta.totalBlocks |= (static_cast<uint64_t>(sb.s_blocks_count_hi) << 32);
    }

    meta.freeBlocks = sb.s_free_blocks_count_lo;
    if (meta.is64Bit) {
        meta.freeBlocks |= (static_cast<uint64_t>(sb.s_free_blocks_count_hi) << 32);
    }

    meta.totalInodes = sb.s_inodes_count;
    meta.freeInodes = sb.s_free_inodes_count_lo;
    meta.inodeSize = (sb.s_inode_size >= 128) ? sb.s_inode_size : 128;

    // Volume name (null-terminated copy)
    char volName[17] = {0};
    std::memcpy(volName, sb.s_volume_name, 16);
    meta.volumeName = std::string(volName);

    // Format UUID (16 bytes -> 8-4-4-4-12)
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < 16; ++i) {
        oss << std::setw(2) << static_cast<unsigned>(sb.s_uuid[i]);
        if (i == 3 || i == 5 || i == 7 || i == 9) {
            oss << "-";
        }
    }
    meta.uuid = oss.str();

    return meta;
}

} // namespace Filesystems
} // namespace Recovery
