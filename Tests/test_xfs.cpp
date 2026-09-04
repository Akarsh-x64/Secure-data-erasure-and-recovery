#include <iostream>
#include <vector>
#include <cstring>
#include <cassert>

#include "../Erasure/Core/IStorageDevice.h"
#include "../Erasure/Hardware/Magnetic/HDDController.h"
#include "../Erasure/File Systems/XFS/XFS.h"
#include "../Erasure/File Systems/XFS/XFS_Structures.h"

using namespace Erasure;
using namespace Erasure::FileSystems;
using namespace Erasure::FileSystems::XFS;

// ============================================================================
// In-Memory Storage Device (Simulates raw block device in RAM)
// ============================================================================
// Implements the IStorageDevice OS-layer interface using a std::vector<uint8_t>
// so tests can run hermetically without requiring physical root/admin access.

class MemoryDiskDevice : public Core::IStorageDevice {
private:
    std::vector<uint8_t> m_disk;
    uint32_t m_sectorSize;

public:
    explicit MemoryDiskDevice(size_t totalBytes, uint32_t sectorSize = 512)
        : m_disk(totalBytes, 0)
        , m_sectorSize(sectorSize)
    {}

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

    uint8_t* GetDiskData() { return m_disk.data(); }
    size_t GetDiskSize() const { return m_disk.size(); }
};

/**
 * @brief Helper to pack a 128-bit XFS BMBT Extent Record in Big-Endian.
 *
 * Bitfield layout across two 64-bit words:
 *   - w0[63]:     state (0=written)
 *   - w0[9..62]:  startoff (54 bits)
 *   - w0[0..8]:   startblock high 9 bits
 *   - w1[21..63]: startblock low 43 bits
 *   - w1[0..20]:  blockcount (21 bits)
 */
static XfsBmbtRec MakeExtentRec(uint64_t startoff, uint64_t startblock, uint32_t blockcount) {
    uint64_t w0 = ((startoff & ((1ULL << 54) - 1)) << 9) | ((startblock >> 43) & 0x1FFULL);
    uint64_t w1 = ((startblock & ((1ULL << 43) - 1)) << 21) | (blockcount & ((1ULL << 21) - 1));

    XfsBmbtRec rec;
    rec.l0 = cpu_to_be64(w0);
    rec.l1 = cpu_to_be64(w1);
    return rec;
}

// ============================================================================
// Synthetic XFS Filesystem Geometry & Layout Constants
// ============================================================================

static constexpr size_t DISK_SIZE = 4 * 1024 * 1024;                    // 4 MB total memory disk
static constexpr uint32_t SECTOR_SIZE = 512;                            // 512 bytes per sector
static constexpr uint32_t BLOCK_SIZE = 4096;                            // 4096 bytes per block (8 sectors/block)
static constexpr uint32_t INODE_SIZE = 256;                             // 256-byte standard XFS inodes
static constexpr uint32_t INODES_PER_BLOCK = BLOCK_SIZE / INODE_SIZE;   // 16 inodes per block
static constexpr uint64_t ROOT_INO = 64;                                // Inode 64: Block 4, index 0
static constexpr uint64_t FILE1_INO = 65;                               // Inode 65: Block 4, index 1 ("secret.txt")
static constexpr uint64_t FILE2_INO = 66;                               // Inode 66: Block 4, index 2 ("archive.bin")

/**
 * @brief Constructs a valid, synthetic XFS filesystem in memory.
 *
 * Structure created:
 *   - Block 0: Primary Superblock (XFSB, 4KB blocks, 1 AG, rootino=64)
 *   - Block 4, Inode 64: Root directory (Shortform, contains "secret.txt" and "archive.bin")
 *   - Block 4, Inode 65: "secret.txt" (Extent format, references data blocks 10 & 11)
 *   - Block 4, Inode 66: "archive.bin" (B+Tree format, BMDR root points to leaf block 20, data in block 30)
 *   - Blocks 10, 11: Populated with non-zero bytes (0xAA, 0xBB)
 *   - Block 20: Leaf B+Tree metadata block pointing to data block 30
 *   - Block 30: Populated with non-zero bytes (0xCC)
 */
