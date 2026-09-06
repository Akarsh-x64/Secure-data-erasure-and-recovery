#include <iostream>
#include <vector>
#include <cstring>
#include <cassert>

#include "../Erasure/Core/IStorageDevice.h"
#include "../Erasure/Hardware/Magnetic/HDDController.h"
#include "../Erasure/File Systems/ext4/ext4.h"
#include "../Erasure/File Systems/ext4/ext4_Structures.h"
#include "../Recovery/Core/IReadOnlyStorage.h"
#include "../Recovery/Core/ByteReader.h"
#include "../Recovery/Filesystems/Ext4Detector.h"

using namespace Erasure;
using namespace Erasure::FileSystems::Ext4;

// ============================================================================
// In-Memory Storage Device (implements both Erasure & Recovery interfaces)
// ============================================================================

class MemoryDiskDevice : public Core::IStorageDevice, public Recovery::Core::IReadOnlyStorage {
private:
    std::vector<uint8_t> m_disk;
    uint32_t m_sectorSize;

public:
    explicit MemoryDiskDevice(size_t totalBytes, uint32_t sectorSize = 512)
        : m_disk(totalBytes, 0)
        , m_sectorSize(sectorSize)
    {}

    // IStorageDevice methods
    bool Open(const std::string&) override { return true; }
    void Close() override {}

    bool ReadSectors(uint64_t startSector, uint32_t sectorCount, void* buffer) override {
        size_t startByte = startSector * m_sectorSize;
        size_t byteCount = static_cast<size_t>(sectorCount) * m_sectorSize;
        if (startByte + byteCount > m_disk.size()) return false;
        std::memcpy(buffer, m_disk.data() + startByte, byteCount);
        return true;
    }

    bool WriteSectors(uint64_t startSector, uint32_t sectorCount, const void* buffer) override {
        size_t startByte = startSector * m_sectorSize;
        size_t byteCount = static_cast<size_t>(sectorCount) * m_sectorSize;
        if (startByte + byteCount > m_disk.size()) return false;
        std::memcpy(m_disk.data() + startByte, buffer, byteCount);
        return true;
    }

    bool LockVolume() override { return true; }
    bool UnlockVolume() override { return true; }
    bool DismountVolume() override { return true; }
    bool SendDeviceCommand(uint32_t, void*, uint32_t, void*, uint32_t) override { return true; }

    Core::DeviceGeometry GetGeometry() const override {
        return { m_sectorSize, m_disk.size() / m_sectorSize, "MemoryDisk" };
    }

    // IReadOnlyStorage methods
    bool Read(uint64_t offset, uint32_t size, void* buffer) override {
        if (offset + size > m_disk.size()) return false;
        std::memcpy(buffer, m_disk.data() + offset, size);
        return true;
    }

    uint64_t GetSize() const override { return m_disk.size(); }
    uint32_t GetSectorSize() const override { return m_sectorSize; }

    // Direct memory access for assertions
    const std::vector<uint8_t>& GetData() const { return m_disk; }
    std::vector<uint8_t>& GetData() { return m_disk; }
};

// ============================================================================
// Synthetic ext4 Filesystem Builder
// ============================================================================

