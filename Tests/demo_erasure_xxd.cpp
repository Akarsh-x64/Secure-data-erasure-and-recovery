#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "../Erasure/Core/IStorageDevice.h"
#include "../Erasure/Hardware/Magnetic/HDDController.h"
#include "../Erasure/File Systems/ext4/ext4.h"
#include "../Erasure/File Systems/ext4/ext4_Structures.h"

using namespace Erasure;
using namespace Erasure::FileSystems::Ext4;

// POSIX raw file-based storage device for Linux / WSL testing
class FileStorageDevice : public Core::IStorageDevice {
private:
    int m_fd;
    std::string m_path;
    uint32_t m_sectorSize;
    uint64_t m_totalSectors;

public:
    FileStorageDevice() : m_fd(-1), m_sectorSize(512), m_totalSectors(0) {}
    ~FileStorageDevice() override { Close(); }

    bool Open(const std::string& path) override {
        m_path = path;
        m_fd = ::open(path.c_str(), O_RDWR);
        if (m_fd < 0) return false;
        struct stat st;
        if (::fstat(m_fd, &st) == 0) {
            m_totalSectors = st.st_size / m_sectorSize;
        }
        return true;
    }

    void Close() override {
        if (m_fd >= 0) {
            ::close(m_fd);
            m_fd = -1;
        }
    }

    bool ReadSectors(uint64_t startSector, uint32_t sectorCount, void* buffer) override {
        if (m_fd < 0) return false;
        off_t offset = startSector * m_sectorSize;
        size_t bytes = sectorCount * m_sectorSize;
        return (::pread(m_fd, buffer, bytes, offset) == static_cast<ssize_t>(bytes));
    }

    bool WriteSectors(uint64_t startSector, uint32_t sectorCount, const void* buffer) override {
        if (m_fd < 0) return false;
        off_t offset = startSector * m_sectorSize;
        size_t bytes = sectorCount * m_sectorSize;
        return (::pwrite(m_fd, buffer, bytes, offset) == static_cast<ssize_t>(bytes));
    }

    bool LockVolume() override { return true; }
    bool UnlockVolume() override { return true; }
    bool DismountVolume() override { return true; }
    bool SendDeviceCommand(uint32_t, void*, uint32_t, void*, uint32_t) override { return true; }

    Core::DeviceGeometry GetGeometry() const override {
        return { m_sectorSize, m_totalSectors, m_path };
    }
};