void BuildSyntheticXfsImage(MemoryDiskDevice& dev) {
    uint8_t* disk = dev.GetDiskData();

    // -------------------------------------------------------------------------
    // 1. Write Primary Superblock at Sector 0 (Block 0)
    // -------------------------------------------------------------------------
    XfsSuperblock sb;
    std::memset(&sb, 0, sizeof(sb));
    sb.sb_magicnum   = cpu_to_be32(XFS_SB_MAGIC);
    sb.sb_blocksize  = cpu_to_be32(BLOCK_SIZE);
    sb.sb_dblocks    = cpu_to_be64(DISK_SIZE / BLOCK_SIZE);
    sb.sb_rootino    = cpu_to_be64(ROOT_INO);
    sb.sb_agblocks   = cpu_to_be32(DISK_SIZE / BLOCK_SIZE);
    sb.sb_agcount    = cpu_to_be32(1);
    sb.sb_sectsize   = cpu_to_be16(SECTOR_SIZE);
    sb.sb_inodesize  = cpu_to_be16(INODE_SIZE);
    sb.sb_inopblock  = cpu_to_be16(INODES_PER_BLOCK);
    sb.sb_blocklog   = 12; // log2(4096)
    sb.sb_sectlog    = 9;  // log2(512)
    sb.sb_inodelog   = 8;  // log2(256)
    sb.sb_inopblog   = 4;  // log2(16)
    sb.sb_agblklog   = 10; // log2(1024)
    sb.sb_versionnum = cpu_to_be16(4 | XFS_SB_VERSION_DIRV2BIT);
    sb.sb_features2  = cpu_to_be32(0); // Standard shortform / no ftype

    std::memcpy(disk, &sb, sizeof(sb));

    // -------------------------------------------------------------------------
    // 2. Setup Root Directory Inode (64) in Block 4, Offset 0 (Shortform Format)
    // -------------------------------------------------------------------------
    size_t rootOffset = 4 * BLOCK_SIZE + 0 * INODE_SIZE;
    uint8_t* rootPtr = disk + rootOffset;

    *reinterpret_cast<uint16_t*>(rootPtr + 0) = cpu_to_be16(XFS_DINODE_MAGIC);
    *reinterpret_cast<uint16_t*>(rootPtr + 2) = cpu_to_be16(XFS_S_IFDIR | 0755);
    rootPtr[4] = 2;                     // v2 inode (v4 filesystem)
    rootPtr[5] = XFS_DINODE_FMT_LOCAL;  // Shortform directory stored directly in data fork
    *reinterpret_cast<uint64_t*>(rootPtr + 56) = cpu_to_be64(64); // Size in bytes

    // Shortform directory payload starting at data fork offset 100
    uint8_t* sfPtr = rootPtr + 100;
    XfsDir2SfHdr* sfHdr = reinterpret_cast<XfsDir2SfHdr*>(sfPtr);
    sfHdr->count = 2;   // Two active entries
    sfHdr->i8count = 0; // Inodes fit in 32-bit fields
    *reinterpret_cast<uint32_t*>(sfHdr->parent) = cpu_to_be32(ROOT_INO);

    // Entry 1: "secret.txt" -> Inode 65
    size_t e1Off = 6; // Offset after shortform header (with 32-bit parent)
    sfPtr[e1Off + 0] = 10; // namelen
    sfPtr[e1Off + 1] = 0;  // tag hi
    sfPtr[e1Off + 2] = 0;  // tag lo
    std::memcpy(&sfPtr[e1Off + 3], "secret.txt", 10);
    *reinterpret_cast<uint32_t*>(&sfPtr[e1Off + 3 + 10]) = cpu_to_be32(FILE1_INO);

    // Entry 2: "archive.bin" -> Inode 66
    size_t e2Off = e1Off + 3 + 10 + 4; // 23
    sfPtr[e2Off + 0] = 11; // namelen
    sfPtr[e2Off + 1] = 0;
    sfPtr[e2Off + 2] = 0;
    std::memcpy(&sfPtr[e2Off + 3], "archive.bin", 11);
    *reinterpret_cast<uint32_t*>(&sfPtr[e2Off + 3 + 11]) = cpu_to_be32(FILE2_INO);

    // -------------------------------------------------------------------------
    // 3. Setup File 1 Inode (65): "secret.txt" (Extent Format)
    // -------------------------------------------------------------------------
    size_t f1Offset = 4 * BLOCK_SIZE + 1 * INODE_SIZE;
    uint8_t* f1Ptr = disk + f1Offset;

    *reinterpret_cast<uint16_t*>(f1Ptr + 0) = cpu_to_be16(XFS_DINODE_MAGIC);
    *reinterpret_cast<uint16_t*>(f1Ptr + 2) = cpu_to_be16(XFS_S_IFREG | 0644);
    f1Ptr[4] = 2;
    f1Ptr[5] = XFS_DINODE_FMT_EXTENTS; // Stored in direct extent records
    *reinterpret_cast<uint64_t*>(f1Ptr + 56) = cpu_to_be64(8192); // 8192 bytes (2 blocks)
    *reinterpret_cast<uint64_t*>(f1Ptr + 64) = cpu_to_be64(2);
    *reinterpret_cast<uint32_t*>(f1Ptr + 76) = cpu_to_be32(1);    // 1 extent record

    // Extent record at data fork offset 100: Logical 0, Physical Block 10, length 2 blocks
    XfsBmbtRec ext1 = MakeExtentRec(0, 10, 2);
    std::memcpy(f1Ptr + 100, &ext1, sizeof(ext1));

    // Populate data blocks 10 and 11 with distinct non-zero bytes (0xAA and 0xBB)
    std::memset(disk + 10 * BLOCK_SIZE, 0xAA, BLOCK_SIZE);
    std::memset(disk + 11 * BLOCK_SIZE, 0xBB, BLOCK_SIZE);

    // -------------------------------------------------------------------------
    // 4. Setup File 2 Inode (66): "archive.bin" (B+Tree Format)
    // -------------------------------------------------------------------------
    size_t f2Offset = 4 * BLOCK_SIZE + 2 * INODE_SIZE;
    uint8_t* f2Ptr = disk + f2Offset;

    *reinterpret_cast<uint16_t*>(f2Ptr + 0) = cpu_to_be16(XFS_DINODE_MAGIC);
    *reinterpret_cast<uint16_t*>(f2Ptr + 2) = cpu_to_be16(XFS_S_IFREG | 0644);
    f2Ptr[4] = 2;
    f2Ptr[5] = XFS_DINODE_FMT_BTREE;   // Escalated to B+Tree format
    *reinterpret_cast<uint64_t*>(f2Ptr + 56) = cpu_to_be64(4096);
    *reinterpret_cast<uint64_t*>(f2Ptr + 64) = cpu_to_be64(2);    // 1 data block + 1 btree block
    *reinterpret_cast<uint32_t*>(f2Ptr + 76) = cpu_to_be32(1);

    // BMDR root header at data fork offset 100
    XfsBmdrBlock* bmdr = reinterpret_cast<XfsBmdrBlock*>(f2Ptr + 100);
    bmdr->bb_level = cpu_to_be16(1);   // Level 1: points to leaf blocks
    bmdr->bb_numrecs = cpu_to_be16(1);

    // Key at offset 104 (startoff = 0)
    *reinterpret_cast<uint64_t*>(f2Ptr + 104) = 0;
    // Pointer at offset 112: fsbno 20 (points to B+Tree leaf block 20)
    *reinterpret_cast<uint64_t*>(f2Ptr + 112) = cpu_to_be64(20);

    // Block 20: Indirect leaf B+Tree block on disk
    uint8_t* b20Ptr = disk + 20 * BLOCK_SIZE;
    XfsBtreeBlock* btreeHdr = reinterpret_cast<XfsBtreeBlock*>(b20Ptr);
    btreeHdr->bb_magic = cpu_to_be32(XFS_BMAP_MAGIC);
    btreeHdr->bb_level = 0;            // Level 0: leaf block containing extent records
    btreeHdr->bb_numrecs = cpu_to_be16(1);

    // Extent record at offset 24 of block 20: Logical 0, Physical Block 30, length 1 block
    XfsBmbtRec ext2 = MakeExtentRec(0, 30, 1);
    std::memcpy(b20Ptr + 24, &ext2, sizeof(ext2));

    // Populate data block 30 with distinct non-zero bytes (0xCC)
    std::memset(disk + 30 * BLOCK_SIZE, 0xCC, BLOCK_SIZE);
}