static void BuildSyntheticExt4Image(MemoryDiskDevice& disk) {
    constexpr uint32_t BLOCK_SIZE = 4096;
    constexpr uint32_t TOTAL_BLOCKS = 1024; // 4 MB image
    constexpr uint32_t INODES_PER_GROUP = 128;
    constexpr uint32_t INODE_SIZE = 256;

    std::vector<uint8_t>& data = disk.GetData();

    // 1. Superblock at byte offset 1024
    Ext4Superblock sb;
    std::memset(&sb, 0, sizeof(sb));
    sb.s_magic = EXT4_SUPER_MAGIC; // 0xEF53
    sb.s_inodes_count = INODES_PER_GROUP;
    sb.s_blocks_count_lo = TOTAL_BLOCKS;
    sb.s_free_blocks_count_lo = 1000;
    sb.s_free_inodes_count_lo = INODES_PER_GROUP - 13;
    sb.s_first_data_block = 0;
    sb.s_log_block_size = 2; // 1024 << 2 = 4096
    sb.s_blocks_per_group = TOTAL_BLOCKS;
    sb.s_inodes_per_group = INODES_PER_GROUP;
    sb.s_inode_size = INODE_SIZE;
    sb.s_feature_incompat = EXT4_FEATURE_INCOMPAT_FILETYPE | EXT4_FEATURE_INCOMPAT_EXTENTS;
    std::memcpy(sb.s_volume_name, "TEST_EXT4", 9);
    for (int i = 0; i < 16; ++i) sb.s_uuid[i] = static_cast<uint8_t>(i + 1);

    std::memcpy(data.data() + EXT4_SUPERBLOCK_OFFSET, &sb, sizeof(sb));

    // 2. Block Group Descriptor Table at Block 1 (offset 4096)
    Ext4GroupDesc gd;
    std::memset(&gd, 0, sizeof(gd));
    gd.bg_block_bitmap_lo = 2; // Block 2: Block bitmap
    gd.bg_inode_bitmap_lo = 3; // Block 3: Inode bitmap
    gd.bg_inode_table_lo  = 4; // Block 4..11: Inode table (128 * 256 = 32768 bytes = 8 blocks)
    gd.bg_free_blocks_count_lo = 1000;
    gd.bg_free_inodes_count_lo = INODES_PER_GROUP - 13;

    std::memcpy(data.data() + 1 * BLOCK_SIZE, &gd, sizeof(gd));

    // 3. Block Bitmap at Block 2 (offset 8192)
    // Mark blocks 0..15 as used
    uint8_t* bmap = data.data() + 2 * BLOCK_SIZE;
    bmap[0] = 0xFF; // blocks 0..7
    bmap[1] = 0xFF; // blocks 8..15

    // 4. Inode Bitmap at Block 3 (offset 12288)
    // Inodes 1..13 are used (bits 0..12)
    uint8_t* imap = data.data() + 3 * BLOCK_SIZE;
    imap[0] = 0xFF; // inodes 1..8
    imap[1] = 0x1F; // inodes 9..13 (5 bits: 1+2+4+8+16 = 31 = 0x1F)

    // Helper to write an inode into the inode table (blocks 4..11)
    auto writeInode = [&](uint32_t inodeNum, const Ext4Inode& inode) {
        uint64_t byteOffset = (4 * BLOCK_SIZE) + ((inodeNum - 1) * INODE_SIZE);
        std::memcpy(data.data() + byteOffset, &inode, sizeof(Ext4Inode));
    };

    // Helper to initialize leaf extent
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

    // 5. Inode 2: Root Directory '/'
    Ext4Inode rootInode;
    std::memset(&rootInode, 0, sizeof(rootInode));
    rootInode.i_mode = EXT4_S_IFDIR | 0755;
    rootInode.i_size_lo = BLOCK_SIZE;
    rootInode.i_links_count = 3;
    setSingleExtent(rootInode, 12); // Points to Block 12
    writeInode(EXT4_ROOT_INO, rootInode);

    // Root Directory entries in Block 12
    uint8_t* dirBlock12 = data.data() + 12 * BLOCK_SIZE;
    size_t off = 0;

    // Entry 1: '.' (inode 2, len 12)
    Ext4DirEntry2* e1 = reinterpret_cast<Ext4DirEntry2*>(dirBlock12 + off);
    e1->inode = 2;
    e1->rec_len = 12;
    e1->name_len = 1;
    e1->file_type = EXT4_FT_DIR;
    e1->name[0] = '.';
    off += e1->rec_len;

    // Entry 2: '..' (inode 2, len 12)
    Ext4DirEntry2* e2 = reinterpret_cast<Ext4DirEntry2*>(dirBlock12 + off);
    e2->inode = 2;
    e2->rec_len = 12;
    e2->name_len = 2;
    e2->file_type = EXT4_FT_DIR;
    e2->name[0] = '.'; e2->name[1] = '.';
    off += e2->rec_len;

    // Entry 3: 'secret.txt' (inode 11, len 24)
    Ext4DirEntry2* e3 = reinterpret_cast<Ext4DirEntry2*>(dirBlock12 + off);
    e3->inode = 11;
    e3->rec_len = 24;
    e3->name_len = 10;
    e3->file_type = EXT4_FT_REG_FILE;
    std::memcpy(e3->name, "secret.txt", 10);
    off += e3->rec_len;

    // Entry 4: 'documents' (inode 12, rec_len covers rest of block)
    Ext4DirEntry2* e4 = reinterpret_cast<Ext4DirEntry2*>(dirBlock12 + off);
    e4->inode = 12;
    e4->rec_len = static_cast<uint16_t>(BLOCK_SIZE - off);
    e4->name_len = 9;
    e4->file_type = EXT4_FT_DIR;
    std::memcpy(e4->name, "documents", 9);

    // 6. Inode 11: File '/secret.txt'
    Ext4Inode file11;
    std::memset(&file11, 0, sizeof(file11));
    file11.i_mode = EXT4_S_IFREG | 0644;
    file11.i_size_lo = 16;
    file11.i_links_count = 1;
    setSingleExtent(file11, 13); // Points to Block 13
    writeInode(11, file11);

    // Data in Block 13: Payload
    std::memcpy(data.data() + 13 * BLOCK_SIZE, "CONFIDENTIAL_123", 16);

    // 7. Inode 12: Directory '/documents'
    Ext4Inode dir12;
    std::memset(&dir12, 0, sizeof(dir12));
    dir12.i_mode = EXT4_S_IFDIR | 0755;
    dir12.i_size_lo = BLOCK_SIZE;
    dir12.i_links_count = 2;
    setSingleExtent(dir12, 14); // Points to Block 14
    writeInode(12, dir12);

    // Directory entries in Block 14
    uint8_t* dirBlock14 = data.data() + 14 * BLOCK_SIZE;
    off = 0;

    Ext4DirEntry2* d1 = reinterpret_cast<Ext4DirEntry2*>(dirBlock14 + off);
    d1->inode = 12;
    d1->rec_len = 12;
    d1->name_len = 1;
    d1->file_type = EXT4_FT_DIR;
    d1->name[0] = '.';
    off += d1->rec_len;

    Ext4DirEntry2* d2 = reinterpret_cast<Ext4DirEntry2*>(dirBlock14 + off);
    d2->inode = 2;
    d2->rec_len = 12;
    d2->name_len = 2;
    d2->file_type = EXT4_FT_DIR;
    d2->name[0] = '.'; d2->name[1] = '.';
    off += d2->rec_len;

    Ext4DirEntry2* d3 = reinterpret_cast<Ext4DirEntry2*>(dirBlock14 + off);
    d3->inode = 13;
    d3->rec_len = static_cast<uint16_t>(BLOCK_SIZE - off);
    d3->name_len = 14;
    d3->file_type = EXT4_FT_REG_FILE;
    std::memcpy(d3->name, "classified.pdf", 14);

    // 8. Inode 13: File '/documents/classified.pdf'
    Ext4Inode file13;
    std::memset(&file13, 0, sizeof(file13));
    file13.i_mode = EXT4_S_IFREG | 0644;
    file13.i_size_lo = 21;
    file13.i_links_count = 1;
    setSingleExtent(file13, 15); // Points to Block 15
    writeInode(13, file13);

    // Data in Block 15: Payload
    std::memcpy(data.data() + 15 * BLOCK_SIZE, "TOP_SECRET_CLASSIFIED", 21);
}