// Generates a 4MB ext4 image with both inline data files and regular extent files
static void CreateDemonstrationDisk(const std::string& filename) {
    constexpr uint32_t BLOCK_SIZE = 4096;
    constexpr uint32_t TOTAL_BLOCKS = 1024; // 4MB
    constexpr uint32_t INODES_PER_GROUP = 128;
    constexpr uint32_t INODE_SIZE = 256;

    std::vector<uint8_t> data(TOTAL_BLOCKS * BLOCK_SIZE, 0);

    // 1. Superblock at offset 1024
    Ext4Superblock sb;
    std::memset(&sb, 0, sizeof(sb));
    sb.s_magic = EXT4_SUPER_MAGIC; // 0xEF53
    sb.s_inodes_count = INODES_PER_GROUP;
    sb.s_blocks_count_lo = TOTAL_BLOCKS;
    sb.s_free_blocks_count_lo = 1000;
    sb.s_free_inodes_count_lo = INODES_PER_GROUP - 14;
    sb.s_first_data_block = 0;
    sb.s_log_block_size = 2; // 4096 bytes
    sb.s_blocks_per_group = TOTAL_BLOCKS;
    sb.s_inodes_per_group = INODES_PER_GROUP;
    sb.s_inode_size = INODE_SIZE;
    sb.s_feature_incompat = EXT4_FEATURE_INCOMPAT_FILETYPE | EXT4_FEATURE_INCOMPAT_EXTENTS | EXT4_FEATURE_INCOMPAT_INLINE_DATA;
    std::memcpy(sb.s_volume_name, "DEMO_EXT4", 9);
    for (int i = 0; i < 16; ++i) sb.s_uuid[i] = static_cast<uint8_t>(0xAA + i);

    std::memcpy(data.data() + EXT4_SUPERBLOCK_OFFSET, &sb, sizeof(sb));

    // 2. Group Descriptor Table at Block 1
    Ext4GroupDesc gd;
    std::memset(&gd, 0, sizeof(gd));
    gd.bg_block_bitmap_lo = 2;
    gd.bg_inode_bitmap_lo = 3;
    gd.bg_inode_table_lo  = 4; // Blocks 4..11 (8 blocks)
    gd.bg_free_blocks_count_lo = 1000;
    gd.bg_free_inodes_count_lo = INODES_PER_GROUP - 14;

    std::memcpy(data.data() + 1 * BLOCK_SIZE, &gd, sizeof(gd));

    // 3. Block Bitmap at Block 2
    // Blocks 0..15 marked as used
    uint8_t* bmap = data.data() + 2 * BLOCK_SIZE;
    bmap[0] = 0xFF; // blocks 0..7
    bmap[1] = 0xFF; // blocks 8..15

    // 4. Inode Bitmap at Block 3
    // Inodes 1..14 marked as used (bits 0..13)
    uint8_t* imap = data.data() + 3 * BLOCK_SIZE;
    imap[0] = 0xFF; // inodes 1..8
    imap[1] = 0x3F; // inodes 9..14 (6 bits: 0x3F)

    auto writeInode = [&](uint32_t inodeNum, const Ext4Inode& inode, const void* extraData = nullptr, size_t extraSize = 0) {
        uint64_t byteOffset = (4 * BLOCK_SIZE) + ((inodeNum - 1) * INODE_SIZE);
        std::memcpy(data.data() + byteOffset, &inode, sizeof(Ext4Inode));
        if (extraData && extraSize > 0) {
            size_t copyLen = std::min(extraSize, static_cast<size_t>(INODE_SIZE - 128));
            std::memcpy(data.data() + byteOffset + 128, extraData, copyLen);
        }
    };

    auto setSingleExtent = [](Ext4Inode& inode, uint64_t targetBlock) {
        inode.i_flags = EXT4_EXTENTS_FL;
        Ext4ExtentHeader* eh = reinterpret_cast<Ext4ExtentHeader*>(inode.i_block);
        eh->eh_magic = EXT4_EXTENT_MAGIC;
        eh->eh_entries = 1;
        eh->eh_max = 4;
        eh->eh_depth = 0;
        eh->eh_generation = 0;

        Ext4Extent* ext = reinterpret_cast<Ext4Extent*>(inode.i_block + sizeof(Ext4ExtentHeader));
        ext->ee_block = 0;
        ext->ee_len = 1;
        ext->ee_start_hi = static_cast<uint16_t>(targetBlock >> 32);
        ext->ee_start_lo = static_cast<uint32_t>(targetBlock & 0xFFFFFFFF);
    };

    // 5. Inode 2: Root Directory '/' (Points to Block 12)
    Ext4Inode rootInode;
    std::memset(&rootInode, 0, sizeof(rootInode));
    rootInode.i_mode = EXT4_S_IFDIR | 0755;
    rootInode.i_size_lo = BLOCK_SIZE;
    rootInode.i_links_count = 3;
    setSingleExtent(rootInode, 12);
    writeInode(EXT4_ROOT_INO, rootInode);

    // Root directory entries in Block 12
    uint8_t* dirBlock12 = data.data() + 12 * BLOCK_SIZE;
    size_t off = 0;

    // '.'
    Ext4DirEntry2* e1 = reinterpret_cast<Ext4DirEntry2*>(dirBlock12 + off);
    e1->inode = 2; e1->rec_len = 12; e1->name_len = 1; e1->file_type = EXT4_FT_DIR; e1->name[0] = '.';
    off += e1->rec_len;

    // '..'
    Ext4DirEntry2* e2 = reinterpret_cast<Ext4DirEntry2*>(dirBlock12 + off);
    e2->inode = 2; e2->rec_len = 12; e2->name_len = 2; e2->file_type = EXT4_FT_DIR; e2->name[0] = '.'; e2->name[1] = '.';
    off += e2->rec_len;

    // 'pin_code.txt' (INLINE file, Inode 11)
    Ext4DirEntry2* e3 = reinterpret_cast<Ext4DirEntry2*>(dirBlock12 + off);
    e3->inode = 11; e3->rec_len = 24; e3->name_len = 12; e3->file_type = EXT4_FT_REG_FILE;
    std::memcpy(e3->name, "pin_code.txt", 12);
    off += e3->rec_len;

    // 'database_dump.sql' (REGULAR extent file, Inode 12)
    Ext4DirEntry2* e4 = reinterpret_cast<Ext4DirEntry2*>(dirBlock12 + off);
    e4->inode = 12; e4->rec_len = 28; e4->name_len = 17; e4->file_type = EXT4_FT_REG_FILE;
    std::memcpy(e4->name, "database_dump.sql", 17);
    off += e4->rec_len;

    // 'secrets' (Subdirectory, Inode 13)
    Ext4DirEntry2* e5 = reinterpret_cast<Ext4DirEntry2*>(dirBlock12 + off);
    e5->inode = 13; e5->rec_len = static_cast<uint16_t>(BLOCK_SIZE - off); e5->name_len = 7; e5->file_type = EXT4_FT_DIR;
    std::memcpy(e5->name, "secrets", 7);

    // 6. Inode 11: '/pin_code.txt' -> INLINE DATA FILE
    // No external blocks! i_blocks_lo = 0, no extents flag, inline data flag set
    Ext4Inode inlineFile11;
    std::memset(&inlineFile11, 0, sizeof(inlineFile11));
    inlineFile11.i_mode = EXT4_S_IFREG | 0600;
    inlineFile11.i_size_lo = 32; // 32 bytes
    inlineFile11.i_links_count = 1;
    inlineFile11.i_flags = EXT4_INLINE_DATA_FL;
    // Data stored directly inside i_block[60]
    const char* inlinePayload = "PIN=9876_SUPER_SECRET_VAULT_KEY!";
    std::memcpy(inlineFile11.i_block, inlinePayload, 32);
    // Also simulate extra extended security attributes in the 128..255 byte region of the 256-byte inode
    const char* extraSecurityAttr = "SEC_ATTR:CLEARANCE_LEVEL_TOP_SECRET_SIG=0xDEADBEEF";
    writeInode(11, inlineFile11, extraSecurityAttr, std::strlen(extraSecurityAttr));

    // 7. Inode 12: '/database_dump.sql' -> REGULAR EXTENT FILE (Block 13)
    Ext4Inode regularFile12;
    std::memset(&regularFile12, 0, sizeof(regularFile12));
    regularFile12.i_mode = EXT4_S_IFREG | 0644;
    regularFile12.i_size_lo = 64;
    regularFile12.i_links_count = 1;
    setSingleExtent(regularFile12, 13); // Points to Block 13
    writeInode(12, regularFile12);

    // Block 13 Payload
    const char* regularPayload = "DATABASE_DUMP: root_hash=$6$rounds=5000$salt$HASHED_PASSWORDS_HERE";
    std::memcpy(data.data() + 13 * BLOCK_SIZE, regularPayload, std::strlen(regularPayload));

    // 8. Inode 13: Subdirectory '/secrets' (Points to Block 14)
    Ext4Inode dir13;
    std::memset(&dir13, 0, sizeof(dir13));
    dir13.i_mode = EXT4_S_IFDIR | 0755;
    dir13.i_size_lo = BLOCK_SIZE;
    dir13.i_links_count = 2;
    setSingleExtent(dir13, 14); // Points to Block 14
    writeInode(13, dir13);

    // Directory entries in Block 14
    uint8_t* dirBlock14 = data.data() + 14 * BLOCK_SIZE;
    off = 0;

    Ext4DirEntry2* s1 = reinterpret_cast<Ext4DirEntry2*>(dirBlock14 + off);
    s1->inode = 13; s1->rec_len = 12; s1->name_len = 1; s1->file_type = EXT4_FT_DIR; s1->name[0] = '.';
    off += s1->rec_len;

    Ext4DirEntry2* s2 = reinterpret_cast<Ext4DirEntry2*>(dirBlock14 + off);
    s2->inode = 2; s2->rec_len = 12; s2->name_len = 2; s2->file_type = EXT4_FT_DIR; s2->name[0] = '.'; s2->name[1] = '.';
    off += s2->rec_len;

    Ext4DirEntry2* s3 = reinterpret_cast<Ext4DirEntry2*>(dirBlock14 + off);
    s3->inode = 14; s3->rec_len = static_cast<uint16_t>(BLOCK_SIZE - off); s3->name_len = 14; s3->file_type = EXT4_FT_REG_FILE;
    std::memcpy(s3->name, "classified.pdf", 14);

    // 9. Inode 14: Nested File '/secrets/classified.pdf' (Points to Block 15)
    Ext4Inode file14;
    std::memset(&file14, 0, sizeof(file14));
    file14.i_mode = EXT4_S_IFREG | 0644;
    file14.i_size_lo = 45;
    file14.i_links_count = 1;
    setSingleExtent(file14, 15); // Points to Block 15
    writeInode(14, file14);

    // Block 15 Payload
    const char* pdfPayload = "%PDF-1.7: TOP_SECRET_DEFENSE_CONTRACT_2026_REPORT";
    std::memcpy(data.data() + 15 * BLOCK_SIZE, pdfPayload, std::strlen(pdfPayload));

    // Flush to disk file
    std::ofstream out(filename, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()), data.size());
    out.close();
}