// ============================================================================
// Comprehensive Unit Test Suite
// ============================================================================

void TestXfsDriver() {
    std::cout << "=============================================\n";
    std::cout << "        XFS Secure Deletion Unit Tests        \n";
    std::cout << "=============================================\n\n";

    MemoryDiskDevice dev(DISK_SIZE, SECTOR_SIZE);
    BuildSyntheticXfsImage(dev);

    // Hardware layer wrapping the in-memory OS pipe
    Hardware::HDDController hdd(&dev);
    // Filesystem driver layer
    XfsDriver driver(&hdd);

    // -------------------------------------------------------------------------
    // Test 1: Mount & Geometry Verification
    // -------------------------------------------------------------------------
    std::cout << "[Test 1] Testing XFS Mount()...\n";
    bool mountOk = driver.Mount();
    assert(mountOk);
    assert(driver.GetBlockSize() == 4096);
    assert(driver.GetAgCount() == 1);
    assert(driver.GetRootIno() == 64);
    std::cout << "  -> [PASSED] Superblock parsed and mounted successfully!\n\n";

    // Verify initial data bytes are in place before testing deletion
    uint8_t* rawDisk = dev.GetDiskData();
    assert(rawDisk[10 * BLOCK_SIZE] == 0xAA);
    assert(rawDisk[11 * BLOCK_SIZE] == 0xBB);
    assert(rawDisk[30 * BLOCK_SIZE] == 0xCC);

    // -------------------------------------------------------------------------
    // Test 2: EraseFile("secret.txt") - Extent Format File
    // -------------------------------------------------------------------------
    std::cout << "[Test 2] Erasing 'secret.txt' (Extent format)...\n";
    bool erase1Ok = driver.EraseFile("secret.txt");
    assert(erase1Ok);

    // Verify data blocks 10 and 11 were overwritten with 0x00
    for (size_t i = 0; i < 2 * BLOCK_SIZE; ++i) {
        assert(rawDisk[10 * BLOCK_SIZE + i] == 0x00);
    }
    std::cout << "  -> [PASSED] Target file data blocks 10 & 11 completely zeroed!\n";

    // Verify on-disk Inode 65 was overwritten with 0x00
    size_t f1Offset = 4 * BLOCK_SIZE + 1 * INODE_SIZE;
    for (size_t i = 0; i < INODE_SIZE; ++i) {
        assert(rawDisk[f1Offset + i] == 0x00);
    }
    std::cout << "  -> [PASSED] Target Inode 65 metadata on disk completely zeroed!\n";

    // Verify that subsequent lookup fails because filename was wiped from parent directory
    assert(!driver.EraseFile("secret.txt"));
    std::cout << "  -> [PASSED] 'secret.txt' is unresolvable after erasure!\n\n";

    // -------------------------------------------------------------------------
    // Test 3: EraseFile("archive.bin") - B+Tree Format File
    // -------------------------------------------------------------------------
    std::cout << "[Test 3] Erasing 'archive.bin' (B+Tree format)...\n";
    bool erase2Ok = driver.EraseFile("archive.bin");
    assert(erase2Ok);

    // Verify data block 30 was overwritten with 0x00
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        assert(rawDisk[30 * BLOCK_SIZE + i] == 0x00);
    }
    std::cout << "  -> [PASSED] B+Tree data block 30 completely zeroed!\n";

    // Verify indirect B+Tree metadata block 20 was overwritten with 0x00
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        assert(rawDisk[20 * BLOCK_SIZE + i] == 0x00);
    }
    std::cout << "  -> [PASSED] B+Tree metadata block 20 completely zeroed!\n";

    // Verify on-disk Inode 66 was overwritten with 0x00
    size_t f2Offset = 4 * BLOCK_SIZE + 2 * INODE_SIZE;
    for (size_t i = 0; i < INODE_SIZE; ++i) {
        assert(rawDisk[f2Offset + i] == 0x00);
    }
    std::cout << "  -> [PASSED] Target Inode 66 metadata on disk completely zeroed!\n\n";

    // -------------------------------------------------------------------------
    // Test 4: WipeVolume() - Surgical Volume-Wide Wipe
    // -------------------------------------------------------------------------
    std::cout << "[Test 4] Testing Surgical WipeVolume()...\n";
    // Write test data in unreserved user blocks
    std::memset(rawDisk + 50 * BLOCK_SIZE, 0x55, BLOCK_SIZE);
    std::memset(rawDisk + 51 * BLOCK_SIZE, 0x66, BLOCK_SIZE);

    bool wipeOk = driver.WipeVolume();
    assert(wipeOk);

    // Verify user blocks 50 and 51 were zeroed
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        assert(rawDisk[50 * BLOCK_SIZE + i] == 0x00);
        assert(rawDisk[51 * BLOCK_SIZE + i] == 0x00);
    }
    std::cout << "  -> [PASSED] User blocks 50 & 51 zeroed during volume wipe!\n";

    // Verify Superblock is preserved intact
    const XfsSuperblock* sbAfter = reinterpret_cast<const XfsSuperblock*>(rawDisk);
    assert(be32_to_cpu(sbAfter->sb_magicnum) == XFS_SB_MAGIC);
    std::cout << "  -> [PASSED] Superblock preserved intact!\n";

    // Verify Root Inode block is preserved intact
    uint16_t rootMagic = be16_to_cpu(*reinterpret_cast<const uint16_t*>(rawDisk + 4 * BLOCK_SIZE));
    assert(rootMagic == XFS_DINODE_MAGIC);
    std::cout << "  -> [PASSED] Root Inode preserved intact!\n\n";

    std::cout << "=============================================\n";
    std::cout << "   ALL XFS TESTS PASSED SUCCESSFULLY! (100%) \n";
    std::cout << "=============================================\n";
}

int main() {
    TestXfsDriver();
    return 0;
}