// ============================================================================
// Automated Tests Runner
// ============================================================================

static int g_passed = 0;
static int g_failed = 0;

static void AssertCheck(const char* name, bool condition) {
    if (condition) {
        std::cout << "  [PASS] " << name << "\n";
        ++g_passed;
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failed;
    }
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "       Ext4 Filesystem Support - Test Suite             \n";
    std::cout << "========================================================\n\n";

    constexpr size_t DISK_SIZE = 1024 * 4096; // 4MB
    MemoryDiskDevice disk(DISK_SIZE, 512);
    BuildSyntheticExt4Image(disk);

    // ------------------------------------------------------------------------
    // TEST 1: Recovery Module Ext4Detector
    // ------------------------------------------------------------------------
    std::cout << ">>> Running Test Suite 1: Recovery Ext4Detector <<<\n";
    {
        Recovery::Core::ByteReader reader(&disk);
        bool canParse = Recovery::Filesystems::Ext4Detector::CanParse(reader);
        AssertCheck("Ext4Detector::CanParse detects valid ext4 superblock", canParse);

        Recovery::Filesystems::Ext4Metadata meta = Recovery::Filesystems::Ext4Detector::Parse(reader);
        AssertCheck("Ext4Metadata isValid is true", meta.isValid);
        AssertCheck("Ext4Metadata block size is 4096", meta.blockSize == 4096);
        AssertCheck("Ext4Metadata total blocks is 1024", meta.totalBlocks == 1024);
        AssertCheck("Ext4Metadata volume name is 'TEST_EXT4'", meta.volumeName == "TEST_EXT4");
        AssertCheck("Ext4Metadata extents feature is enabled", meta.hasExtents);
    }

    // ------------------------------------------------------------------------
    // TEST 2: Erasure Module Mount & Superblock Parsing
    // ------------------------------------------------------------------------
    std::cout << "\n>>> Running Test Suite 2: Erasure Ext4Driver Mount <<<\n";
    Hardware::HDDController hw(&disk);
    FileSystems::Ext4Driver ext4(&hw);

    AssertCheck("Ext4Driver::Mount() succeeds", ext4.Mount());
    AssertCheck("Ext4Driver block size is 4096", ext4.GetBlockSize() == 4096);
    AssertCheck("Ext4Driver group count is 1", ext4.GetGroupCount() == 1);
    ext4.PrintSuperblockInfo();

    // ------------------------------------------------------------------------
    // TEST 3: Pre-Erasure Data Integrity Check
    // ------------------------------------------------------------------------
    std::cout << "\n>>> Running Test Suite 3: Pre-Erasure Data Integrity <<<\n";
    {
        const uint8_t* block13 = disk.GetData().data() + 13 * 4096;
        AssertCheck("Block 13 contains 'CONFIDENTIAL_123'",
                    std::memcmp(block13, "CONFIDENTIAL_123", 16) == 0);

        const uint8_t* block15 = disk.GetData().data() + 15 * 4096;
        AssertCheck("Block 15 contains 'TOP_SECRET_CLASSIFIED'",
                    std::memcmp(block15, "TOP_SECRET_CLASSIFIED", 21) == 0);
    }

    // ------------------------------------------------------------------------
    // TEST 4: Surgical Single File Erasure (secret.txt)
    // ------------------------------------------------------------------------
    std::cout << "\n>>> Running Test Suite 4: Surgical Single File Erasure <<<\n";
    {
        bool erased = ext4.EraseFile("secret.txt");
        AssertCheck("ext4.EraseFile('secret.txt') succeeds", erased);

        // Verify data block 13 is zeroed
        const uint8_t* block13 = disk.GetData().data() + 13 * 4096;
        bool block13Zeroed = true;
        for (size_t i = 0; i < 4096; ++i) {
            if (block13[i] != 0) { block13Zeroed = false; break; }
        }
        AssertCheck("Block 13 data sectors are completely zeroed", block13Zeroed);

        // Verify Block Bitmap bit for block 13 is cleared (bit 13 = byte 1, bit 5)
        const uint8_t* bmap = disk.GetData().data() + 2 * 4096;
        bool bmap13Cleared = ((bmap[1] & (1 << 5)) == 0);
        AssertCheck("Block 13 allocation bit in Block Bitmap is cleared", bmap13Cleared);

        // Verify Inode Bitmap bit for inode 11 is cleared (bit 10 = byte 1, bit 2)
        const uint8_t* imap = disk.GetData().data() + 3 * 4096;
        bool imap11Cleared = ((imap[1] & (1 << 2)) == 0);
        AssertCheck("Inode 11 allocation bit in Inode Bitmap is cleared", imap11Cleared);

        // Verify Inode 11 table entry is scrubbed (size = 0, blocks = 0)
        const uint8_t* itable = disk.GetData().data() + 4 * 4096;
        const Ext4Inode* ino11 = reinterpret_cast<const Ext4Inode*>(itable + 10 * 256);
        AssertCheck("Inode 11 size is zeroed", ino11->i_size_lo == 0);
        AssertCheck("Inode 11 deletion timestamp is set", ino11->i_dtime != 0);

        // Verify directory entry in block 12 is neutralized (inode = 0)
        const uint8_t* dirBlock12 = disk.GetData().data() + 12 * 4096;
        const Ext4DirEntry2* e3 = reinterpret_cast<const Ext4DirEntry2*>(dirBlock12 + 24);
        AssertCheck("Directory entry for 'secret.txt' has inode set to 0", e3->inode == 0);
    }

    // ------------------------------------------------------------------------
    // TEST 5: Recursive Path Traversal Erasure (documents/classified.pdf)
    // ------------------------------------------------------------------------
    std::cout << "\n>>> Running Test Suite 5: Recursive Path Traversal Erasure <<<\n";
    {
        bool erased = ext4.EraseFile("documents/classified.pdf");
        AssertCheck("ext4.EraseFile('documents/classified.pdf') succeeds", erased);

        // Verify data block 15 is zeroed
        const uint8_t* block15 = disk.GetData().data() + 15 * 4096;
        bool block15Zeroed = true;
        for (size_t i = 0; i < 4096; ++i) {
            if (block15[i] != 0) { block15Zeroed = false; break; }
        }
        AssertCheck("Block 15 data sectors are completely zeroed", block15Zeroed);

        // Verify Block Bitmap bit for block 15 is cleared (bit 15 = byte 1, bit 7)
        const uint8_t* bmap = disk.GetData().data() + 2 * 4096;
        bool bmap15Cleared = ((bmap[1] & (1 << 7)) == 0);
        AssertCheck("Block 15 allocation bit in Block Bitmap is cleared", bmap15Cleared);

        // Verify Inode Bitmap bit for inode 13 is cleared (bit 12 = byte 1, bit 4)
        const uint8_t* imap = disk.GetData().data() + 3 * 4096;
        bool imap13Cleared = ((imap[1] & (1 << 4)) == 0);
        AssertCheck("Inode 13 allocation bit in Inode Bitmap is cleared", imap13Cleared);
    }

    // ------------------------------------------------------------------------
    // TEST 6: Full Volume Wipe with Structure Quarantine
    // ------------------------------------------------------------------------
    std::cout << "\n>>> Running Test Suite 6: Full Volume Wipe <<<\n";
    {
        // Re-seed a canary block in an unallocated data block (e.g. block 50)
        std::memcpy(disk.GetData().data() + 50 * 4096, "CANARY_DATA", 11);

        bool wiped = ext4.WipeVolume();
        AssertCheck("ext4.WipeVolume() succeeds", wiped);

        // Canary block 50 must be wiped
        const uint8_t* block50 = disk.GetData().data() + 50 * 4096;
        bool block50Zeroed = true;
        for (size_t i = 0; i < 4096; ++i) {
            if (block50[i] != 0) { block50Zeroed = false; break; }
        }
        AssertCheck("Unallocated Canary Block 50 was sanitized", block50Zeroed);

        // Quarantined Superblock must still be intact
        const Ext4Superblock* sb = reinterpret_cast<const Ext4Superblock*>(disk.GetData().data() + EXT4_SUPERBLOCK_OFFSET);
        AssertCheck("Quarantined Superblock is intact (magic 0xEF53)", sb->s_magic == EXT4_SUPER_MAGIC);

        // Quarantined GDT must still be intact
        const Ext4GroupDesc* gd = reinterpret_cast<const Ext4GroupDesc*>(disk.GetData().data() + 1 * 4096);
        AssertCheck("Quarantined GDT is intact (block bitmap at block 2)", gd->bg_block_bitmap_lo == 2);

        // Volume can be re-mounted cleanly after wipe
        FileSystems::Ext4Driver remountedExt4(&hw);
        AssertCheck("Remounting wiped volume succeeds", remountedExt4.Mount());
    }

    std::cout << "\n========================================================\n";
    std::cout << " Test Summary: " << g_passed << " Passed, " << g_failed << " Failed\n";
    std::cout << "========================================================\n";

    return (g_failed == 0) ? 0 : 1;
}