int main(int argc, char* argv[]) {
    const std::string imgPath = "demo_test_disk.img";

    if (argc > 1 && std::string(argv[1]) == "--create") {
        std::cout << "Creating synthetic ext4 demonstration disk: " << imgPath << " (4MB)...\n";
        CreateDemonstrationDisk(imgPath);
        std::cout << "Demonstration disk created successfully.\n";
        return 0;
    }

    if (argc > 1 && std::string(argv[1]) == "--erase") {
        if (argc < 3) {
            std::cerr << "Usage: " << argv[0] << " --erase <relative_file_path>\n";
            return 1;
        }
        std::string target = argv[2];

        FileStorageDevice dev;
        if (!dev.Open(imgPath)) {
            std::cerr << "Failed to open " << imgPath << "\n";
            return 1;
        }

        Hardware::HDDController hw(&dev);
        FileSystems::Ext4Driver ext4(&hw);

        if (!ext4.Mount()) {
            std::cerr << "Failed to mount ext4 on " << imgPath << "\n";
            return 1;
        }

        std::cout << "Mount successful. Executing EraseFile for: " << target << "\n";
        if (ext4.EraseFile(target)) {
            std::cout << "EraseFile succeeded for " << target << "\n";
        } else {
            std::cerr << "EraseFile failed for " << target << "\n";
            return 1;
        }
        return 0;
    }

    std::cout << "Usage:\n";
    std::cout << "  " << argv[0] << " --create\n";
    std::cout << "  " << argv[0] << " --erase <path>\n";
    return 0;
}
